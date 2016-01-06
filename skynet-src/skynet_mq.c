#include "skynet_mq.h"
#include "skynet_malloc.h"
#include "simplelock.h"

#define MQ_LENGTH 64

struct skynet_mq {
	struct rwlock lock;
	int head;
	int tail;
	int cap;
	struct skynet_message_package *q;
};

struct skynet_mq *
skynet_mq_create() {
	struct skynet_mq *mq = skynet_malloc(sizeof(*mq));
	mq->head = 0;
	mq->tail = 0;
	mq->cap = MQ_LENGTH;
	mq->q = skynet_malloc(mq->cap * sizeof(struct skynet_message_package));
	rwlock_init(&mq->lock);
	return mq;
}

void
skynet_mq_release(struct skynet_mq *mq) {
	int head = mq->head;
	int tail = mq->tail;
	if (tail < head) {
		tail += mq->cap;
	}
	while(head < tail) {
		int index = head;
		if (index >= mq->cap) {
			index -= mq->cap;
		}
		struct skynet_message_package *pack = &mq->q[head];
		if (pack->msg) {
			skynet_message_release(pack->msg);
		}
		++head;
	}

	skynet_free(mq->q);
	rwlock_destory(&mq->lock);
	skynet_free(mq);
}

// return 1 when mq is full
static int
perpare_space(struct skynet_mq *mq, struct skynet_mq *expand) {
	int tail = mq->tail + 1;
	if (tail >= mq->cap) {
		tail -= mq->cap;
	}
	if (tail != mq->head) {
		expand->tail = mq->tail;
		return 0;
	}
	expand->cap = mq->cap * 2;
	expand->q = skynet_malloc(expand->cap * sizeof(struct skynet_message_package));
	int head = mq->head;
	tail = mq->tail;
	if (tail < head) {
		tail += mq->cap;
	}
	expand->tail = tail;
	expand->head = head;
	for (;head<tail;head++) {
		int ptr = head;
		if (ptr >= mq->cap) {
			ptr -= mq->cap;
		}
		expand->q[head] = mq->q[ptr];
	}

	return 1;
}

static void
push_message(struct skynet_mq *mq, struct skynet_message_package *pack) {
	mq->q[mq->tail] = *pack;
	++mq->tail;
	if (mq->tail >= mq->cap) {
		mq->tail -= mq->cap;
	}
}

void
skynet_mq_pushst(struct skynet_mq *mq, struct skynet_message_package *pack) {
	struct skynet_mq expand;
	if (perpare_space(mq, &expand)) {
		skynet_free(mq->q);
		mq->tail = expand.tail;
		mq->cap = expand.cap;
		mq->q = expand.q;
	}
	push_message(mq, pack);
}

void
skynet_mq_pushmt(struct skynet_mq *mq, struct skynet_message_package *pack) {
	struct skynet_mq expand;
	if (perpare_space(mq, &expand)) {
		rwlock_wlock(&mq->lock);
			void * ptr = mq->q;
			if (mq->head < expand.head) {
				// the head changes (some one pop in another thread) between perpare_space and rwlock_wlock
				mq->head += mq->cap;
			} 
			mq->q = expand.q;
			mq->cap = expand.cap;
			mq->tail = expand.tail;
		rwlock_wunlock(&mq->lock);
		skynet_free(ptr);
	}
	push_message(mq, pack);
}

static inline void
pop_message(struct skynet_mq *mq) {
	++mq->head;
	if (mq->head >= mq->cap) {
		mq->head -= mq->cap;
	}
}

void
skynet_mq_popst(struct skynet_mq *mq) {
	if (mq->head == mq->tail) {
		return;
	}
	pop_message(mq);
}

int
skynet_mq_headst(struct skynet_mq *mq, struct skynet_message_package *pack) {
	if (mq->head == mq->tail) {
		return 0;
	}
	*pack = mq->q[mq->head];
	return 1;
}

int
skynet_mq_popmt(struct skynet_mq *mq, struct skynet_message_package *pack) {
	if (mq->head == mq->tail) {
		return 0;
	}
	rwlock_rlock(&mq->lock);
		// only one reader, so use read lock is ok.
		// but we can call skynet_mq_pushmt in another thread
		*pack = mq->q[mq->head];
		pop_message(mq);
	rwlock_rlock(&mq->lock);
	return 1;
}

// multi-thread push, single-thread pop.
struct skynet_mq_fixed {
	spinlock_t lock;	// only lock .tail
	int head;
	int tail;
	struct skynet_message_package q[MQ_LENGTH];
};

struct skynet_mq_fixed *
skynet_mq_createfixed() {
	struct skynet_mq_fixed *mq = skynet_malloc(sizeof(*mq));
	spin_lock_init(mq);
	mq->head = 0;
	mq->tail = 0;
	return mq;
}

void
skynet_mq_releasefixed(struct skynet_mq_fixed *mq) {
	int head = mq->head;
	int tail = mq->tail;
	if (tail < head) {
		tail += MQ_LENGTH;
	}
	while(head < tail) {
		int index = head;
		if (index >= MQ_LENGTH) {
			index -= MQ_LENGTH;
		}
		struct skynet_message_package *pack = &mq->q[head];
		if (pack->msg) {
			skynet_message_release(pack->msg);
		}
		++head;
	}
	spin_lock_destory(mq);
	skynet_free(mq);
}

int
skynet_mq_pushfixed(struct skynet_mq_fixed *mq, struct skynet_message_package *pack) {
	if (!spin_trylock(mq)) {
		return 0;
	}
		int tail = mq->tail;
		++tail;
		if (tail >= MQ_LENGTH) {
			tail -= MQ_LENGTH;
		}
		if (tail == mq->head) {
			// mq is full
			spin_unlock(mq);
			return 0;
		}
		mq->q[mq->tail] = *pack;
		mq->tail = tail;
	spin_unlock(mq);
	return 1;
}

int
skynet_mq_popfixed(struct skynet_mq_fixed *mq, struct skynet_message_package *pack) {
	if (mq->head == mq->tail) {
		// mq is empty
		return 0;
	}
	*pack = mq->q[mq->head];
	++mq->head;
	if (mq->head >= MQ_LENGTH) {
		mq->head -= MQ_LENGTH;
	}
	return 1;
}
