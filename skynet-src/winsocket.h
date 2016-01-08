#ifndef windows_socket_h
#define windows_socket_h

// If you need more connection, change it.
// But we don't sugguest use windows version in production 
#define FD_SETSIZE 4096

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SO_NOSIGPIPE 0		// ignore it, don't support

#ifdef errno
#undef errno
#endif
#define errno WSAGetLastError()

#define SOCK_EAGAIN WSATRY_AGAIN
// In windows, connect returns WSAEWOULDBLOCK rather than WSAEINPROGRESS
#define SOCK_EINPROGRESS WSAEWOULDBLOCK

// only support fcntl(fd, F_SETFL, O_NONBLOCK)
#define F_SETFL 0
#define O_NONBLOCK 0
typedef u_short sa_family_t;
typedef int socklen_t;

static int 
win_getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
	if (optname == SO_NOSIGPIPE) {
		// ignore
		return 0;
	}
	int size = (int)*optlen;
	int ret = getsockopt(sockfd, level, optname, (char *)optval, &size);
	if (ret == 0) {
		*optlen = size;
		return 0;
	} else {
		return -1;
	}
}

static int 
win_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
	if (optname == SO_NOSIGPIPE) {
		// ignore
		return 0;
	}
	int ret = setsockopt(sockfd, level, optname, (const char *)optval, (int)optlen);
	if (ret == 0) {
		return 0;
	} else {
		return -1;
	}
}

static int
fcntl(int fd, int cmd, int value) {
	unsigned long on = 1;
	return ioctlsocket(fd, FIONBIO, &on);
}

#define NS_INT16SZ   2
#define NS_IN6ADDRSZ  16

static const char *
inet_ntop4(const unsigned char *src, char *dst, size_t size) {
	char tmp[sizeof "255.255.255.255"];
	size_t len = snprintf(tmp, sizeof(tmp), "%u.%u.%u.%u", src[0], src[1], src[2], src[3]);
	if (len >= size) {
		return NULL;
	}
	memcpy(dst, tmp, len + 1);

	return dst;
}

static const char *
inet_ntop(int af, const void *src, char *dst,  socklen_t size) {
	switch (af) {
	case AF_INET:
		return inet_ntop4((const unsigned char*)src, dst, size);
	case AF_INET6:
		// don't support ipv6 in windows
		return NULL;
	default:
		return NULL;
	}
}

static int
inet_pton(int af, const char *src, void *dst) {
	if (af == AF_INET) {
		unsigned long ip = inet_addr(src);
		if (ip == INADDR_NONE)
			return 0;
		memcpy(dst,&ip,sizeof(ip));
		return 1;
	} else {
		// only support ipv4 in windows
		return 0;
	}
}

static void
simplesocket_init() {
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2,2), &wsaData);
}

#define getsockopt win_getsockopt
#define setsockopt win_setsockopt
#define close closesocket

#endif