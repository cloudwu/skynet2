#ifndef skynet_message_queue_h
#define skynet_message_queue_h

#include "skynet.h"

#define MESSAGE_TYPE_REQUEST 1
#define MESSAGE_TYPE_RESPONSE 2
#define MESSAGE_TYPE_POST 3
#define MESSAGE_TYPE_ERROR 4

struct skynet_message_package {
	int type;
	address_t id;
	session_t session;
	struct skynet_message *msg;
};

struct skynet_mq;
struct skynet_mq_fixed;	// support multi write

struct skynet_mq * skynet_mq_create();
void skynet_mq_release(struct skynet_mq *);
void skynet_mq_pushst(struct skynet_mq *mq, struct skynet_message_package *pack);	// use in single thread
int skynet_mq_headst(struct skynet_mq *mq, struct skynet_message_package *pack);	// return 0 when mq is empty
void skynet_mq_popst(struct skynet_mq *mq);


// push/pop can be in different threads
void skynet_mq_pushmt(struct skynet_mq *mq, struct skynet_message_package *pack);	
int skynet_mq_popmt(struct skynet_mq *mq, struct skynet_message_package *pack); 	// return 0 when mq is empty

struct skynet_mq_fixed * skynet_mq_createfixed();	// fixed mq can be multi write in different threads
void skynet_mq_releasefixed(struct skynet_mq_fixed *);
int skynet_mq_pushfixed(struct skynet_mq_fixed *, struct skynet_message_package *pack);	// return 0 when mq is busy
int skynet_mq_popfixed(struct skynet_mq_fixed *, struct skynet_message_package *pack);	// return 0 when mq is empty

#endif
