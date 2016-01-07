include platform.mk

SKYNET_BUILD_PATH ?= build

CFLAGS = -g -O2 -Wall -I$(LUA_INC) $(MYCFLAGS)
# CFLAGS += -DUSE_PTHREAD_LOCK

# lua

LUA_INC ?= 3rd/lua

$(LUA_STATICLIB) :
	cd 3rd/lua && $(MAKE) ALL=a CC='$(CC) -std=gnu99' $(PLAT)

# skynet

LUA_CLIB = 

SKYNET_SRC = skynet_main.c skynet_message.c skynet_malloc.c skynet_mq.c skynet_service.c skynet_handle.c skynet_api.c

all : \
  $(SKYNET_BUILD_PATH)/skynet.so
#  $(foreach v, $(SKYNET_BUILD_PATH), $(SKYNET_BUILD_PATH)/$(v).so) 

$(SKYNET_BUILD_PATH)/skynet.so : $(foreach v, $(SKYNET_SRC), skynet-src/$(v))
	$(CC) $(CFLAGS) $(SHARED) -o $@ $^ -Iskynet-src $(LDFLAGS) $(SKYNET_LIBS) $(LUA_LIBS)

clean :
	rm -f $(SKYNET_BUILD_PATH)/*.so

cleanall: clean
	cd 3rd/lua && $(MAKE) clean
