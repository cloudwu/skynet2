#include "skynet.h"
#include "simplelock.h"
#include "skynet_malloc.h"
#include <assert.h>
#include <string.h>

#define MESSAGE_SKYNET 1
#define MESSAGE_USER 2

struct skynet_message {
	int type;
	int ref;
	size_t sz;
};

struct skynet_message_user {
	struct skynet_message header;	// header must be the first field
	void * buffer;
	void (*userfree)(void *);
};

struct skynet_message *
skynet_message_init(void *buffer, size_t sz, void (*userfree)(void *)) {
	if (userfree) {
		struct skynet_message_user * msg = skynet_malloc(sizeof(struct skynet_message_user));
		assert(buffer);
		msg->header.type = MESSAGE_USER;
		msg->header.ref = 1;
		msg->header.sz = sz;
		msg->buffer = buffer;
		msg->userfree = userfree;
		return (struct skynet_message *)msg;
	} else {
		struct skynet_message *msg = skynet_malloc(sizeof(struct skynet_message) + sz);
		msg->type = MESSAGE_SKYNET;
		msg->ref = 1;
		msg->sz = sz;
		if (buffer) {
			memcpy(msg+1, buffer, sz);
		}
		return msg;
	}
}

void
skynet_message_grab(struct skynet_message *msg) {
	assert(msg->ref > 0);
	atom_inc(&msg->ref);
}

void
skynet_message_release(struct skynet_message *msg) {
	if (msg == NULL)
		return;
	if (atom_dec(&msg->ref) == 0) {
		if (msg->type == MESSAGE_USER) {
			struct skynet_message_user *umsg = (struct skynet_message_user *)msg;
			umsg->userfree(umsg->buffer);
		} else {
			assert(msg->type == MESSAGE_SKYNET);
		}
		skynet_free(msg);
	}
}

void *
skynet_message_buffer(struct skynet_message *msg, size_t *sz) {
	if (sz) {
		*sz = msg->sz;
	}
	if (msg->type == MESSAGE_USER) {
		struct skynet_message_user *umsg = (struct skynet_message_user *)msg;
		return umsg->buffer;
	}
	assert(msg->type == MESSAGE_SKYNET);
	return msg+1;
}
