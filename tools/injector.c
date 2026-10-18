#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

static int Fail(const wchar_t* stage) {
    fwprintf(stderr,L"%ls: Windows error %lu\n",stage,GetLastError());
    return 1;
}
static uintptr_t Module(DWORD pid,const wchar_t* name,BOOL full) {
    for(unsigned attempt=0;attempt<20;++attempt) {
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
        if(snapshot==INVALID_HANDLE_VALUE) {
            if(GetLastError()==ERROR_BAD_LENGTH) {Sleep(10);continue;}
            return 0;
        }
        MODULEENTRY32W item={0};item.dwSize=sizeof(item);
        uintptr_t result=0;
        if(Module32FirstW(snapshot,&item)) do {
            if(!_wcsicmp(full?item.szExePath:item.szModule,name)) {result=(uintptr_t)item.modBaseAddr;break;}
        } while(Module32NextW(snapshot,&item));
        CloseHandle(snapshot);return result;
    }
    return 0;
}
static BOOL Validate(const wchar_t* path) {
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE) return FALSE;
    IMAGE_DOS_HEADER dos={0};IMAGE_NT_HEADERS64 nt={0};DWORD n=0;
    LARGE_INTEGER size,offset;BOOL ok=FALSE;
    if(GetFileSizeEx(file,&size) && ReadFile(file,&dos,sizeof(dos),&n,NULL) && n==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && (uint64_t)dos.e_lfanew+sizeof(nt)<=(uint64_t)size.QuadPart) {
        offset.QuadPart=dos.e_lfanew;
        if(SetFilePointerEx(file,offset,NULL,FILE_BEGIN) && ReadFile(file,&nt,sizeof(nt),&n,NULL) && n==sizeof(nt))
            ok=nt.Signature==IMAGE_NT_SIGNATURE && nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 && nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC && (nt.FileHeader.Characteristics&IMAGE_FILE_DLL);
    }
    CloseHandle(file);if(!ok)SetLastError(ERROR_BAD_EXE_FORMAT);return ok;
}
int wmain(int argc,wchar_t** argv) {
    BOOL check=argc==2 && !_wcsicmp(argv[1],L"--check");
    if(argc>2) {fwprintf(stderr,L"Usage: fakelatency-injector.exe [DLL_PATH | --check]\n");return 1;}
    wchar_t path[32768];
    if(argc==2 && !check) {
        DWORD n=GetFullPathNameW(argv[1],32768,path,NULL);
        if(!n || n>=32768)return Fail(L"DLL path");
    } else {
        DWORD n=GetModuleFileNameW(NULL,path,32768);
        if(!n || n>=32768)return Fail(L"Executable path");
        wchar_t* slash=wcsrchr(path,L'\\');
        if(!slash || slash-path+1+15>=32768) {SetLastError(ERROR_FILENAME_EXCED_RANGE);return Fail(L"DLL path");}
        wcscpy(slash+1,L"fakelatency.dll");
    }
    if(!Validate(path))return Fail(L"x64 DLL validation");
    if(check) {wprintf(L"PASS: x64 DLL present; no injection performed.\n");return 0;}
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return Fail(L"Process snapshot");
    PROCESSENTRY32W entry={0};entry.dwSize=sizeof(entry);DWORD pid=0;unsigned count=0;
    if(Process32FirstW(snapshot,&entry))do {
        if(!_wcsicmp(entry.szExeFile,L"cs2.exe")) {pid=entry.th32ProcessID;++count;}
    }while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    if(count!=1) {fwprintf(stderr,L"Expected one running cs2.exe; found %u.\n",count);return 1;}
    if(Module(pid,path,TRUE)) {wprintf(L"DLL is already loaded.\n");return 0;}
    HMODULE local=GetModuleHandleW(L"kernel32.dll"),owner=NULL;
    FARPROC load=GetProcAddress(local,"LoadLibraryW");
    if(!load || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)(uintptr_t)load,&owner))return Fail(L"Loader address");
    wchar_t ownerPath[32768];DWORD length=GetModuleFileNameW(owner,ownerPath,32768);
    if(!length || length>=32768)return Fail(L"Loader module");
    const wchar_t* ownerName=wcsrchr(ownerPath,L'\\');ownerName=ownerName?ownerName+1:ownerPath;
    uintptr_t remote=Module(pid,ownerName,FALSE);
    if(!remote) {SetLastError(ERROR_MOD_NOT_FOUND);return Fail(L"Remote loader module");}
    LPTHREAD_START_ROUTINE remoteLoad=(LPTHREAD_START_ROUTINE)(remote+(uintptr_t)load-(uintptr_t)owner);
    HANDLE process=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ,FALSE,pid);
    if(!process)return Fail(L"Open cs2.exe");
    SIZE_T bytes=(wcslen(path)+1)*sizeof(wchar_t),written=0;
    void* argument=VirtualAllocEx(process,NULL,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!argument) {DWORD error=GetLastError();CloseHandle(process);SetLastError(error);return Fail(L"Allocate DLL path");}
    if(!WriteProcessMemory(process,argument,path,bytes,&written) || written!=bytes) {
        DWORD error=GetLastError();VirtualFreeEx(process,argument,0,MEM_RELEASE);CloseHandle(process);SetLastError(error);return Fail(L"Write DLL path");
    }
    HANDLE thread=CreateRemoteThread(process,NULL,0,remoteLoad,argument,0,NULL);
    if(!thread) {DWORD error=GetLastError();VirtualFreeEx(process,argument,0,MEM_RELEASE);CloseHandle(process);SetLastError(error);return Fail(L"Load DLL");}
    DWORD wait=WaitForSingleObject(thread,15000);
    if(wait!=WAIT_OBJECT_0) {
        CloseHandle(thread);CloseHandle(process);
        fwprintf(stderr,L"DLL loading has not completed; path buffer retained.\n");return 2;
    }
    DWORD code=0;BOOL gotCode=GetExitCodeThread(thread,&code);
    CloseHandle(thread);VirtualFreeEx(process,argument,0,MEM_RELEASE);CloseHandle(process);
    Sleep(300);
    if(!Module(pid,path,TRUE)) {
        fwprintf(stderr,L"DLL is not loaded, or initialization rejected this game build. Loader result: %lu (%u).\n",code,(unsigned)gotCode);return 1;
    }
    wprintf(L"DLL loaded. Gameplay effect is not verified.\n");return 0;
}
