#pragma once
#include <CommonCrypto/CommonDigest.h>
inline int mbedtls_sha256(const unsigned char* in,size_t size,unsigned char* out,int){return CC_SHA256(in,static_cast<CC_LONG>(size),out)?0:-1;}
