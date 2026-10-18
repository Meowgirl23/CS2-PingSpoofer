#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../third_party/minhook/include/MinHook.h"
#include "logic.h"

using AckFn=uint8_t* (__fastcall*)(void*,void*,uint8_t*,uint8_t*);
using TextFn=bool (__fastcall*)(void*,const char*,int,int,bool,double,uint64_t);
struct RelayState {
    volatile LONG active;
    volatile LONG gate;
    void* callback;
    void* fallback;
};
struct Hook {
    void* target;
    void* original;
    void* relay;
    RelayState* state;
    bool created;
};
static Hook g_ack{},g_text{};
static HMODULE g_module;
static HANDLE g_stop;
static volatile LONG g_enabled=0;

static bool DigestMatches(HMODULE module,const uint8_t expected[32]) {
    wchar_t path[32768];
    if(!GetModuleFileNameW(module,path,32768)) return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    uint8_t actual[32],buffer[65536];
    bool ok=false;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0 &&
       BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0) {
        ok=true;
        for(;;) {
            DWORD n=0;
            if(!ReadFile(file,buffer,sizeof(buffer),&n,nullptr)) {ok=false;break;}
            if(!n) break;
            if(BCryptHashData(hash,buffer,n,0)<0) {ok=false;break;}
        }
        if(ok) ok=BCryptFinishHash(hash,actual,32,0)>=0 && memcmp(actual,expected,32)==0;
    }
    if(hash) BCryptDestroyHash(hash);
    if(alg) BCryptCloseAlgorithmProvider(alg,0);
    CloseHandle(file);
    return ok;
}
static void* LocateAck(HMODULE module) {
    auto base=reinterpret_cast<uint8_t*>(module);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return nullptr;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64) return nullptr;
    const uint8_t signature[]={0x48,0x89,0x54,0x24,0x10,0x53,0x57,0x41,0x55,0x41,0x56,0x48,0x81,0xec,0x88,0,0,0,0x48,0x83,0xb9,0x20,0x0a,0,0,0};
    void* found=nullptr;
    auto section=IMAGE_FIRST_SECTION(nt);
    for(unsigned s=0;s<nt->FileHeader.NumberOfSections;++s) {
        if(!(section[s].Characteristics&IMAGE_SCN_MEM_EXECUTE)) continue;
        size_t n=section[s].Misc.VirtualSize;
        if(n<sizeof(signature)) continue;
        auto start=base+section[s].VirtualAddress;
        for(size_t i=0;i<=n-sizeof(signature);++i) {
            if(memcmp(start+i,signature,sizeof(signature))) continue;
            if(found) return nullptr;
            found=start+i;
        }
    }
    return found;
}
static bool CreateRelay(Hook& hook,void* callback) {
    hook.relay=VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!hook.relay) return false;
    hook.state=reinterpret_cast<RelayState*>(static_cast<uint8_t*>(hook.relay)+4096);
    hook.state->gate=1;
    hook.state->callback=callback;
    uint8_t code[]={
        0x48,0x83,0xec,0x48,
        0x49,0xbb,0,0,0,0,0,0,0,0,
        0xf0,0x41,0xff,0x03,
        0x48,0x8b,0x44,0x24,0x70,0x48,0x89,0x44,0x24,0x20,
        0x48,0x8b,0x44,0x24,0x78,0x48,0x89,0x44,0x24,0x28,
        0x48,0x8b,0x84,0x24,0x80,0,0,0,0x48,0x89,0x44,0x24,0x30,
        0x41,0x83,0x7b,0x04,0,0x74,0x06,
        0x49,0x8b,0x43,0x08,0xeb,0x04,
        0x49,0x8b,0x43,0x10,
        0xff,0xd0,
        0x49,0xbb,0,0,0,0,0,0,0,0,
        0xf0,0x41,0xff,0x0b,
        0x48,0x83,0xc4,0x48,0xc3
    };
    uintptr_t pointer=reinterpret_cast<uintptr_t>(hook.state);
    memcpy(code+6,&pointer,8);
    for(size_t i=14;i+10<=sizeof(code);++i) {
        if(code[i]==0x49 && code[i+1]==0xbb) {memcpy(code+i+2,&pointer,8);break;}
    }
    memcpy(hook.relay,code,sizeof(code));
    auto table=reinterpret_cast<RUNTIME_FUNCTION*>(reinterpret_cast<uint8_t*>(hook.state)+128);
    const uint8_t unwind[]={1,4,1,0,4,0x82,0,0};
    memcpy(reinterpret_cast<uint8_t*>(hook.state)+256,unwind,sizeof(unwind));
    table->BeginAddress=0;
    table->EndAddress=sizeof(code);
    table->UnwindData=4096+256;
    if(!RtlAddFunctionTable(table,1,reinterpret_cast<DWORD64>(hook.relay))) return false;
    DWORD old=0;
    if(!VirtualProtect(hook.relay,4096,PAGE_EXECUTE_READ,&old)) return false;
    FlushInstructionCache(GetCurrentProcess(),hook.relay,sizeof(code));
    if(MH_CreateHook(hook.target,hook.relay,&hook.original)!=MH_OK) return false;
    hook.created=true;
    hook.state->fallback=hook.original;
    return true;
}
static uint8_t* __fastcall Ack(void* connection,void* helper,uint8_t* output,uint8_t* end) {
    auto result=reinterpret_cast<AckFn>(g_ack.original)(connection,helper,output,end);
    uintptr_t first=reinterpret_cast<uintptr_t>(output),last=reinterpret_cast<uintptr_t>(result),limit=reinterpret_cast<uintptr_t>(end);
    if(InterlockedCompareExchange(&g_enabled,0,0) && connection && first && last>=first && last<=limit && last-first>=5) {
        int rtt;
        memcpy(&rtt,reinterpret_cast<uint8_t*>(connection)+0x1cc,sizeof(rtt));
        fl::Adjust(output,last-first,rtt);
    }
    return result;
}
static bool __fastcall Text(void* self,const char* text,int a,int b,bool c,double d,uint64_t e) {
    auto command=fl::Parse(text);
    if(command==fl::Command::Other) return reinterpret_cast<TextFn>(g_text.original)(self,text,a,b,c,d,e);
    if(command==fl::Command::Enable) InterlockedExchange(&g_enabled,1);
    if(command==fl::Command::Disable) InterlockedExchange(&g_enabled,0);
    if(command==fl::Command::Unload) {InterlockedExchange(&g_enabled,0);SetEvent(g_stop);}
    return true;
}
static bool Disable(Hook& hook) {
    if(!hook.created) return true;
    InterlockedExchange(&hook.state->gate,0);
    auto result=MH_DisableHook(hook.target);
    if(result!=MH_OK && result!=MH_ERROR_DISABLED) return false;
    InterlockedExchangePointer(&hook.state->fallback,hook.target);
    return true;
}
static void WaitIdle(Hook& hook) {
    if(!hook.created) return;
    while(InterlockedCompareExchange(&hook.state->active,0,0)) Sleep(1);
}
static DWORD WINAPI Worker(void*) {
    HMODULE tier=nullptr,sockets=nullptr;
    for(unsigned i=0;i<6000;++i) {
        tier=GetModuleHandleW(L"tier0.dll");
        sockets=GetModuleHandleW(L"steamnetworkingsockets.dll");
        if(tier && sockets) break;
        Sleep(100);
    }
    bool initialized=false;
    bool ready=false;
    if(tier && sockets) {
#ifndef FAKELATENCY_TEST
        const uint8_t expected[]={0xce,0x6,0x51,0xd0,0xc,0x31,0x16,0x6,0xc7,0x93,0x6f,0xa7,0x30,0x14,0x9d,0x2b,0x7e,0x25,0x55,0x74,0x30,0xef,0x5c,0x32,0x91,0xee,0xd4,0x7d,0xfd,0xcf,0x7,0x36};
        ready=DigestMatches(sockets,expected);
#else
        ready=true;
#endif
        if(ready) {
#ifdef FAKELATENCY_TEST
            g_ack.target=reinterpret_cast<void*>(GetProcAddress(sockets,"FixtureAck"));
#else
            g_ack.target=LocateAck(sockets);
#endif
            g_text.target=reinterpret_cast<void*>(GetProcAddress(tier,"?AddText@CCommandBuffer@@QEAA_NPEBDHH_NN_K@Z"));
            initialized=MH_Initialize()==MH_OK;
            ready=initialized && g_ack.target && g_text.target && CreateRelay(g_ack,reinterpret_cast<void*>(&Ack)) && CreateRelay(g_text,reinterpret_cast<void*>(&Text));
            if(ready) ready=MH_EnableHook(g_ack.target)==MH_OK && MH_EnableHook(g_text.target)==MH_OK;
        }
    }
#ifdef FAKELATENCY_TEST
    HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,L"Local\fakelatency_fixture_ready");
    if(event) {SetEvent(event);CloseHandle(event);}
#endif
    if(ready) WaitForSingleObject(g_stop,INFINITE);
    InterlockedExchange(&g_enabled,0);
    while(!Disable(g_ack)) Sleep(10);
    while(!Disable(g_text)) Sleep(10);
    WaitIdle(g_ack); WaitIdle(g_text);
    if(g_ack.created) MH_RemoveHook(g_ack.target);
    if(g_text.created) MH_RemoveHook(g_text.target);
    if(initialized) MH_Uninitialize();
    CloseHandle(g_stop);
    FreeLibraryAndExitThread(g_module,0);
}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        g_module=module;
        DisableThreadLibraryCalls(module);
        g_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!g_stop) return FALSE;
        HANDLE thread=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);
        if(!thread) {CloseHandle(g_stop);return FALSE;}
        CloseHandle(thread);
    }
    return TRUE;
}
