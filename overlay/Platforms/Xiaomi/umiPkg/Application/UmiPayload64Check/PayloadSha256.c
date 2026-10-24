/* SPDX-License-Identifier: MIT */
/* Adapted from UniXNU NativeAdrenoSha256.cpp at
 * 7b52e7968c76453cdc18b5aa174fc368d5293b91. Only C syntax, UEFI types
 * and memory functions differ; algorithm remains unchanged. */
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

static const UINT32 K[64]={
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
    0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
    0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
    0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
    0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
    0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
    0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
    0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
    0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
static inline UINT32 rotr(UINT32 value,unsigned count)
{return (value>>count)|(value<<(32-count));}
static void compress(UINT32 state[8],const UINT8 block[64])
{
    UINT32 w[64];
    for(unsigned i=0;i<16;++i)
        w[i]=((UINT32)(block[i*4])<<24)|((UINT32)(block[i*4+1])<<16)|
            ((UINT32)(block[i*4+2])<<8)|(UINT32)(block[i*4+3]);
    for(unsigned i=16;i<64;++i) {
        const UINT32 s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
        const UINT32 s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    UINT32 a=state[0],b=state[1],c=state[2],d=state[3];
    UINT32 e=state[4],f=state[5],g=state[6],h=state[7];
    for(unsigned i=0;i<64;++i) {
        const UINT32 s1=rotr(e,6)^rotr(e,11)^rotr(e,25);
        const UINT32 ch=(e&f)^(~e&g);
        const UINT32 t1=h+s1+ch+K[i]+w[i];
        const UINT32 s0=rotr(a,2)^rotr(a,13)^rotr(a,22);
        const UINT32 maj=(a&b)^(a&c)^(b&c);
        const UINT32 t2=s0+maj;
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;
    state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
}
void PayloadSha256(const UINT8 *bytes,UINTN length,UINT8 digest[32])
{
    UINT32 state[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    UINTN at=0;
    while(length-at>=64) {compress(state,bytes+at);at+=64;}
    UINT8 tail[64];
    const UINTN rest=length-at;
    ZeroMem(tail,sizeof(tail));
    if(rest)CopyMem(tail,bytes+at,rest);
    tail[rest]=0x80;
    if(rest>=56) {compress(state,tail);ZeroMem(tail,sizeof(tail));}
    const UINT64 bits=(UINT64)(length)*8;
    for(unsigned i=0;i<8;++i)tail[63-i]=(UINT8)(bits>>(8*i));
    compress(state,tail);
    for(unsigned i=0;i<8;++i) {
        digest[i*4]=(UINT8)(state[i]>>24);digest[i*4+1]=(UINT8)(state[i]>>16);
        digest[i*4+2]=(UINT8)(state[i]>>8);digest[i*4+3]=(UINT8)(state[i]);
    }
}
