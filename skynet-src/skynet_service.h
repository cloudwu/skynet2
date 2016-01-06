#ifndef skynet_service_h
#define skynet_service_h

#include "skynet.h"
#include "skynet_mq.h"

address_t skynet_service_id(struct skynet_service *svc);
struct skynet_service * skynet_service_new(address_t id);
void skynet_service_destory(struct skynet_service *svc);
void skynet_service_close(struct skynet_service *svc);

// return 1 when succ, 0 means busy
int skynet_service_postmessage(struct skynet_service *self, struct skynet_service *svc, struct skynet_message_package *pack);
void skynet_service_queuemessage(struct skynet_service *self, struct skynet_message_package *pack);

session_t skynet_service_newrequest(struct skynet_service *self);
void skynet_service_closeresponse(struct skynet_service *self, session_t session, struct skynet_message_package *pack);

// return 1 when succ, 0 means busy or empty
int skynet_service_dispatch(struct skynet_service *self);

#endif
