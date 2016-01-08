#include "skynet_socket.h"
#include "simplepoll.h"
#include "simplelock.h"
#include "skynet_mq.h"
#include "skynet_malloc.h"
#include <assert.h>
#include <string.h>

// MAX_SOCKET will be 2^MAX_SOCKET_P
// Notice: If you use select(windows), another limit is FD_SETSIZE
#define MAX_SOCKET_P 16
#define MAX_EVENT 64
#define MIN_READ_BUFFER 64
#define SOCKET_TYPE_INVALID 0
#define SOCKET_TYPE_RESERVE 1
#define SOCKET_TYPE_LISTEN 2
#define SOCKET_TYPE_HALFCLOSE 3
#define SOCKET_TYPE_CONNECTING 4
#define SOCKET_TYPE_CONNECTED 5

#define MAX_SOCKET (1<<MAX_SOCKET_P)

#define PRIORITY_HIGH 0
#define PRIORITY_LOW 1

#define HASH_ID(id) (((unsigned)id) % MAX_SOCKET)

#define PROTOCOL_TCP 0
#define PROTOCOL_UDP 1
#define PROTOCOL_UDPv6 2
/*
#define UDP_ADDRESS_SIZE 19	// ipv6 128bit + port 16bit + 1 byte type

#define MAX_UDP_PACKAGE 65535
*/

struct write_buffer {
	struct write_buffer * next;
	struct skynet_message * msg;
	size_t offset;
};

struct wb_list {
	struct write_buffer * head;
	struct write_buffer * tail;
};

struct socket {
	struct wb_list high;
	struct wb_list low;
	size_t wb_size;
	socket_t id;
	int fd;
	short protocol;
	short type;
	int read_size;
};

struct skynet_socket_server {
	socket_t alloc_id;
	int event_n;
	int event_index;
	int event_trigger;
	struct skynet_mq *request;
	SOCKET_POLL poll;
	struct poll_event ev[MAX_EVENT];
	struct socket slot[MAX_SOCKET];
};

union sockaddr_all {
	struct sockaddr s;
	struct sockaddr_in v4;
	struct sockaddr_in6 v6;
};

static inline void
clear_wb_list(struct wb_list *list) {
	list->head = NULL;
	list->tail = NULL;
}

struct skynet_socket_server *
sss_create() {
	struct skynet_socket_server *sss = skynet_malloc(sizeof(*sss));
	if (sp_create(sss->poll)) {
		// failed
		skynet_free(sss);
		return NULL;
	}
	sss->request = skynet_mq_create();
	sss->event_trigger = 0;
	sss->alloc_id = 0;
	sss->event_n = 0;
	sss->event_index = 0;

	int i;
	for (i=0;i<MAX_SOCKET;i++) {
		struct socket *s = &sss->slot[i];
		s->type = SOCKET_TYPE_INVALID;
		clear_wb_list(&s->high);
		clear_wb_list(&s->low);
	}

	simplesocket_init();

	return sss;
}

static void
free_wb_list(struct wb_list *list) {
	struct write_buffer *wb = list->head;
	while (wb) {
		struct write_buffer *tmp = wb;
		wb = wb->next;
		skynet_message_release(tmp->msg);
		skynet_free(tmp);
	}
	list->head = NULL;
	list->tail = NULL;
}

static inline void
check_wb_list(struct wb_list *s) {
	assert(s->head == NULL);
	assert(s->tail == NULL);
}

static void
make_socket_message(struct skynet_message_package *pack,
	int type, int fd1, int fd2, struct skynet_message *msg) {
	pack->type = type;
	SOCKET_REQ_A1(pack, fd1);
	SOCKET_REQ_A2(pack, fd2);
	pack->msg = msg;
}

static void
force_close(struct skynet_socket_server *sss, struct socket *s, struct skynet_message_package *result) {
	make_socket_message(result,SOCKET_MSG_CLOSE, s->id, 0, NULL);
	if (s->type == SOCKET_TYPE_INVALID) {
		return;
	}
	assert(s->type != SOCKET_TYPE_RESERVE);
	free_wb_list(&s->high);
	free_wb_list(&s->low);

	sp_del(sss->poll, s->fd);
	close(s->fd);	// check close succ ?
	s->type = SOCKET_TYPE_INVALID;
}

void 
sss_release(struct skynet_socket_server *sss) {
	int i;
	struct skynet_message_package dummy;
	for (i=0;i<MAX_SOCKET;i++) {
		struct socket *s = &sss->slot[i];
		if (s->type != SOCKET_TYPE_RESERVE) {
			force_close(sss, s , &dummy);
		}
	}
	skynet_mq_release(sss->request);
	sp_release(sss->poll);
	skynet_free(sss);
}

static int
reserve_id(struct skynet_socket_server *sss) {
	int i;
	for (i=0;i<MAX_SOCKET;i++) {
		socket_t id = atom_inc(&(sss->alloc_id));
		if (id == 0) {
			id = atom_inc(&(sss->alloc_id));
		}
		struct socket *s = &sss->slot[HASH_ID(id)];
		if (s->type == SOCKET_TYPE_INVALID) {
			if (atom_cas_long(&s->type, SOCKET_TYPE_INVALID, SOCKET_TYPE_RESERVE)) {
				s->id = id;
				s->fd = -1;
				return id;
			} else {
				// retry
				--i;
			}
		}
	}
	return 0;
}

static struct socket *
new_fd(struct skynet_socket_server *ss, int id, int fd) {
	struct socket * s = &ss->slot[HASH_ID(id)];
	assert(s->type == SOCKET_TYPE_RESERVE);

	if (sp_add(ss->poll, fd, s)) {
		s->type = SOCKET_TYPE_INVALID;
		return NULL;
	}

	s->id = id;
	s->fd = fd;
	s->read_size = MIN_READ_BUFFER;
	check_wb_list(&s->high);
	check_wb_list(&s->low);
	return s;
}

socket_t
sss_request(struct skynet_socket_server *sss, struct skynet_message_package *req) {
	int id = 0;
	switch (req->type) {
	int port;
	int backlog;
	case SOCKET_REQ_LISTEN:
	case SOCKET_REQ_CONNECT:
		id = reserve_id(sss);
		if (id == 0) {
			return 0;
		}
		
		port = SOCKET_REQ_P1(req);
		backlog = SOCKET_REQ_P2(req);	// connect should be 0
		if (backlog > 0xffff)
			backlog = 0xffff;
	
		SOCKET_REQ_A1(req, id);
		SOCKET_REQ_A2(req, backlog << 16 | port);
		// go though
	case SOCKET_REQ_EXIT:
	case SOCKET_REQ_CLOSE:
	case SOCKET_REQ_SEND:
		skynet_mq_pushmt(sss->request, req);
		if (atom_cas_long(&sss->event_trigger, 0, 1)) {
			sp_trigger(sss->poll);
		}
		return id;
	default:
		return 0;
	}
}

static void
socket_keepalive(int fd) {
	int keepalive = 1;
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive , sizeof(keepalive));  
}

static int
message_to_sockaddr(struct skynet_message *msg, int port, union sockaddr_all *sa, socklen_t *retsz) {
	if (msg == NULL)
		return -1;
	size_t sz = 0;
	void * buf = skynet_message_buffer(msg, &sz);
	if (buf == NULL) {
		skynet_message_release(msg);
		return -1;
	}
	int family;
	void *ptr;
	if (memchr(buf, ':', sz)) {
		// ipv6
		family = AF_INET6;
		*retsz = sizeof(sa->v6);
		ptr = &sa->v6.sin6_addr;
		sa->v6.sin6_port = htons(port);
	} else {
		// ipv4
		family = AF_INET;
		*retsz = sizeof(sa->v4);
		ptr = &sa->v4.sin_addr;
		sa->v4.sin_port = htons(port);
	}
	if (inet_pton(family, buf, ptr) != 1) {
		skynet_message_release(msg);
		return -1;
	}
	sa->s.sa_family = family;

	return family;
}

#define MAKE_LITERAL(s) ""s, sizeof(s)

static void
make_error_result(struct skynet_message_package *result, socket_t id, const char *err, size_t sz) {
	struct skynet_message *msg = skynet_message_init((void *)err, sz, NULL);
	make_socket_message(result, SOCKET_MSG_ERROR, id, 0, msg);
}

static int
perpare_fd(struct skynet_message_package *req, union sockaddr_all *sa, socklen_t *ssz, struct skynet_message_package *result) {
	socket_t id = SOCKET_REQ_P1(req);
	unsigned long p2 = SOCKET_REQ_P2(req);
	int port = p2 & 0xffff;
	int af = message_to_sockaddr(req->msg, port, sa, ssz);
	if (af < 0) {
		make_error_result(result, id, MAKE_LITERAL("Invalid address"));
		return -1;
	}
	int fd = socket(af, SOCK_STREAM, 0);
	if (fd < 0) {
		make_error_result(result, id, MAKE_LITERAL("Can't create socket"));
		return -1;
	}

	return fd;
}

static void
do_listen(struct skynet_socket_server *sss, struct skynet_message_package *req, struct skynet_message_package *result) {
	socket_t id = SOCKET_REQ_P1(req);
	unsigned long p2 = SOCKET_REQ_P2(req);
	int backlog = p2 >> 16;
	union sockaddr_all sa;
	socklen_t ssz;
	int fd = perpare_fd(req, &sa, &ssz, result);
	if (fd < 0) {
		goto error;
	}
	int reuse = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(int)) == -1) {
		close(fd);
		make_error_result(result, id, MAKE_LITERAL("Can't set reuse"));
		goto error;
	}
	if (bind(fd, &sa.s, ssz) == -1) {
		close(fd);
		make_error_result(result, id, MAKE_LITERAL("Can't bind"));
		goto error;
	}
	if (listen(fd, backlog) == -1) {
		close(fd);
		make_error_result(result, id, MAKE_LITERAL("Can't listen"));
		goto error;
	}
	struct socket * s = new_fd(sss,id,fd);
	if (s) {
		s->fd = fd;
		s->type = SOCKET_TYPE_LISTEN;
		make_socket_message(result, SOCKET_MSG_OPEN, id, 0, NULL);
	} else {
		make_error_result(result, id, MAKE_LITERAL("Can't poll listen"));
	}
	return;
error:
	s->type = SOCKET_TYPE_INVALID;
}

static int
do_connect(struct skynet_socket_server *sss, struct skynet_message_package *req, struct skynet_message_package *result) {
	socket_t id = SOCKET_REQ_P1(req);
	union sockaddr_all sa;
	socklen_t ssz;
	struct socket * s = &sss->slot[HASH_ID(id)];
	s->type = SOCKET_TYPE_INVALID;
	int fd = perpare_fd(req, &sa, &ssz, result);
	if (fd < 0) {
		goto error;
	}
	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
		close(fd);
		make_error_result(result, id, MAKE_LITERAL("Can't set nonblock"));
		goto error;
	}

	socket_keepalive(fd);
	int status = connect( fd, &sa.s, ssz);
	if (status != 0 && errno != SOCK_EINPROGRESS) {
		close(fd);
		make_error_result(result, id, MAKE_LITERAL("Connect error"));
		goto error;
	}

	s->fd = fd;

	if(status == 0) {
		s->type = SOCKET_TYPE_CONNECTED;
		make_socket_message(result, SOCKET_MSG_OPEN, id, 0, NULL);
		return 1;
	} else {
		s->type = SOCKET_TYPE_CONNECTING;
		return 0;
	}
error:
	s->type = SOCKET_TYPE_INVALID;
	return 1;
}

static struct write_buffer *
append_sendbuffer_(struct wb_list *s, struct skynet_message *msg, size_t offset) {
	struct write_buffer * buf = skynet_malloc(sizeof(*buf));
	buf->offset = offset;
	buf->msg = msg;
	buf->next = NULL;
	if (s->head == NULL) {
		s->head = s->tail = buf;
	} else {
		assert(s->tail != NULL);
		assert(s->tail->next == NULL);
		s->tail->next = buf;
		s->tail = buf;
	}
	return buf;
}

static inline void
append_sendbuffer(struct skynet_socket_server *sss, struct socket *s,struct skynet_message *msg, size_t offset) {
	append_sendbuffer_(&s->high, msg, offset);
}

static inline void
append_sendbuffer_low(struct skynet_socket_server *sss, struct socket *s,struct skynet_message *msg) {
	append_sendbuffer_(&s->low, msg, 0);
}

static inline int
send_buffer_empty(struct socket *s) {
	return (s->high.head == NULL && s->low.head == NULL);
}

static inline int
list_uncomplete(struct wb_list *s) {
	struct write_buffer *wb = s->head;
	if (wb == NULL)
		return 0;
	
	return wb->offset != 0;
}

static void
raise_uncomplete(struct socket * s) {
	struct wb_list *low = &s->low;
	struct write_buffer *tmp = low->head;
	low->head = tmp->next;
	if (low->head == NULL) {
		low->tail = NULL;
	}

	// move head of low list (tmp) to the empty high list
	struct wb_list *high = &s->high;
	assert(high->head == NULL);

	tmp->next = NULL;
	high->head = high->tail = tmp;
}

// return -1 error ; 0 blocked ; 1 send all
static int
send_buffer(int fd, struct skynet_message *msg, size_t *offset) {
	size_t sz;
	void * ptr = skynet_message_buffer(msg, &sz) + *offset;
	size_t need = sz - *offset;
	ssize_t n = send(fd, ptr, need, 0);
	if (n < 0) {
		if (errno == EINTR || errno == SOCK_EAGAIN) {
			n = 0;
		} else {
			return -1;
		}
	}
	*offset += n;
	if (n == need) {
		return 1;
	} else {
		return 0;
	}
}

// return 1 means send all, -1 means socket error and closed
static int
send_list(struct skynet_socket_server *sss, struct socket *s, struct wb_list *list, struct skynet_message_package *result) {
	while (list->head) {
		struct write_buffer * tmp = list->head;
		int n = send_buffer(s->fd, tmp->msg, &tmp->offset);
		if (n < 0) {
			// error
			force_close(sss,s,result);
			return -1;
		}
		if (n == 0) {
			// blocked
			return 0;
		}
		list->head = tmp->next;
		skynet_free(tmp);
	}
	list->tail = NULL;

	return 1;
}

/*
	Each socket has two write buffer list, high priority and low priority.

	1. send high list as far as possible.
	2. If high list is empty, try to send low list.
	3. If low list head is uncomplete (send a part before), move the head of low list to empty high list (call raise_uncomplete) .
	4. If two lists are both empty, turn off the event. (call check_close)

	return 1 means socket closed
 */
static int
send_all(struct skynet_socket_server *sss, struct socket *s, struct skynet_message_package *result) {
	assert(!list_uncomplete(&s->low));
	// step 1
	int status = send_list(sss,s,&s->high,result);
	if (status <= 0) {
		if (status < 0) {
			return 1;
		} else {
			// send blocked
			return 0;
		}
	}
	if (s->high.head == NULL) {
		// step 2
		if (s->low.head != NULL) {
			if (send_list(sss,s,&s->low,result) < 0) {
				return 1;
			}
			// step 3
			if (list_uncomplete(&s->low)) {
				raise_uncomplete(s);
			}
		} else {
			// step 4
			if (s->type == SOCKET_TYPE_HALFCLOSE) {
				force_close(sss, s, result);
				return 1;
			} else {
				sp_write(sss->poll, s->fd, s, false);
			}
		}
	}

	return 0;
}

/*
	When send a package , we can assign the priority : PRIORITY_HIGH or PRIORITY_LOW

	If socket buffer is empty, write to fd directly.
		If write a part, append the rest part to high list. (Even priority is PRIORITY_LOW)
	Else append package to high (PRIORITY_HIGH) or low (PRIORITY_LOW) list.
 */
static int
do_send(struct skynet_socket_server *sss, struct skynet_message_package *req, struct skynet_message_package *result) {
	socket_t id = SOCKET_REQ_P1(req);
	int low = SOCKET_REQ_P2(req);
	struct socket * s = &sss->slot[HASH_ID(id)];
	if (s->type != SOCKET_TYPE_CONNECTED || s->id != id ) {
		skynet_message_release(req->msg);
		make_error_result(result, id, MAKE_LITERAL("Invalid socket"));
		return 1;
	}

	if (send_buffer_empty(s)) {
		size_t n = send_buffer(s->fd, req->msg, &n);
		if (n > 0) {
			// send all
			return 0;
		}
		if (n < 0) {
			// error
			force_close(sss,s,result);
			return 1;
		}
		append_sendbuffer(sss, s, req->msg, n);	// add to high priority list, even priority == PRIORITY_LOW
		sp_write(sss->poll, s->fd, s, true);
	} else {
		// todo: udp
		if (low) {
			append_sendbuffer_low(sss, s, req->msg);
		} else {
			append_sendbuffer(sss, s, req->msg, 0);
		}
	}
	return 0;
}

static int
do_close(struct skynet_socket_server *sss, struct skynet_message_package *req, struct skynet_message_package *result) {
	socket_t id = SOCKET_REQ_P1(req);
	int shutdown = SOCKET_REQ_P2(req);
	struct socket * s = &sss->slot[HASH_ID(id)];
	if (s->type == SOCKET_TYPE_INVALID || s->id != id) {
		// already close
		make_socket_message(result, SOCKET_MSG_CLOSE, id, 0, NULL);
		return 1;
	}
	s->type = SOCKET_TYPE_HALFCLOSE;

	if (!send_buffer_empty(s)) { 
		if (send_all(sss,s,result))
			return 1;
	}
	if (shutdown || send_buffer_empty(s)) {
		force_close(sss,s,result);
		return 1;
	}
	return 0;
}

static int
process_req(struct skynet_socket_server *sss, struct skynet_message_package *result) {
	struct skynet_message_package req;
	while(!skynet_mq_popmt(sss->request, &req)) {
		switch(req.type) {
		case SOCKET_REQ_EXIT:
			make_socket_message(result, SOCKET_MSG_EXIT, 0, 0, NULL);
			return 1;
		case SOCKET_REQ_LISTEN:
			do_listen(sss, &req, result);
			return 1;
		case SOCKET_REQ_CONNECT:
			if (do_connect(sss, &req, result))	// connect may error
				return 1;
			break;
		case SOCKET_REQ_SEND:
			if (do_send(sss, &req, result))	// send may error
				return 1;
			break;
		case SOCKET_REQ_CLOSE:
			if (do_close(sss, &req, result))	// already close
				return 1;
			break;
		default:
			// invalid request
			skynet_message_release(req.msg);
			break;
		}
	}
	return 0;
}

static int
forward_message(struct skynet_socket_server *sss, struct socket *s, struct skynet_message_package *result) {
	int sz = s->read_size;
	struct skynet_message *msg = skynet_message_init(NULL, sz, NULL);
	void * buffer = skynet_message_buffer(msg, NULL);
	ssize_t n = recv(s->fd, buffer, sz, 0);
	if (n<0) {
		skynet_message_release(msg);
		if (errno != EINTR) {
			// error
			force_close(sss,s,result);
			return 0;
		} else {
			return 1;
		}
	}
	if (n==0) {
		skynet_message_release(msg);
		force_close(sss, s, result);
		return 0;
	}

	if (s->type == SOCKET_TYPE_HALFCLOSE) {
		// discard recv data
		skynet_message_release(msg);
		return 1;
	}

	if (n == sz) {
		s->read_size *= 2;
	} else {
		skynet_message_shrink(msg, n);
		if (sz > MIN_READ_BUFFER && n*2 < sz) {
			s->read_size /= 2;
		}
	}

	make_socket_message(result, SOCKET_MSG_DATA, s->id, 0, msg);
	return 0;
}

static void
report_connect(struct skynet_socket_server *sss, struct socket *s, struct skynet_message_package *result) {
	int error;
	socklen_t len = sizeof(error);  
	int code = getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &error, &len);  
	if (code < 0 || error) {  
		int id = s->id;
		force_close(sss,s,result);
		// todo : if (code >= 0) use error, or else errno
		make_error_result(result, id, MAKE_LITERAL("Connect failed"));
	} else {
		s->type = SOCKET_TYPE_CONNECTED;
		make_socket_message(result, SOCKET_MSG_OPEN, s->id, 0, NULL);
	}
}

static int
report_accept(struct skynet_socket_server *sss, struct socket *s, struct skynet_message_package *result) {
	union sockaddr_all u;
	socklen_t len = sizeof(u);
	int client_fd = accept(s->fd, &u.s, &len);
	if (client_fd < 0) {
		if (errno == EMFILE || errno == ENFILE) {
			make_error_result(result, s->id, MAKE_LITERAL("Accept file limit"));
			return 0;
		} else {
			// ignore other error
			return 1;
		}
	}
	int id = reserve_id(sss);
	if (id < 0) {
		close(client_fd);
		make_error_result(result, s->id, MAKE_LITERAL("Accept skynet max socket limit"));
		return 0;
	}
	if (fcntl(client_fd, F_SETFL, O_NONBLOCK) < 0) {
		close(client_fd);
		make_error_result(result, s->id, MAKE_LITERAL("Accept new fd can't set nonblock"));
		return 0;
	}
	socket_keepalive(client_fd);
	struct socket *ns = new_fd(sss, id, client_fd);
	if (ns == NULL) {
		close(client_fd);
		make_error_result(result, s->id, MAKE_LITERAL("Accept new fd can't set in poll"));
		return 0;
	}
	ns->type = SOCKET_TYPE_CONNECTED;

	void * sin_addr = (u.s.sa_family == AF_INET) ? (void*)&u.v4.sin_addr : (void *)&u.v6.sin6_addr;
	int sin_port = ntohs((u.s.sa_family == AF_INET) ? u.v4.sin_port : u.v6.sin6_port);

	struct skynet_message *msg = skynet_message_init(NULL, INET6_ADDRSTRLEN + 8, NULL);
	char * tmp = skynet_message_buffer(msg, NULL);
	if (inet_ntop(u.s.sa_family, sin_addr, tmp, INET6_ADDRSTRLEN)) {
		int n = snprintf(tmp, INET6_ADDRSTRLEN + 8, "%s:%d", tmp, sin_port);
		skynet_message_shrink(msg, n);
	} else {
		skynet_message_release(msg);
		msg = NULL;
	}

	make_socket_message(result, SOCKET_MSG_ACCEPT, s->id, id, msg);

	return 0;
}

int
sss_poll(struct skynet_socket_server *sss, struct skynet_message_package *result) {
again:
	if (process_req(sss,result)) {
		return 1;
	}
	if (sss->event_index == sss->event_n) {
		sss->event_trigger = 1;	// allow sss_request trigger event
		// process again, double check
		if (process_req(sss, result)) {
			return 1;
		}
		sss->event_n = sp_wait(sss->poll, sss->ev, MAX_EVENT);
		sss->event_trigger = 0; // disable trigger event

		sss->event_index = 0;
		if (sss->event_n <= 0) {
			// ignore sp_wait error, try again
			sss->event_n = 0;
			return 0;
		}
	}
	struct poll_event *e = &sss->ev[sss->event_index++];
	struct socket *s = e->s;

	switch (s->type) {
	case SOCKET_TYPE_CONNECTING:
		report_connect(sss, s, result);
		break;
	case SOCKET_TYPE_LISTEN:
		if (report_accept(sss, s, result)) {
			// ignore the error
			goto again;
		}
		break;
	case SOCKET_TYPE_INVALID:
	case SOCKET_TYPE_RESERVE:
		// ignore the invalid socket
		break;
	default:
		if (e->read) {
			if (forward_message(sss, s, result)) {
				// read no data
				goto again;
			}
			int type = result->type;
			if (e->write && type == SOCKET_MSG_DATA) {
				// Try to dispatch write message next step if write flag set.
				// Or else ignore the write event
				e->read = false;
				--sss->event_index;
			}
		} else if (e->write) {
			if (!send_all(sss,s,result)) {
				// send event doesn't need return, so poll again
				goto again;
			}
		}
		break;
	}
	return sss->event_index != sss->event_n;
}
