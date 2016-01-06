#ifndef skynet_malloc_h
#define skynet_malloc_h

#include <stddef.h>

#ifndef SKYNET_MODULE_NAME
#define SKYNET_MODULE_NAME __FILE__
#endif

#define skynet_malloc(sz) skynet_malloc_(sz, SKYNET_MODULE_NAME)
#define skynet_free(sz) skynet_free_(sz, SKYNET_MODULE_NAME)
#define skynet_realloc(ptr, sz) skynet_realloc_(ptr, sz, SKYNET_MODULE_NAME)

void* skynet_malloc_(size_t sz, const char *mname);
void skynet_free_(void *ptr, const char *mname);
void* skynet_realloc_(void *ptr, size_t sz, const char *mname);

#endif
