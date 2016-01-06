include platform.mk

SKYNET_BUILD_PATH ?= build

CFLAGS = -g -O2 -Wall -I$(LUA_INC) $(MYCFLAGS)
# CFLAGS += -DUSE_PTHREAD_LOCK

# lua

LUA_STATICLIB := 3rd/lua/liblua.a
LUA_LIB ?= $(LUA_STATICLIB)
LUA_INC ?= 3rd/lua

$(LUA_STATICLIB) :
	cd 3rd/lua && $(MAKE) ALL=a CC='$(CC) -std=gnu99' $(PLAT)

# skynet

LUA_CLIB = 

SKYNET_SRC = skynet_main.c skynet_message.c skynet_malloc.c skynet_mq.c skynet_service.c skynet_handle.c skynet_api.c

all : \
  $(SKYNET_BUILD_PATH)/skynet
#  $(foreach v, $(SKYNET_BUILD_PATH), $(SKYNET_BUILD_PATH)/$(v).so) 

$(SKYNET_BUILD_PATH)/skynet : $(foreach v, $(SKYNET_SRC), skynet-src/$(v)) $(LUA_LIB)
	$(CC) $(CFLAGS) -o $@ $^ -Iskynet-src $(LDFLAGS) $(EXPORT) $(SKYNET_LIBS) $(SKYNET_DEFINES)

clean :
	rm -f $(SKYNET_BUILD_PATH)/skynet $(SKYNET_BUILD_PATH)/*.so

cleanall: clean
	cd 3rd/lua && $(MAKE) clean
