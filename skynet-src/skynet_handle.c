#include "skynet_handle.h"
#include "skynet_malloc.h"
#include "skynet_service.h"
#include "simplelock.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

// 128K
#define MAX_SERVICE 0x20000	

struct service_node {
	address_t id;
	int ref;
	int exit;
	struct skynet_service *svc;
};

struct all_service {
	struct skynet_service * launcher;
	address_t last_id;
	struct service_node s[MAX_SERVICE];
};

struct all_service *G = NULL;

void
skynet_handle_init() {
	G = skynet_malloc(sizeof(*G));
	memset(G, 0, sizeof(*G));
}

void
skynet_handle_exit() {
	// todo : delete all the service
	skynet_free(G);
}

struct skynet_service *
skynet_service_grab(address_t id) {
	if (id == 0)
		return NULL;
	struct service_node *sn = &G->s[id % MAX_SERVICE];
	if (sn->exit || sn->id != id)
		return NULL;
	atom_inc(&sn->ref);
	if (sn->exit) {
		// double check, service may exit after inc ref
		// .ref never inc after .exit set
		atom_dec(&sn->ref);
		return NULL;
	}
	// the ref is great than 1, so .svc is safe now
	if (skynet_service_id(sn->svc) != id) {
		atom_dec(&sn->ref);
		return NULL;
	}
	return sn->svc;
}

void
skynet_service_release(address_t id) {
	if (id == 0)
		return;
	struct service_node *sn = &G->s[id % MAX_SERVICE];
	assert(sn->id == id);
	assert(skynet_service_id(sn->svc) == id);
	atom_dec(&sn->ref);
}

// new and delete must call in one thread

static address_t
alloc_id() {
	int i;
	for (i=0;i<MAX_SERVICE;i++) {
		address_t id = ++G->last_id;
		if (id == 0) {
			id = ++G->last_id;
		}
		struct service_node *sn = &G->s[id % MAX_SERVICE];
		if (sn->id == 0)
			return id;
		if (sn->exit && sn->ref == 0) {
			sn->id = 0;
			atom_sync();
			sn->svc = NULL;
			skynet_service_destory(sn->svc);
			return id;
		}
	}
	return 0;
}

static inline void
assert_launcher(struct skynet_service *self) {
	if (G->launcher != self) {
		if (!atom_cas_pointer(&G->launcher, NULL, self)) {
			fprintf(stderr, "service create/delete must call in the same service\n");
			abort();
		}
	}
}

address_t
skynet_service_create(struct skynet_service *self) {
	assert_launcher(self);
	address_t id = alloc_id();
	if (id == 0)
		return 0;
	struct service_node *sn = &G->s[id % MAX_SERVICE];
	sn->svc = skynet_service_new(id);
	sn->exit = 0;
	sn->id = id;
	assert(sn->ref == 0);
	return id;
}

void
skynet_service_delete(struct skynet_service *self, address_t id) {
	assert_launcher(self);
	struct skynet_service *svc = skynet_service_grab(id);
	if (svc == NULL)
		return;
	struct service_node *sn = &G->s[id % MAX_SERVICE];
	assert(sn->id == id);
	assert(skynet_service_id(sn->svc) == id);
	sn->exit = 1;
	
	atom_sync();

	if (sn->ref == 0) {
		sn->id = 0;
		atom_sync();
		sn->svc = NULL;
		skynet_service_destory(svc);
	} else {
		// Only close service, the slot will reuse after .ref == 0 when call skynet_service_new
		skynet_service_close(svc);
	}
}
