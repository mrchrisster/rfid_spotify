#pragma once
#include <stddef.h>
inline int mbedtls_base64_encode(unsigned char* out,size_t cap,size_t* length,const unsigned char* in,size_t n){
 const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 *length=((n+2)/3)*4;if(cap<=*length)return -1;
 size_t j=0;for(size_t i=0;i<n;i+=3){unsigned v=unsigned(in[i])<<16;if(i+1<n)v|=unsigned(in[i+1])<<8;if(i+2<n)v|=in[i+2];
 out[j++]=alphabet[(v>>18)&63];out[j++]=alphabet[(v>>12)&63];out[j++]=i+1<n?alphabet[(v>>6)&63]:'=';out[j++]=i+2<n?alphabet[v&63]:'=';}
 out[j]=0;return 0;
}
