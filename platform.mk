PLAT ?= none
PLATS = linux freebsd macosx mingw

CC = gcc

.PHONY : none $(PLATS) clean all cleanall

#ifneq ($(PLAT), none)

.PHONY : default

default :
	$(MAKE) $(PLAT)

#endif

none :
	@echo "Please do 'make PLATFORM' where PLATFORM is one of these:"
	@echo "   $(PLATS)"

SKYNET_LIBS = -lpthread -lm
LUA_LIBS =
SHARED = -fPIC --shared

linux : PLAT = linux
macosx : PLAT = macosx
freebsd : PLAT = freebsd
mingw : PLAT = mingw

macosx : SHARED = -fPIC -dynamiclib -Wl,-undefined,dynamic_lookup
macosx : EXPORT =
macosx linux : SKYNET_LIBS += -ldl
linux freebsd : SKYNET_LIBS += -lrt
mingw : LUA_LIBS += -L/usr/local/bin -llua53
mingw : SHARED = --shared

linux macosx freebsd mingw:
	$(MAKE) all PLAT=$@ SKYNET_LIBS="$(SKYNET_LIBS)" SHARED="$(SHARED)" LUA_LIBS="$(LUA_LIBS)"
