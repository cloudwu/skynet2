#ifndef skynet_handle_h
#define skynet_handle_h

#include "skynet.h"

void skynet_handle_init();	// global init
void skynet_handle_exit();	// global exit

// thread safe
struct skynet_service * skynet_service_grab(address_t id);
void skynet_service_release(address_t id);

// must call in one thread (The self must the unique one, or NULL at start)
address_t skynet_service_create(struct skynet_service *self);
void skynet_service_delete(struct skynet_service *self, address_t id);

#endif
