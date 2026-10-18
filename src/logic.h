#pragma once
#include <stdint.h>
#include <stddef.h>
namespace fl {
enum class Command { Other, Enable, Disable, Unload, Ignore };
inline bool Space(char c) { return c==' ' || c=='\t'; }
inline bool Tail(char c) { return Space(c) || c=='\r' || c=='\n'; }
inline bool Equal(const char* a, size_t n, const char* b) {
    size_t i=0;
    for (;i<n && b[i];++i) {
        char c=a[i];
        if(c>='A' && c<='Z') c+=32;
        if(c!=b[i]) return false;
    }
    return i==n && b[i]==0;
}
inline Command Parse(const char* s) {
    if(!s) return Command::Other;
    size_t length=0;
    while(length<128 && s[length]) ++length;
    if(length==128) return Command::Other;
    while(length && Tail(s[length-1])) --length;
    if(length && s[length-1]==';') {
        --length;
        while(length && Tail(s[length-1])) --length;
    }
    for(size_t i=0;i<length;++i)
        if(s[i]==';' || s[i]=='\r' || s[i]=='\n') return Command::Other;
    size_t start=0;
    while(start<length && Space(s[start])) ++start;
    size_t end=start;
    while(end<length && !Space(s[end])) ++end;
    if(!Equal(s+start,end-start,"fake_latency")) return Command::Other;
    start=end;
    while(start<length && Space(s[start])) ++start;
    size_t n=length-start;
    if(Equal(s+start,n,"0")) return Command::Enable;
    if(Equal(s+start,n,"disable")) return Command::Disable;
    if(Equal(s+start,n,"unload")) return Command::Unload;
    return Command::Ignore;
}
inline bool Adjust(uint8_t* data, size_t length, int rtt) {
    if(!data || length<5 || (data[0]&0xf8)!=0x98 || rtt<0 || rtt>2000) return false;
    uint32_t delay=uint32_t(data[3]) | uint32_t(data[4])<<8;
    if(delay==0xffff) return false;
    uint32_t value=delay+((uint32_t(rtt)*1000+1016)>>5);
    if(value>0xfffe) value=0xfffe;
    data[3]=uint8_t(value); data[4]=uint8_t(value>>8);
    return true;
}
}
