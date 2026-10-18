#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/logic.h"
using AckFn=uint8_t* (*)(void*,void*,uint8_t*,uint8_t*);
using TextFn=bool (*)(void*,const char*,int,int,bool,double,uint64_t);
static AckFn ack;
static TextFn text;
static volatile LONG quit=0;
static uint8_t connection[512];
static int checks=0;
static void Check(bool success,const char* name) {
    if(!success) {printf("FAIL %s\n",name);ExitProcess(1);}
    ++checks;
}
static DWORD WINAPI Stress(void*) {
    while(!InterlockedCompareExchange(&quit,0,0)) {
        uint8_t buffer[32];
        ack(connection,nullptr,buffer,buffer+sizeof(buffer));
    }
    return 0;
}
static bool Send(const char* s) {return text(nullptr,s,0,0,false,0,0);}
static unsigned Delay() {
    uint8_t b[16];
    Check(ack(connection,nullptr,b,b+sizeof(b))==b+5,"ACK bounds");
    return unsigned(b[3]) | unsigned(b[4])<<8;
}
int main(int argc,char** argv) {
    Check(argc==3,"arguments");
    Check(fl::Parse("fake_latency 0")==fl::Command::Enable,"enable parser");
    Check(fl::Parse(" FAKE_LATENCY\t0; \r\n")==fl::Command::Enable,"whitespace parser");
    Check(fl::Parse("fake_latency disable")==fl::Command::Disable,"disable parser");
    Check(fl::Parse("fake_latency unload")==fl::Command::Unload,"unload parser");
    Check(fl::Parse("fake_latencyx 0")==fl::Command::Other,"prefix isolation");
    Check(fl::Parse("fake_latency 0;status")==fl::Command::Other,"compound input isolation");
    Check(fl::Parse("status")==fl::Command::Other,"normal command parser");
    Check(fl::Parse("fake_latency 500")==fl::Command::Ignore,"invalid argument consumed");
    for(unsigned rtt=0;rtt<=2000;++rtt) {
        uint8_t b[]={0x98,11,22,16,0};
        Check(fl::Adjust(b,5,rtt),"ACK adjustment");
        unsigned expected=16+((rtt*1000+1016)>>5);
        if(expected>65534)expected=65534;
        Check((unsigned(b[3]) | unsigned(b[4])<<8)==expected,"ACK correction math");
        Check(b[0]==0x98 && b[1]==11 && b[2]==22,"ACK identifiers preserved");
    }
    uint8_t bad[]={0x98,0,0,0xff,0xff};
    Check(!fl::Adjust(bad,5,80),"unknown ACK timing preserved");
    Check(!fl::Adjust(bad,4,80),"short frame guarded");
    Check(!fl::Adjust(bad,5,-1),"invalid RTT guarded");
    HMODULE tier=LoadLibraryA("tier0.dll"),sockets=LoadLibraryA("steamnetworkingsockets.dll");
    Check(tier && sockets,"fixtures loaded");
    text=reinterpret_cast<TextFn>(GetProcAddress(tier,"?AddText@CCommandBuffer@@QEAA_NPEBDHH_NN_K@Z"));
    ack=reinterpret_cast<AckFn>(GetProcAddress(sockets,"FixtureAck"));
    Check(text && ack,"fixture functions");
    int rtt=80;memcpy(connection+0x1cc,&rtt,4);
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,L"Local\fakelatency_fixture_ready");
    for(int cycle=0;cycle<20;++cycle) {
        ResetEvent(ready);
        HMODULE dll=LoadLibraryA(argv[1]);
        Check(dll!=nullptr,"DLL loaded");
        Check(WaitForSingleObject(ready,10000)==WAIT_OBJECT_0,"hook startup");
        Check(Delay()==16,"initially disabled");
        volatile LONG forwardingErrors=0;
        Check(!text(const_cast<LONG*>(&forwardingErrors),"status",17,23,true,9.25,0x1122334455667788ull),"other commands forwarded");
        Check(forwardingErrors==0,"all seven ABI arguments preserved");
        Check(Send("fake_latency 0"),"enable consumed silently");
        Check(Delay()==2547,"zero-latency ACK correction");
        Check(Send("fake_latency disable"),"disable consumed silently");
        Check(Delay()==16,"original ACK restored");
        HANDLE threads[4];quit=0;
        for(auto& t:threads)t=CreateThread(nullptr,0,Stress,nullptr,0,nullptr);
        Check(Send("fake_latency 0"),"enable during traffic");
        Sleep(20);
        Check(Send("fake_latency unload"),"unload consumed silently");
        for(int i=0;i<1000 && GetModuleHandleA(argv[1]);++i)Sleep(10);
        Check(!GetModuleHandleA(argv[1]),"DLL actually unmapped");
        Check(Delay()==16,"traffic restored after unload");
        quit=1;
        Check(WaitForMultipleObjects(4,threads,TRUE,10000)==WAIT_OBJECT_0,"traffic threads complete");
        for(auto t:threads)CloseHandle(t);
        Check(!Send("fake_latency 0"),"command hook removed");
    }
    HMODULE release=LoadLibraryA(argv[2]);
    Check(release!=nullptr,"release hash-gate test load");
    for(int i=0;i<1000 && GetModuleHandleA(argv[2]);++i)Sleep(10);
    Check(!GetModuleHandleA(argv[2]),"release rejects mismatched network module");
    Check(!Send("fake_latency 0") && Delay()==16,"mismatch leaves fixtures unchanged");
    CloseHandle(ready);
    printf("PASS %d; real_game_tested=false\n",checks);
    return 0;
}
