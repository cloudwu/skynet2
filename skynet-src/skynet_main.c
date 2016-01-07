#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <string.h>
#include <stdio.h>

#include "skynet_malloc.h"
#include "simplethread.h"

static void *
thread_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
	(void)ud; (void)osize;
	if (nsize == 0) {
		skynet_free(ptr);
		return NULL;
	} else {
		return skynet_realloc(ptr,nsize);
	}
}

struct lua_thread {
	lua_State *L;
};

static int
thread_init(lua_State *L) {
	luaL_openlibs(L);
	lua_State * hL = lua_touserdata(L, 1);
	lua_settop(L,0);
	const char *filename = lua_tostring(hL, 1);
	if (luaL_loadfile(L, filename) != LUA_OK) {
		return lua_error(L);
	}
	int n = lua_gettop(hL) - 1;	// the last value is struct lua_thread
	luaL_checkstack(L, n, NULL);
	int i;
	for (i=2;i<=n;i++) {
		switch(lua_type(hL, i)) {
			size_t sz = 0;
			const char * str;
		case LUA_TSTRING:
			str = lua_tolstring(hL, i, &sz);
			lua_pushlstring(L, str, sz);
			break;
		case LUA_TBOOLEAN:
			lua_pushboolean(L, lua_toboolean(hL,i));
			break;
		case LUA_TNIL:
			lua_pushnil(L);
			break;
		case LUA_TNUMBER:
			if (lua_isinteger(hL, i)) {
				lua_pushinteger(L, lua_tointeger(hL, i));
			} else {
				lua_pushnumber(L, lua_tonumber(hL, i));
			}
			break;
		}
	}

	return lua_gettop(L);
}

static void
check_args(lua_State *L) {
	// the args passed to thread should be number/string/boolean/nil.
	int n = lua_gettop(L);
	int i;
	for (i=2;i<=n;i++) {
		int t = lua_type(L, i);
		switch(t) {
		case LUA_TNUMBER:
		case LUA_TSTRING:
		case LUA_TBOOLEAN:
		case LUA_TNIL:
			break;
		default:
			luaL_error(L, "Unsupport arg type %s", lua_typename(L, t));
		}
	}
}

static int
lthread_create(lua_State *L) {
	luaL_checkstring(L,1);	// the 1st arg is main func filename
	check_args(L);
	struct lua_thread *thr = lua_newuserdata(L, sizeof(*thr));
	thr->L = lua_newstate(thread_alloc, NULL);
	if (thr->L == NULL) {
		return luaL_error(L, "new thread state failed");
	}
	lua_pushcfunction(thr->L, thread_init);
	lua_pushlightuserdata(thr->L, L);
	int err = lua_pcall(thr->L, 1, LUA_MULTRET, 0);
	if (err == LUA_OK) {
		return 1;
	} else {
		size_t sz = 0;
		const char *err = lua_tolstring(thr->L, 1, &sz);
		char tmp[sz];
		memcpy(tmp, err, sz);
		lua_close(thr->L);
		lua_pushlstring(L, tmp, sz);
		return lua_error(L);
	}
}

static void
close_thread(struct lua_thread *thr) {
	lua_close(thr->L);
	thr->L = NULL;
}

static void
close_all_thread(lua_State *L) {
	int n = lua_rawlen(L,1);
	int i;
	for (i=0;i<n;i++) {
		lua_geti(L, 1, i+1);
		struct lua_thread *thr = lua_touserdata(L, -1);
		if (thr) {
			close_thread(thr);
		}
	}
}

static void
thread_func(void *ud) {
	struct lua_thread *thr = ud;
	int n = lua_gettop(thr->L) - 1;
	if (lua_pcall(thr->L, n, 0, 0) != LUA_OK) {
		fprintf(stderr, "thread error : %s\n", lua_tostring(thr->L, -1));
	}
}

static int
lthread_join(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	int n = lua_rawlen(L,1);
	int i;
	// check threads
	for (i=0;i<n;i++) {
		lua_geti(L, 1, i+1);
		struct lua_thread *thr = lua_touserdata(L, -1);
		if (thr == NULL || thr->L == NULL) {
			close_all_thread(L);
			return luaL_error(L, "Index %d is not a thread", i+1);
		}
		lua_pop(L,1);
	}
	struct thread threads[n];
	for (i=0;i<n;i++) {
		lua_geti(L, 1, i+1);
		struct lua_thread *thr = lua_touserdata(L, -1);
		threads[i].func = thread_func;
		threads[i].ud = thr;
		lua_pop(L,1);
	}
	thread_join(threads, n);
	return 0;
}

LUAMOD_API int
luaopen_skynet_framework(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "thread_create", lthread_create },
		{ "thread_join", lthread_join },
		{ NULL, NULL },
	};
	luaL_newlib(L,l);
	return 1;
}
