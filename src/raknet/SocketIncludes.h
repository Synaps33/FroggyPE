// All this crap just to include type SOCKET










#ifndef RAKNET_SOCKET_INCLUDES_H
#define RAKNET_SOCKET_INCLUDES_H

#if defined(SF2000)
#include <stdio.h>
#include <cstdio>
typedef int SOCKET;
typedef int socklen_t;
#define INVALID_SOCKET -1
#define closesocket(s) ((void)0)
#include "../platform/sf2000/SF2000_Compat.h"

#define AF_INET 2
#define AF_INET6 23
#define IPPROTO_IP 0
#define IPPROTO_IPV6 41

struct in_addr {
    unsigned int s_addr;
};

struct sockaddr_in {
    short sin_family;
    unsigned short sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

struct sockaddr {
    unsigned short sa_family;
    char sa_data[14];
};

static inline char* inet_ntoa(struct in_addr in) {
    static char buf[32];
    unsigned char* p = (unsigned char*)&in.s_addr;
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
    return buf;
}

static inline unsigned int inet_addr(const char* cp) {
    if (!cp) return 0;
    unsigned int a = 0, b = 0, c = 0, d = 0;
    if (sscanf(cp, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        return (d << 24) | (c << 16) | (b << 8) | a;
    }
    return 0;
}

#ifndef ntohs
#define ntohs(x) __builtin_bswap16((unsigned short)(x))
#endif
#ifndef htons
#define htons(x) __builtin_bswap16((unsigned short)(x))
#endif
#ifndef ntohl
#define ntohl(x) __builtin_bswap32((unsigned int)(x))
#endif
#ifndef htonl
#define htonl(x) __builtin_bswap32((unsigned int)(x))
#endif
#elif defined(_WIN32)
typedef int socklen_t;
// IP_DONTFRAGMENT is different between winsock 1 and winsock 2.  Therefore, Winsock2.h must be linked againt Ws2_32.lib
// winsock.h must be linked against WSock32.lib.  If these two are mixed up the flag won't work correctly
#include <winsock2.h>
#else
#define closesocket close
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#define INVALID_SOCKET -1
//#include "RakMemoryOverride.h"
/// Unix/Linux uses ints for sockets
typedef int SOCKET;
#endif

#endif // RAKNET_SOCKET_INCLUDES_H
