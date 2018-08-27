/*
** $Id: lstate.c,v 2.133 2015/11/13 12:16:51 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#define lstate_cpp
#define LUA_CORE

#include "uvm/lprefix.h"


#include <stddef.h>
#include <string.h>
#include <cstdint>

#include "uvm/lua.h"

#include "uvm/lapi.h"
#include "uvm/ldebug.h"
#include "uvm/ldo.h"
#include "uvm/lfunc.h"
#include "uvm/lgc.h"
#include "uvm/llex.h"
#include "uvm/lmem.h"
#include "uvm/lstate.h"
#include "uvm/lstring.h"
#include "uvm/ltable.h"
#include "uvm/ltm.h"
#include "uvm/uvm_api.h"
#include "uvm/uvm_lib.h"


#if !defined(LUAI_GCPAUSE)
#define LUAI_GCPAUSE	200  /* 200% */
#endif

#if !defined(LUAI_GCMUL)
#define LUAI_GCMUL	200 /* GC runs 'twice the speed' of memory allocation */
#endif


/*
** a macro to help the creation of a unique random seed when a state is
** created; the seed is used to randomize hashes.
*/
#if !defined(luai_makeseed)
#include <time.h>
#define luai_makeseed()		lua_cast(unsigned int, time(nullptr))
#endif



/*
** thread state + extra space
*/
typedef struct LX {
    lu_byte extra_[LUA_EXTRASPACE];
    lua_State l;
} LX;


/*
** Main thread combines a thread state and the global state
*/
typedef struct LG {
    LX l;
} LG;



#define fromstate(L)	(lua_cast(LX *, lua_cast(lu_byte *, (L)) - offsetof(LX, l)))


/*
** Compute an initial seed as random as possible. Rely on Address Space
** Layout Randomization (if present) to increase randomness..
*/
#define addbuff(b,p,e) \
  { size_t t = lua_cast(size_t, e); \
    memcpy(b + p, &t, sizeof(t)); p += sizeof(t); }

static unsigned int makeseed(lua_State *L) {
    char buff[4 * sizeof(size_t)];
    unsigned int h = luai_makeseed();
    int p = 0;
    addbuff(buff, p, L);  /* heap variable */
    addbuff(buff, p, &h);  /* local variable */
    addbuff(buff, p, luaO_nilobject);  /* global variable */
    addbuff(buff, p, &lua_newstate);  /* public function */
    lua_assert(p == sizeof(buff));
    return luaS_hash(buff, p, h);
}


/*
** set GCdebt to a new value keeping the value (totalbytes + GCdebt)
** invariant (and avoiding underflows in 'totalbytes')
*/
void luaE_setdebt(l_mem debt) {
    
}


CallInfo *luaE_extendCI(lua_State *L) {
    CallInfo *ci = luaM_new(L, CallInfo);
    lua_assert(L->ci->next == nullptr);
    L->ci->next = ci;
    ci->previous = L->ci;
    ci->next = nullptr;
    L->nci++;
    return ci;
}


/*
** free all CallInfo structures not in use by a thread
*/
void luaE_freeCI(lua_State *L) {
    CallInfo *ci = L->ci;
    CallInfo *next = ci->next;
    ci->next = nullptr;
    while ((ci = next) != nullptr) {
        next = ci->next;
        luaM_free(L, ci);
        L->nci--;
    }
}


/*
** free half of the CallInfo structures not in use by a thread
*/
void luaE_shrinkCI(lua_State *L) {
    CallInfo *ci = L->ci;
    CallInfo *next2;  /* next's next */
    /* while there are two nexts */
    while (ci->next != nullptr && (next2 = ci->next->next) != nullptr) {
        luaM_free(L, ci->next);  /* free next */
        L->nci--;
        ci->next = next2;  /* remove 'next' from the list */
        next2->previous = ci;
        ci = next2;  /* keep next's next */
    }
}


static void stack_init(lua_State *L1, lua_State *L) {
    int i; CallInfo *ci;
    /* initialize stack array */
    L1->stack = luaM_newvector(L, BASIC_STACK_SIZE, TValue);
    L1->stacksize = BASIC_STACK_SIZE;
    for (i = 0; i < BASIC_STACK_SIZE; i++)
        setnilvalue(L1->stack + i);  /* erase new stack */
    L1->top = L1->stack;
    L1->stack_last = L1->stack + L1->stacksize - EXTRA_STACK;
    /* initialize first ci */
    ci = &L1->base_ci;
    ci->next = ci->previous = nullptr;
    ci->callstatus = 0;
    ci->func = L1->top;
    setnilvalue(L1->top++);  /* 'function' entry for this 'ci' */
    ci->top = L1->top + LUA_MINSTACK;
    L1->ci = ci;
}


static void freestack(lua_State *L) {
    if (L->stack == nullptr)
        return;  /* stack not completely built yet */
    L->ci = &L->base_ci;  /* free the entire 'ci' list */
    luaE_freeCI(L);
    lua_assert(L->nci == 0);
    luaM_freearray(L, L->stack, L->stacksize);  /* free stack array */
}


/*
** Create registry table and its predefined values
*/
static void init_registry(lua_State *L) {
    TValue temp;
    /* create registry */
    Table *registry = luaH_new(L);
    sethvalue(L, &L->l_registry, registry);
    luaH_resize(L, registry, LUA_RIDX_LAST, 0);
    /* registry[LUA_RIDX_MAINTHREAD] = L */
    setthvalue(L, &temp, L);  /* temp = L */
    luaH_setint(L, registry, LUA_RIDX_MAINTHREAD, &temp);
    /* registry[LUA_RIDX_GLOBALS] = table of globals */
    sethvalue(L, &temp, luaH_new(L));  /* temp = new table (global table) */
    luaH_setint(L, registry, LUA_RIDX_GLOBALS, &temp);
}


/*
** open parts of the state that may cause memory-allocation errors.
** ('g->version' != nullptr flags that the state was completely build)
*/
static void f_luaopen(lua_State *L, void *ud) {
    UNUSED(ud);
    stack_init(L, L);  /* init stack */
    init_registry(L);
    luaS_init(L);
    luaT_init(L);
    luaX_init(L);
    luai_userstateopen(L);
}


/*
** preinitialize a thread with consistent values without allocating
** any memory (to avoid errors)
*/
static void preinit_thread(lua_State *L) {
    L->stack = nullptr;
    L->ci = nullptr;
    L->nci = 0;
    L->stacksize = 0;
    L->twups = L;  /* thread has no upvalues */
    L->errorJmp = nullptr;
    L->nCcalls = 0;
    L->hook = nullptr;
    L->hookmask = 0;
    L->basehookcount = 0;
    L->allowhook = 1;
    resethookcount(L);
    L->openupval = nullptr;
    L->nny = 1;
    L->status = LUA_OK;
    L->errfunc = 0;
}


static void close_state(lua_State *L) {
    luaF_close(L, L->stack);  /* close all upvalues for this thread */
    luaC_freeallobjects(L);  /* collect all objects */
    if (L->version)  /* closing a fully built state? */
        luai_userstateclose(L);
    luaM_freearray(L, L->strt.hash, L->strt.size);
    freestack(L);
	if (L->gc_state) {
		delete L->gc_state;
		// L->ud = nullptr;
	}
    // (*g->frealloc)(L->ud, fromstate(L), sizeof(LG), 0);  /* free main block */
}


// TODO: remove lua thread
LUA_API lua_State *lua_newthread(lua_State *L) {
    lua_State *L1;
    lua_lock(L);
    luaC_checkGC(L);
    /* create new thread */
    L1 = &lua_cast(LX *, luaM_newobject(L, LUA_TTHREAD, sizeof(LX)))->l;
    L1->tt = LUA_TTHREAD;
    /* anchor it on L stack */
    setthvalue(L, L->top, L1);
    api_incr_top(L);
    preinit_thread(L1);
    L1->hookmask = L->hookmask;
    L1->basehookcount = L->basehookcount;
    L1->hook = L->hook;
    resethookcount(L1);
    /* initialize L1 extra space */
    /*memcpy(lua_getextraspace(L1), lua_getextraspace(g->mainthread),
        LUA_EXTRASPACE);*/
    luai_userstatethread(L, L1);
    stack_init(L1, L);  /* init stack */
    lua_unlock(L);
    return L1;
}


void luaE_freethread(lua_State *L, lua_State *L1) {
    LX *l = fromstate(L1);
    luaF_close(L1, L1->stack);  /* close all upvalues for this thread */
    lua_assert(L1->openupval == nullptr);
    luai_userstatefree(L, L1);
    freestack(L1);
    luaM_free(L, l);
}


LUA_API lua_State *lua_newstate(lua_Alloc f, void *ud) {
    int i;
    lua_State *L;
	auto gc_state = new vmgc::GcState(LUA_MALLOC_TOTAL_SIZE);
	if (!gc_state) return nullptr;
    LG *l = lua_cast(LG *, (*f)(ud ? ud : gc_state, nullptr, LUA_TTHREAD, sizeof(LG)));
    if (l == nullptr) return nullptr;
    L = &l->l.l;
    L->next = nullptr;
    L->tt = LUA_TTHREAD;
	L->malloc_buffer = nullptr;// malloc(LUA_MALLOC_TOTAL_SIZE);
    L->malloc_pos = 0;
	L->gc_state = gc_state;
    memset(L->compile_error, 0x0, LUA_COMPILE_ERROR_MAX_LENGTH);
	memset(L->runerror, 0x0, LUA_VM_EXCEPTION_STRNG_MAX_LENGTH);
    L->in = stdin;
    L->out = stdout;
    L->err = stderr;
    L->force_stopping = false;
	L->exit_code = 0;
    L->preprocessor = nullptr;
	static const lua_Number version = LUA_VERSION_NUM;
	L->version = &version;
	L->frealloc = f;
	L->ud = ud ? ud : gc_state;
	L->seed = makeseed(L);
	setnilvalue(&L->l_registry);
	L->strt.size = L->strt.nuse = 0;
	L->strt.hash = nullptr;

	L->panic = nullptr;
    preinit_thread(L);

	L->evalstacksize = 100;
	L->evalstack = luaM_newvector(L, L->evalstacksize, TValue);
	L->evalstacktop = L->evalstack;


    for (i = 0; i < LUA_NUMTAGS; i++) L->mt[i] = nullptr;
    if (luaD_rawrunprotected(L, f_luaopen, nullptr) != LUA_OK) {
        /* memory allocation error: free partial state */
        close_state(L);
        L = nullptr;
    }
    return L;
}


LUA_API void lua_close(lua_State *L) {
    uvm::lua::lib::close_lua_state_values(L);
    lua_lock(L);
    close_state(L);
}

static size_t align8(size_t s) {
    if ((s & 0x7) == 0)
        return s;
    return ((s >> 3) + 1) << 3;
};

void *lua_malloc(lua_State *L, size_t size)
{
	return L->gc_state->gc_malloc(size);
}

void *lua_calloc(lua_State *L, size_t element_count, size_t element_size)
{
	auto p = L->gc_state->gc_malloc(element_count * element_size);
	if (!p)
		return nullptr;
	memset(p, 0, element_count * element_size);
	return p;
}

void lua_free(lua_State *L, void *address)
{
    if (nullptr == address || nullptr == L)
        return;
	L->gc_state->gc_free(address);
}
