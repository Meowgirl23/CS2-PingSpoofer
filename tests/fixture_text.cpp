#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
extern "C" __declspec(dllexport) bool FixtureAddText(void* self,const char*,int a,int b,bool c,double d,uint64_t e) {
    if(self && (a!=17 || b!=23 || !c || d!=9.25 || e!=0x1122334455667788ull))
        InterlockedIncrement(static_cast<volatile LONG*>(self));
    return false;
}
