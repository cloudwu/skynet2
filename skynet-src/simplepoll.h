#ifndef simple_poll_h
#define simple_poll_h

#include <stdbool.h>

#include "simplesocket.h"

struct poll_event {
	void * s;
	bool read;
	bool write;
};

#ifdef __linux__
#include "socket_epoll.h"
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__NetBSD__)
#include "socket_kqueue.h"
#else
#include "socket_select.h"
#endif

#endif
