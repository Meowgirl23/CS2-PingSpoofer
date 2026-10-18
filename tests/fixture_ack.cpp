#include <stdint.h>
extern "C" __declspec(dllexport) uint8_t* FixtureAck(void*,void*,uint8_t* out,uint8_t* end) {
    if(!out || !end || end-out<5) return out;
    out[0]=0x98;out[1]=23;out[2]=1;out[3]=16;out[4]=0;
    return out+5;
}
