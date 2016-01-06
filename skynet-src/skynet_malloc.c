#include "skynet_malloc.h"
#include "skynet.h"

#include <stdlib.h>
#include <stdio.h>

#define MAGICTAG_ALLOC 0x19790205
#define MAGICTAG_FREE 0x0badf00d

struct cookie {
	size_t sz;
	const char * name;
	unsigned long tag;	// for debug use
	address_t service;
};

static inline void
check_oom(void *buf, const char *mname) {
	if (buf == NULL) {
		fprintf(stderr, "OOM error at %s\n", mname);
		abort();
	}
}

void *
skynet_malloc_(size_t sz, const char *mname) {
	struct cookie * buf = malloc(sz + sizeof(struct cookie));
	check_oom(buf, mname);
	buf->sz = sz;
	buf->name = mname;
	buf->tag = MAGICTAG_ALLOC;
	buf->service = 0;

	return buf + 1;
}

static inline struct cookie *
get_cookie(void *ptr) {
	struct cookie * buf = (struct cookie *)ptr;
	return buf - 1;
}

static inline void
check_cookie(struct cookie *c, const char *mname) {
	if (c->tag != MAGICTAG_ALLOC) {
		if (c->tag == MAGICTAG_FREE) {
			fprintf(stderr, "double free the memory alloc by %s at %s\n", c->name, mname);
		} else {
			fprintf(stderr, "free invalid memory at %s\n", mname);
		}
		abort();
	}
}

void
skynet_free_(void *ptr, const char *mname) {
	if (ptr == NULL)
		return;
	struct cookie * c = get_cookie(ptr);
	check_cookie(c, mname);
	c->tag = MAGICTAG_FREE;
	free(c);
}

void*
skynet_realloc_(void *ptr, size_t sz, const char *mname) {
	if (ptr == NULL) {
		return skynet_malloc_(sz, mname);
	}
	struct cookie * c = get_cookie(ptr);
	check_cookie(c, mname);
	c->tag = MAGICTAG_FREE;
	struct cookie *buf = realloc(c, sz + sizeof(struct cookie));
	check_oom(buf, mname);
	buf->sz = sz;
	buf->name = mname;
	buf->tag = MAGICTAG_ALLOC;
	buf->service = 0;
	
	return buf+1;
}
