#ifndef socket_select_h
#define socket_select_h

// We don't sugguest use select mode in production.
// It just works, only for someone want to try it on windows,
// And the max fd is limited. (by FD_SETSIZE)
// We have no plan to improve the performance.

struct socket_trigger {
	int fd;
	bool write;
	void *ud;
};

struct socket_poll {
	int n;
	int event_trigger;	// use it to trigger event
	int event_poll;	// use it in select
	struct socket_trigger trigger[FD_SETSIZE];
	fd_set rd;
	fd_set wt;
};

typedef struct socket_poll SOCKET_POLL[1];

static int
sp_create(struct socket_poll *sp) {
	sp->n = 0;
	int port;
	int fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		return 1;
	}
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;  
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  

	for (port=10000;port<20000;port++) {	// try an available port to create the event
		addr.sin_port = htons(port);  
		if (bind(fd, (struct sockaddr *)&addr,sizeof(addr))<0) {
			continue;
		}
		if (listen(fd, 1) <0) {
			continue;
		}
		int trigger = socket(PF_INET, SOCK_STREAM, 0);
		if (trigger < 0)
			break;
		fcntl(trigger, F_SETFL, O_NONBLOCK);
		connect(trigger, (struct sockaddr *)&addr,sizeof(addr));
		sp->event_poll = accept(fd, NULL, NULL);
		if (sp->event_poll < 0) {
			close(trigger);
			break;
		}
		fd_set wt;
		FD_ZERO(&wt);
		FD_SET(trigger, &wt);
		if (select(trigger+1, NULL, &wt, NULL, NULL) <=0) {
			close(trigger);
			close(sp->event_poll);
			break;
		}
		close(fd);
		sp->event_trigger = trigger;
		return 0;
	}
	close(fd);
	// failed
	return 1;
}

static void
sp_release(struct socket_poll *sp) {
	(void)sp;
}

static int
sp_add(struct socket_poll *sp, int sock, void *ud) {
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
	if (sp->n >= FD_SETSIZE-1)
		return 1;
#else
	if (sock >= FD_SETSIZE)
		return 1;
#endif
	if (sock < 0)
		return 1;
	int i;
	for (i=0;i<sp->n;i++) {
		struct socket_trigger *trigger = &sp->trigger[i];
		if (trigger->fd == sock) {
			trigger->ud = ud;
			trigger->write = false;
			return 0;
		}
	}
	struct socket_trigger *trigger = &sp->trigger[sp->n++];
	trigger->fd = sock;
	trigger->ud = ud;
	trigger->write = false;
	return 0;
}

static void
sp_del(struct socket_poll *sp, int sock) {
	int i;
	for (i=0;i<sp->n;i++) {
		struct socket_trigger *trigger = &sp->trigger[i];
		if (trigger->fd == sock) {
			--sp->n;
			while(i < sp->n) {
				sp->trigger[i] = sp->trigger[i+1];
				++i;
			}
			return;
		}
	}
	// error ?
}

static void
sp_write(struct socket_poll *sp, int sock, void *ud, bool enable) {
	int i;
	for (i=0;i<sp->n;i++) {
		struct socket_trigger *trigger = &sp->trigger[i];
		if (trigger->fd == sock) {
			trigger->fd = sock;
			trigger->ud = ud;
			trigger->write = enable;
			return;
		}
	}
	// error ?
}

static void
sp_trigger(struct socket_poll *sp) {
	char dummy[1];
	// Only try to wakeup the sp_wait, so we can ignore any failure
	send(sp->event_trigger, dummy, 1, 0);
}

static int
sp_wait(struct socket_poll *sp, struct poll_event *e, int max) {
	int i;
	int maxfd = 0;
	FD_ZERO(&sp->rd);
	FD_ZERO(&sp->wt);
	FD_SET(sp->event_poll,&sp->rd);
	for (i=0;i<sp->n;i++) {
		struct socket_trigger *trigger = &sp->trigger[i];
		if (trigger->fd > maxfd)
			maxfd = trigger->fd;

		FD_SET(trigger->fd, &sp->rd);
		if (trigger->write) {
			FD_SET(trigger->fd, &sp->wt);
		}
	}
	int n = select(maxfd+1, &sp->rd, &sp->wt, NULL, NULL);
	if (n <= 0) {
		return n;
	}
	int retn = 0;
	if (FD_ISSET(sp->event_poll,&sp->rd)) {
		for (;;) {
			// drop all the trigger event, ignore any error
			char buffer[128];
			if (recv(sp->event_poll, buffer, sizeof(buffer), 0) < sizeof(buffer)) {
				break;
			}
		}
		--n;
	}
	for (i=0;i<sp->n && max > 0 && n>0;i++) {
		struct socket_trigger *trigger = &sp->trigger[i];
		int fd = trigger->fd;
		bool set = false;
		if (FD_ISSET(fd, &sp->rd)) {
			e[retn].s = trigger->ud;
			e[retn].read = true;
			set = true;
		}
		if (trigger->write && FD_ISSET(fd, &sp->wt)) {
			e[retn].s = trigger->ud;
			e[retn].write = true;
			set = true;
		}
		if (set) {
			++retn;
			--max;
			--n;
		}
	}
	return retn;
}

#endif
