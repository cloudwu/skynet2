#include "skynet.h"
#include "skynet_service.h"
#include "skynet_handle.h"
#include "skynet_mq.h"

static int
post_message(struct skynet_service *self, struct skynet_message_package *pack) {
	struct skynet_service * svc = skynet_service_grab(pack->id);
	if (svc == NULL) {
		return 0;
	}
	if (skynet_service_postmessage(self, svc, pack)) {
		skynet_service_release(pack->id);
		return 0;
	}
	skynet_service_queuemessage(self, pack);
	skynet_service_release(pack->id);
	return 1;
}

void
skynet_post(struct skynet_service *self, address_t dest, struct skynet_message *msg) {
	struct skynet_message_package pack;
	pack.type = MESSAGE_TYPE_POST;
	pack.id = dest;
	pack.session = 0;
	pack.msg = msg;
	if (!post_message(self, &pack)) {
		skynet_message_release(msg);
	}
}

session_t
skynet_request(struct skynet_service *self, address_t dest, struct skynet_message *msg) {
	struct skynet_message_package pack;
	pack.type = MESSAGE_TYPE_REQUEST;
	pack.id = dest;
	pack.session = skynet_service_newrequest(self);
	pack.msg = msg;
	if (!post_message(self, &pack)) {
		skynet_message_release(msg);
		pack.type = MESSAGE_TYPE_ERROR;
		pack.id = skynet_service_id(self);
		pack.msg = NULL;
		post_message(self, &pack);	// send to self always succ
	}
	return pack.session;
}

void
skynet_response(struct skynet_service *self, session_t session, struct skynet_message *msg) {
	struct skynet_message_package pack;
	pack.type = MESSAGE_TYPE_RESPONSE;
	pack.msg = msg;
	skynet_service_closeresponse(self, session, &pack);
	if (pack.id == 0 || !post_message(self, &pack)) {
		skynet_message_release(msg);
	}
}

void
skynet_error(struct skynet_service *self, session_t session) {
	struct skynet_message_package pack;
	pack.type = MESSAGE_TYPE_ERROR;
	pack.msg = NULL;
	skynet_service_closeresponse(self, session, &pack);
	if (pack.id) {
		post_message(self, &pack);
	}
}
