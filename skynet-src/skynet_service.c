#include "skynet.h"
#include "skynet_service.h"
#include "simplelock.h"
#include "skynet_mq.h"
#include "skynet_malloc.h"
#include "skynet_handle.h"

#include <string.h>
#include <stdio.h>

#include <lua.h>
#include <lauxlib.h>

#define MQ_LENGTH 256
#define RESPONSE_MAP 16
#define SEND_QUEUE 4

struct send_queues {
	int cap;
	int n;
	struct {
		address_t dest;
		struct skynet_mq *q;
	} *q;
};

struct response_map {
	int cap;
	session_t freelist;
	struct {
		session_t req_session;	// or free_next
		address_t req_address;
	} *s;
};

#define FREE_NEXT(s) s.req_session

struct skynet_service {
	spinlock_t lock;
	address_t id;
	session_t request_session;
	lua_State *L;
	struct skynet_mq_fixed *recv;
	struct send_queues send;
	struct response_map sessions;	// alive sessions wait for response
};

address_t
skynet_service_id(struct skynet_service *svc) {
	return svc->id;
}

void
skynet_service_close(struct skynet_service *svc) {
	if (svc->L) {
		lua_close(svc->L);
		svc->L = NULL;
	}
}

static void
report_error(struct response_map *rm) {
	int i;
	for (i=0;i<rm->cap;i++) {
		if (rm->s[i].req_address) {
			// TODO: report error to rm->s[i].req_address, session : rm->s[i].req_session
		}
	}
}

void
skynet_service_destory(struct skynet_service *svc) {
	skynet_service_close(svc);
	skynet_mq_releasefixed(svc->recv);
	int i;
	for (i=0;i<svc->send.cap;i++) {
		struct skynet_mq *mq = svc->send.q[i].q;
		skynet_mq_release(mq);
	}
	report_error(&svc->sessions);
	skynet_free(svc);
}

struct skynet_service *
skynet_service_new(address_t id) {
	struct skynet_service *svc = skynet_malloc(sizeof(*svc));
	memset(svc, 0 , sizeof(*svc));
	spin_lock_init(svc);
	svc->id = id;
	svc->L = luaL_newstate();	// todo: user-defined lua Alloc
	svc->recv = skynet_mq_createfixed();
	return svc;
}

static int
send_all(struct skynet_service *svc, struct skynet_mq *mq) {
	int n = 1;
	struct skynet_message_package pack;
	while (skynet_mq_headst(mq, &pack)) {
		if (!skynet_mq_pushfixed(svc->recv, &pack)) {
			// blocked
			return n;
		}
		++n;
		skynet_mq_popst(mq);
	}
	// mq is empty
	return 0;
}

static void
remove_unsend_mq(struct send_queues *q, int index) {
	int n = --q->n;
	skynet_mq_release(q->q[index].q);
	q->q[index] = q->q[n];
}

int
skynet_service_postmessage(struct skynet_service *self, struct skynet_service *svc, struct skynet_message_package *pack) {
	address_t dest = svc->id;
	struct send_queues *q = &self->send;
	int i;
	for (i=0;i<q->n;i++) {
		if (q->q[i].dest == dest) {
			struct skynet_mq *mq = q->q[i].q;
			if (send_all(svc, mq) == 0) {
				remove_unsend_mq(q, i);
				--i;
			} else {
				// blocked
				return 0;
			}
		}
	}
	return skynet_mq_pushfixed(svc->recv, pack);
}

// must call by self (not thread safe)

static struct skynet_mq *
query_send_queue(struct skynet_service *self, address_t id) {
	int i;
	for (i=0;i<self->send.n;i++) {
		if (self->send.q[i].dest == id) {
			return self->send.q[i].q;
		}
	}
	if (self->send.n == self->send.cap) {
		int cap = 2 * self->send.cap;
		if (cap == 0)
			cap = SEND_QUEUE;
		void *old = self->send.q;
		self->send.q = skynet_malloc(cap * sizeof(*self->send.q));
		memcpy(self->send.q, old, self->send.n * sizeof(*self->send.q));
	}
	int n = self->send.n++;
	self->send.q[n].dest = id;
	struct skynet_mq * mq = skynet_mq_create();
	self->send.q[n].q = mq;
	return mq;
}

void
skynet_service_queuemessage(struct skynet_service *self, struct skynet_message_package *pack) {
	struct skynet_mq * mq = query_send_queue(self, pack->id);
	pack->id = self->id;
	skynet_mq_pushst(mq, pack);
}

session_t
skynet_service_newrequest(struct skynet_service *self) {
	session_t session = ++self->request_session;
	if (session == 0) {
		session = ++self->request_session;
	}
	return session;
}

static session_t
session_alloc(struct response_map *rm, address_t req_address, session_t req_session) {
	if (rm->freelist == 0) {
		int cap = rm->cap * 2;
		if (cap == 0) {
			cap = RESPONSE_MAP;
		}
		rm->s = skynet_realloc(rm->s, cap * sizeof(*rm->s));
		int i;
		for (i=rm->cap;i<cap;i++) {
			FREE_NEXT(rm->s[i]) = i+2;
			rm->s[i].req_address = 0;
		}
		FREE_NEXT(rm->s[cap-1]) = 0;
		rm->freelist = 1;
		rm->cap = cap;
	}
	int index = rm->freelist - 1;
	rm->freelist = FREE_NEXT(rm->s[index]);
	rm->s[index].req_session = req_session;
	rm->s[index].req_address = req_address;
	return index + 1;
}

static void
session_free(struct response_map *rm, session_t session, struct skynet_message_package *pack) {
	if (session == 0 || session > rm->cap) {
		pack->id = 0;
		pack->session = 0;
		return;
	}

	int index = (int)(session - 1);
	pack->id = rm->s[index].req_address;
	pack->session = rm->s[index].req_session;
	if (pack->id == 0) {
		// already free, double response !
		return;
	}
	FREE_NEXT(rm->s[index]) = rm->freelist;
	rm->s[index].req_address = 0;
	rm->freelist = session;
}

void
skynet_service_closeresponse(struct skynet_service *self, session_t session, struct skynet_message_package *pack) {
	session_free(&self->sessions, session, pack);
}

static void
do_request(lua_State *L, session_t session, struct skynet_message *msg) {
}

static void
do_response(lua_State *L, session_t session, struct skynet_message *msg) {
}

static void
do_post(lua_State *L, struct skynet_message *msg) {
}

static void
do_error(lua_State *L, session_t session) {
}

static void
drop_unsend_queue(struct skynet_service *self, struct skynet_mq *mq) {
	struct skynet_message_package pack;
	while (skynet_mq_headst(mq, &pack)) {
		if (pack.type == MESSAGE_TYPE_REQUEST) {
			do_error(self->L, pack.session);
		}
		skynet_mq_popst(mq);
	}
}

static int
distribute_unsend(struct skynet_service *self) {
	struct send_queues * queues = &self->send;
	int i;
	int ret = 0;
	for (i=0;i<queues->n;i++) {
		address_t dest = queues->q[i].dest;
		struct skynet_service * svc = skynet_service_grab(dest);
		struct skynet_mq *mq = queues->q[i].q;
		if (svc == NULL) {
			// dest is dead
			drop_unsend_queue(self, mq);
			remove_unsend_mq(queues, i);
			--i;
		} else {
			int n = send_all(svc, mq);
			if (n == 0) {
				remove_unsend_mq(queues, i);
				--i;
			} 
			if (n > 1) {
				// send something
				ret = 1;
			}
		}
	}
	return ret;
}

int
skynet_service_dispatch(struct skynet_service *self) {
	if (!spin_trylock(self)) {
		// busy
		return 0;
	}
	if (distribute_unsend(self)) {
		// send something unsend before
		spin_unlock(self);
		return 1;
	}

	struct skynet_message_package pack;
	if (!skynet_mq_popfixed(self->recv, &pack)) {
		// mq is empty
		spin_unlock(self);
		return 0;
	}
	switch(pack.type) {
	session_t session;
	case MESSAGE_TYPE_REQUEST:
		if (pack.session) {
			session = session_alloc(&self->sessions, pack.id, pack.session);
			do_request(self->L, session, pack.msg);
		}
		break;
	case MESSAGE_TYPE_RESPONSE:
		do_response(self->L, pack.session, pack.msg);
		break;
	case MESSAGE_TYPE_POST:
		do_post(self->L, pack.msg);
		break;
	case MESSAGE_TYPE_ERROR:
		do_error(self->L, pack.session);
		break;
	}
	skynet_message_release(pack.msg);
	spin_unlock(self);
	return 1;
}

