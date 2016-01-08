#ifndef skynet_h
#define skynet_h

#include <stddef.h>

// 0 is invalid
typedef unsigned long address_t;
typedef unsigned long session_t;

struct skynet_message;
struct skynet_service;

// message api
struct skynet_message * skynet_message_init(void *buffer, size_t sz, void (*userfree)(void *));
void skynet_message_grab(struct skynet_message *msg);
void skynet_message_release(struct skynet_message *msg);
void * skynet_message_buffer(struct skynet_message *msg, size_t *sz);
void skynet_message_shrink(struct skynet_message *msg, size_t sz);

// send message api
void skynet_post(struct skynet_service *self, address_t dest, struct skynet_message *msg);
session_t skynet_request(struct skynet_service *self, address_t dest, struct skynet_message *msg);
void skynet_response(struct skynet_service *self, session_t session, struct skynet_message *msg);
void skynet_error(struct skynet_service *self, session_t session);

#endif
