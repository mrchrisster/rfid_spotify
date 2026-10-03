#pragma once
#include <netdb.h>
#include <netinet/in.h>
int lwip_getaddrinfo(const char*,const char*,const addrinfo*,addrinfo**);
void lwip_freeaddrinfo(addrinfo*);
