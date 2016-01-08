#ifndef skynet_socket_h
#define skynet_socket_h

#include "skynet_mq.h"

// Use struct skynet_message_package to send request

typedef unsigned long socket_t;

// Support up two 32bit params
#define SOCKET_REQ_P1(m) (socket_t)(m->id)
#define SOCKET_REQ_P2(m) (socket_t)(m->session)
#define SOCKET_REQ_A1(m,v) (m)->id = (address_t)(v)
#define SOCKET_REQ_A2(m,v) (m)->session = (session_t)(v)

// type of quest
#define SOCKET_REQ_EXIT 1
#define SOCKET_REQ_LISTEN 2	// P1:port P2:backlog DATA:ip address , RET:fd
#define SOCKET_REQ_CONNECT 3	// P1:port DATA:ip address,	RET:fd
#define SOCKET_REQ_CLOSE 4	// P1:fd P2:send all(0) skip unsend(1)
#define SOCKET_REQ_SEND 5	// P1:fd P2:normal(0) low priority(1) DATA:buffer

#define SOCKET_MSG_EXIT 1
#define SOCKET_MSG_DATA 2	// P1:fd, DATA:buffer
#define SOCKET_MSG_CLOSE 3	// P1:fd
#define SOCKET_MSG_OPEN 4	// P1:fd
#define SOCKET_MSG_ACCEPT 5	// P1:listen fd, P2:new fd, DATA:peer name
#define SOCKET_MSG_ERROR 6	// P1:fd DATA:info

struct skynet_socket_server * sss_create();
void sss_release(struct skynet_socket_server *);

// sss_request should call in unique thread 
socket_t sss_request(struct skynet_socket_server *, struct skynet_message_package *req);

// sss_poll should call in unique thread, return 1 means more message
int sss_poll(struct skynet_socket_server *, struct skynet_message_package *msg);

#endif
