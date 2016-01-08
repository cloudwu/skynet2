#ifndef simple_socket_h
#define simple_socket_h

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)

#include "winsocket.h"

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#define SOCK_EAGAIN EAGAIN
#define SOCK_EINPROGRESS EINPROGRESS

static void simplesocket_init() {}

#endif

#endif
