/*
** $Id: lvm.c,v 2.265 2015/11/23 11:30:45 roberto Exp $
** Lua virtual machine
** See Copyright Notice in lua.h
*/

#define lvm_cpp
#define LUA_CORE

#include <uvm/lprefix.h>

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <algorithm>

#include <uvm/lua.h>

#include <uvm/ldebug.h>
#include <uvm/ldo.h>
#include <uvm/lfunc.h>
#include <uvm/lgc.h>
#include <uvm/lobject.h>
#include <uvm/lopcodes.h>
#include <uvm/lauxlib.h>
#include <uvm/lstate.h>
#include <uvm/lstring.h>
#include <uvm/ltable.h>
#include <uvm/ltm.h>
#include <uvm/lvm.h>
#include <uvm/uvm_api.h>
#include <uvm/uvm_lib.h>

using uvm::lua::api::global_uvm_chain_api;


/* limit for table tag-method chains (to avoid loops) */
#define MAXTAGLOOP	2000



/*
** 'l_intfitsf' checks whether a given integer can be converted to a
** float without rounding. Used in comparisons. Left undefined if
** all integers fit in a float precisely.
*/
#if !defined(l_intfitsf)

/* number of bits in the mantissa of a float */
#define NBM		(l_mathlim(MANT_DIG))

/*
** Check whether some integers may not fit in a float, that is, whether
** (maxinteger >> NBM) > 0 (that implies (1 << NBM) <= maxinteger).
** (The shifts are done in parts to avoid shifting by more than the size
** of an integer. In a worst case, NBM == 113 for long double and
** sizeof(integer) == 32.)
*/
#if ((((LUA_MAXINTEGER >> (NBM / 4)) >> (NBM / 4)) >> (NBM / 4)) \
	>> (NBM - (3 * (NBM / 4))))  >  0

#define l_intfitsf(i)  \
  (-((lua_Integer)1 << NBM) <= (i) && (i) <= ((lua_Integer)1 << NBM))

#endif

#endif



/*
** Try to convert a value to a float. The float case is already handled
** by the macro 'tonumber'.
*/
int luaV_tonumber_(const TValue *obj, lua_Number *n) {
    TValue v;
    if (ttisinteger(obj)) {
        *n = cast_num(ivalue(obj));
        return 1;
    }
    else if (cvt2num(obj) &&  /* string convertible to number? */
        luaO_str2num(svalue(obj), &v) == vslen(obj) + 1) {
        *n = nvalue(&v);  /* convert result of 'luaO_str2num' to a float */
        return 1;
    }
    else {
        return 0;  // conversion failed
    }
}


/*
** try to convert a value to an integer, rounding according to 'mode':
** mode == 0: accepts only integral values
** mode == 1: takes the floor of the number
** mode == 2: takes the ceil of the number
*/
int luaV_tointeger(const TValue *obj, lua_Integer *p, int mode) {
    TValue v;
again:
    if (ttisfloat(obj)) {
        lua_Number n = fltvalue(obj);
        lua_Number f = l_floor(n);
        if (n != f) {  /* not an integral value? */
            if (mode == 0) return 0;  /* fails if mode demands integral value */
            else if (mode > 1)  /* needs ceil? */
                f += 1;  /* convert floor to ceil (remember: n != f) */
        }
        return lua_numbertointeger(f, p);
    }
    else if (ttisinteger(obj)) {
        *p = ivalue(obj);
        return 1;
    }
    else if (cvt2num(obj) &&
        luaO_str2num(svalue(obj), &v) == vslen(obj) + 1) {
        obj = &v;
        goto again;  /* convert result from 'luaO_str2num' to an integer */
    }
    return 0;  /* conversion failed */
}


/*
** Try to convert a 'for' limit to an integer, preserving the
** semantics of the loop.
** (The following explanation assumes a non-negative step; it is valid
** for negative steps mutatis mutandis.)
** If the limit can be converted to an integer, rounding down, that is
** it.
** Otherwise, check whether the limit can be converted to a number.  If
** the number is too large, it is OK to set the limit as LUA_MAXINTEGER,
** which means no limit.  If the number is too negative, the loop
** should not run, because any initial integer value is larger than the
** limit. So, it sets the limit to LUA_MININTEGER. 'stopnow' corrects
** the extreme case when the initial value is LUA_MININTEGER, in which
** case the LUA_MININTEGER limit would still run the loop once.
*/
static int forlimit(const TValue *obj, lua_Integer *p, lua_Integer step,
    int *stopnow) {
    *stopnow = 0;  /* usually, let loops run */
    //  printf("forlimit\n");
    if (!luaV_tointeger(obj, p, (step < 0 ? 2 : 1))) {  /* not fit in integer? */
        lua_Number n;  /* try to convert to float */
        if (!tonumber(obj, &n)) /* cannot convert to float? */
            return 0;  /* not a number */
        if (luai_numlt(0, n)) {  /* if true, float is larger than max integer */
            *p = LUA_MAXINTEGER;
            if (step < 0) *stopnow = 1;
        }
        else {  /* float is smaller than min integer */
            *p = LUA_MININTEGER;
            if (step >= 0) *stopnow = 1;
        }
    }
    lua_Number  n2;
    if (!tonumber(obj, &n2)) {
        return 0;
    }
    return 1;
}


/*
** Complete a table access: if 't' is a table, 'tm' has its metamethod;
** otherwise, 'tm' is nullptr.
*/
void luaV_finishget(lua_State *L, const TValue *t, TValue *key, StkId val,
    const TValue *tm) {
    int loop;  /* counter to avoid infinite loops */
    lua_assert(tm != nullptr || !ttistable(t));
    for (loop = 0; loop < MAXTAGLOOP; loop++) {
        if (tm == nullptr) {  /* no metamethod (from a table)? */
            if (ttisnil(tm = luaT_gettmbyobj(L, t, TM_INDEX)))
                luaG_typeerror(L, t, "index");  /* no metamethod */
        }
        if (ttisfunction(tm)) {  /* metamethod is a function */
            luaT_callTM(L, tm, t, key, val, 1);  /* call it */
            return;
        }
        t = tm;  /* else repeat access over 'tm' */
        if (luaV_fastget(L, t, key, tm, luaH_get)) {  /* try fast track */
            setobj2s(L, val, tm);  /* done */
            return;
        }
        /* else repeat */
    }
    luaG_runerror(L, "gettable chain too long; possible loop");
}


/*
** Main function for table assignment (invoking metamethods if needed).
** Compute 't[key] = val'
*/
void luaV_finishset(lua_State *L, const TValue *t, TValue *key,
    StkId val, const TValue *oldval) {
	if (ttistable(t)) {
		auto table_addr = (intptr_t)t->value_.gc;
		if (L->allow_contract_modify != table_addr && L->contract_table_addresses
			&& std::find(L->contract_table_addresses->begin(), L->contract_table_addresses->end(), table_addr) != L->contract_table_addresses->end()) {
			luaG_runerror(L, "can't modify contract properties");
			return;
		}
	}
    int loop;  /* counter to avoid infinite loops */
    for (loop = 0; loop < MAXTAGLOOP; loop++) {
        const TValue *tm;
        if (oldval != nullptr) {
            // lua_assert(ttistable(t) && ttisnil(oldval));
            Table *h = hvalue(t); // save t table
            lua_assert(ttisnil(oldval));
            /* must check the metamethod */
            // if ((tm = fasttm(L, hvalue(t)->metatable, TM_NEWINDEX)) == nullptr &&
            if ((tm = fasttm(L, h->metatable, TM_NEWINDEX)) == nullptr &&
                /* no metamethod; is there a previous entry in the table? */
                (oldval != luaO_nilobject ||
                /* no previous entry; must create one. (The next test is
                   always true; we only need the assignment.) */
                   // (oldval = luaH_newkey(L, hvalue(t), key), 1))) {
                   (oldval = luaH_newkey(L, h, key), 1))) {
                /* no metamethod and (now) there is an entry with given key */
                setobj2t(L, lua_cast(TValue *, oldval), val);
                // invalidateTMcache(hvalue(t));
                // luaC_barrierback(L, hvalue(t), val);
                invalidateTMcache(h);
                luaC_barrierback(L, h, val);
                return;
            }
            /* else will try the metamethod */
        }
        else {  /* not a table; check metamethod */
            if (ttisnil(tm = luaT_gettmbyobj(L, t, TM_NEWINDEX)))
                luaG_typeerror(L, t, "index");
            if (L->force_stopping)
                return;
        }
        /* try the metamethod */
        if (ttisfunction(tm)) {
            luaT_callTM(L, tm, t, key, val, 0);
            return;
        }
        t = tm;  /* else repeat assignment over 'tm' */
        if (luaV_fastset(L, t, key, oldval, luaH_get, val))
            return;  /* done */
        /* else loop */
    }
    luaG_runerror(L, "settable chain too long; possible loop");
}


/*
** Compare two strings 'ls' x 'rs', returning an integer smaller-equal-
** -larger than zero if 'ls' is smaller-equal-larger than 'rs'.
** The code is a little tricky because it allows '\0' in the strings
** and it uses 'strcoll' (to respect locales) for each segments
** of the strings.
*/
static int l_strcmp(const TString *ls, const TString *rs) {
    const char *l = getstr(ls);
    size_t ll = tsslen(ls);
    const char *r = getstr(rs);
    size_t lr = tsslen(rs);
	if (ll != lr)
		return (int)ll - (int) lr;
    for (;;) {  /* for each segment */
        int temp = strcoll(l, r);
        if (temp != 0)  /* not equal? */
            return temp;  /* done */
        else {  /* strings are equal up to a '\0' */
            size_t len = strlen(l);  /* index of first '\0' in both strings */
            if (len == lr)  /* 'rs' is finished? */
                return (len == ll) ? 0 : 1;  /* check 'ls' */
            else if (len == ll)  /* 'ls' is finished? */
                return -1;  /* 'ls' is smaller than 'rs' ('rs' is not finished) */
            /* both strings longer than 'len'; go on comparing after the '\0' */
            len++;
            l += len; ll -= len; r += len; lr -= len;
        }
    }
}

static int l_c_str_cmp(const char *l, const char *r)
{
	if (strcmp(l, r) == 0)
		return 0;
	lua_table_less cmp_op;
	return cmp_op(l, r) ? -1 : 1;
}


/*
** Check whether integer 'i' is less than float 'f'. If 'i' has an
** exact representation as a float ('l_intfitsf'), compare numbers as
** floats. Otherwise, if 'f' is outside the range for integers, result
** is trivial. Otherwise, compare them as integers. (When 'i' has no
** float representation, either 'f' is "far away" from 'i' or 'f' has
** no precision left for a fractional part; either way, how 'f' is
** truncated is irrelevant.) When 'f' is NaN, comparisons must result
** in false.
*/
static int LTintfloat(lua_Integer i, lua_Number f) {
#if defined(l_intfitsf)
    if (!l_intfitsf(i)) {
        if (f >= -cast_num(LUA_MININTEGER))  /* -minint == maxint + 1 */
            return 1;  /* f >= maxint + 1 > i */
        else if (f > cast_num(LUA_MININTEGER))  /* minint < f <= maxint ? */
            return (i < lua_cast(lua_Integer, f));  /* compare them as integers */
        else  /* f <= minint <= i (or 'f' is NaN)  -->  not(i < f) */
            return 0;
    }
#endif
    return luai_numlt(cast_num(i), f);  /* compare them as floats */
}


/*
** Check whether integer 'i' is less than or equal to float 'f'.
** See comments on previous function.
*/
static int LEintfloat(lua_Integer i, lua_Number f) {
#if defined(l_intfitsf)
    if (!l_intfitsf(i)) {
        if (f >= -cast_num(LUA_MININTEGER))  /* -minint == maxint + 1 */
            return 1;  /* f >= maxint + 1 > i */
        else if (f >= cast_num(LUA_MININTEGER))  /* minint <= f <= maxint ? */
            return (i <= lua_cast(lua_Integer, f));  /* compare them as integers */
        else  /* f < minint <= i (or 'f' is NaN)  -->  not(i <= f) */
            return 0;
    }
#endif
    return luai_numle(cast_num(i), f);  /* compare them as floats */
}


/*
** Return 'l < r', for numbers.
*/
static int LTnum(const TValue *l, const TValue *r) {
    if (ttisinteger(l)) {
        lua_Integer li = ivalue(l);
        if (ttisinteger(r))
            return li < ivalue(r);  /* both are integers */
        else  /* 'l' is int and 'r' is float */
            return LTintfloat(li, fltvalue(r));  /* l < r ? */
    }
    else {
        lua_Number lf = fltvalue(l);  /* 'l' must be float */
        if (ttisfloat(r))
            return luai_numlt(lf, fltvalue(r));  /* both are float */
        else if (luai_numisnan(lf))  /* 'r' is int and 'l' is float */
            return 0;  /* NaN < i is always false */
        else  /* without NaN, (l < r)  <-->  not(r <= l) */
            return !LEintfloat(ivalue(r), lf);  /* not (r <= l) ? */
    }
}


/*
** Return 'l <= r', for numbers.
*/
static int LEnum(const TValue *l, const TValue *r) {
    if (ttisinteger(l)) {
        lua_Integer li = ivalue(l);
        if (ttisinteger(r))
            return li <= ivalue(r);  /* both are integers */
        else  /* 'l' is int and 'r' is float */
            return LEintfloat(li, fltvalue(r));  /* l <= r ? */
    }
    else {
        lua_Number lf = fltvalue(l);  /* 'l' must be float */
        if (ttisfloat(r))
            return luai_numle(lf, fltvalue(r));  /* both are float */
        else if (luai_numisnan(lf))  /* 'r' is int and 'l' is float */
            return 0;  /*  NaN <= i is always false */
        else  /* without NaN, (l <= r)  <-->  not(r < l) */
            return !LTintfloat(ivalue(r), lf);  /* not (r < l) ? */
    }
}


/*
** Main operation less than; return 'l < r'.
*/
int luaV_lessthan(lua_State *L, const TValue *l, const TValue *r) {
    int res;
    if (ttisnumber(l) && ttisnumber(r))  /* both operands are numbers? */
        return LTnum(l, r);
    else if (ttisstring(l) && ttisstring(r))  /* both are strings? */
        return l_strcmp(tsvalue(l), tsvalue(r)) < 0;
	else if (ttisstring(l) && ttisnumber(r))
	{
		auto lstr = svalue(l);
		auto rstr = std::to_string(ttisinteger(r) ? ivalue(r) : fltvalue(r));
		return l_c_str_cmp(lstr, rstr.c_str());
	}
	else if(ttisnumber(l) && ttisstring(r))
	{
		auto lstr = std::to_string(ttisinteger(l) ? ivalue(l) : fltvalue(l));
		auto rstr = svalue(r);
		return l_c_str_cmp(lstr.c_str(), rstr);
	}
    else if ((res = luaT_callorderTM(L, l, r, TM_LT)) < 0)  /* no metamethod? */
        luaG_ordererror(L, l, r);  /* error */
    return res;
}


/*
** Main operation less than or equal to; return 'l <= r'. If it needs
** a metamethod and there is no '__le', try '__lt', based on
** l <= r iff !(r < l) (assuming a total order). If the metamethod
** yields during this substitution, the continuation has to know
** about it (to negate the result of r<l); bit CIST_LEQ in the call
** status keeps that information.
*/
int luaV_lessequal(lua_State *L, const TValue *l, const TValue *r) {
    int res;
    if (ttisnumber(l) && ttisnumber(r))  /* both operands are numbers? */
        return LEnum(l, r);
    else if (ttisstring(l) && ttisstring(r))  /* both are strings? */
        return l_strcmp(tsvalue(l), tsvalue(r)) <= 0;
	else if(ttisstring(l) && ttisnumber(r))
	{
		auto lstr = svalue(l);
		auto rstr = std::to_string(ttisinteger(r) ? ivalue(r) : fltvalue(r));
		return l_c_str_cmp(lstr, rstr.c_str()) <= 0;
	}
	else if(ttisnumber(l) && ttisstring(r))
	{
		auto lstr = std::to_string(ttisinteger(l) ? ivalue(l) : fltvalue(l));
		auto rstr = svalue(r);
		return l_c_str_cmp(lstr.c_str(), rstr) <= 0;
	}
    else if ((res = luaT_callorderTM(L, l, r, TM_LE)) >= 0)  /* try 'le' */
        return res;
    else {  /* try 'lt': */
        L->ci->callstatus |= CIST_LEQ;  /* mark it is doing 'lt' for 'le' */
        res = luaT_callorderTM(L, r, l, TM_LT);
        L->ci->callstatus ^= CIST_LEQ;  /* clear mark */
        if (res < 0)
            luaG_ordererror(L, l, r);
        return !res;  /* result is negated */
    }
}


/*
** Main operation for equality of Lua values; return 't1 == t2'.
** L == nullptr means raw equality (no metamethods)
*/
int luaV_equalobj(lua_State *L, const TValue *t1, const TValue *t2) {
    const TValue *tm;
    if (ttype(t1) != ttype(t2)) {  /* not the same variant? */
        if (ttnov(t1) != ttnov(t2) || ttnov(t1) != LUA_TNUMBER)
            return 0;  /* only numbers can be equal with different variants */
        else {  /* two numbers with different variants */
            lua_Integer i1, i2;  /* compare them as integers */
            return (tointeger(t1, &i1) && tointeger(t2, &i2) && i1 == i2);
        }
    }
    /* values have same type and same variant */
    switch (ttype(t1)) {
    case LUA_TNIL: return 1;
    case LUA_TNUMINT: return (ivalue(t1) == ivalue(t2));
    case LUA_TNUMFLT: return luai_numeq(fltvalue(t1), fltvalue(t2));
    case LUA_TBOOLEAN: return bvalue(t1) == bvalue(t2);  /* true must be 1 !! */
    case LUA_TLIGHTUSERDATA: return pvalue(t1) == pvalue(t2);
    case LUA_TLCF: return fvalue(t1) == fvalue(t2);
    case LUA_TSHRSTR: return eqshrstr(tsvalue(t1), tsvalue(t2));
    case LUA_TLNGSTR: return luaS_eqlngstr(tsvalue(t1), tsvalue(t2));
    case LUA_TUSERDATA: {
        if (uvalue(t1) == uvalue(t2)) return 1;
        else if (L == nullptr) return 0;
        tm = fasttm(L, uvalue(t1)->metatable, TM_EQ);
        if (tm == nullptr)
            tm = fasttm(L, uvalue(t2)->metatable, TM_EQ);
        break;  /* will try TM */
    }
    case LUA_TTABLE: {
        if (hvalue(t1) == hvalue(t2)) return 1;
        else if (L == nullptr) return 0;
        tm = fasttm(L, hvalue(t1)->metatable, TM_EQ);
        if (tm == nullptr)
            tm = fasttm(L, hvalue(t2)->metatable, TM_EQ);
        break;  /* will try TM */
    }
    default:
        return gcvalue(t1) == gcvalue(t2);
    }
    if (tm == nullptr)  /* no TM? */
        return 0;  /* objects are different */
    luaT_callTM(L, tm, t1, t2, L->top, 1);  /* call TM */
    return !l_isfalse(L->top);
}


/* macro used by 'luaV_concat' to ensure that element at 'o' is a string */
#define tostring(L,o)  \
	(ttisstring(o) || (cvt2str(o) && (luaO_tostring(L, o), 1)))

#define isemptystr(o)	(ttisshrstring(o) && tsvalue(o)->shrlen == 0)

/* copy strings in stack from top - n up to top - 1 to buffer */
static void copy2buff(StkId top, int n, char *buff) {
    size_t tl = 0;  /* size already copied */
    do {
        size_t l = vslen(top - n);  /* length of string being copied */
        memcpy(buff + tl, svalue(top - n), l * sizeof(char));
        tl += l;
    } while (--n > 0);
}


/*
** Main operation for concatenation: concat 'total' values in the stack,
** from 'L->top - total' up to 'L->top - 1'.
*/
void luaV_concat(lua_State *L, int total) {
    lua_assert(total >= 2);
    do {
        StkId top = L->top;
        int n = 2;  /* number of elements handled in this pass (at least 2) */
        if (!(ttisstring(top - 2) || cvt2str(top - 2)) || !tostring(L, top - 1))
            luaT_trybinTM(L, top - 2, top - 1, top - 2, TM_CONCAT);
        else if (isemptystr(top - 1))  /* second operand is empty? */
            cast_void(tostring(L, top - 2));  /* result is first operand */
        else if (isemptystr(top - 2)) {  /* first operand is an empty string? */
            setobjs2s(L, top - 2, top - 1);  /* result is second op. */
        }
        else {
            /* at least two non-empty string values; get as many as possible */
            size_t tl = vslen(top - 1);
            TString *ts;
            /* collect total length and number of strings */
            for (n = 1; n < total && tostring(L, top - n - 1); n++) {
                size_t l = vslen(top - n - 1);
                if (l >= (UVM_MAX_SIZE / sizeof(char)) - tl)
                    luaG_runerror(L, "string length overflow");
                tl += l;
            }
            if (tl <= LUAI_MAXSHORTLEN) {  /* is result a short string? */
                char buff[LUAI_MAXSHORTLEN];
                copy2buff(top, n, buff);  /* copy strings to buffer */
                ts = luaS_newlstr(L, buff, tl);
            }
            else {  /* long string; copy strings directly to final result */
                ts = luaS_createlngstrobj(L, tl);
                copy2buff(top, n, getstr(ts));
            }
            setsvalue2s(L, top - n, ts);  /* create result */
        }
        total -= n - 1;  /* got 'n' strings to create 1 new */
        L->top -= n - 1;  /* popped 'n' strings and pushed one */
    } while (total > 1);  /* repeat until only 1 result left */
}


/*
** Main operation 'ra' = #rb'.
*/
void luaV_objlen(lua_State *L, StkId ra, const TValue *rb) {
    const TValue *tm;
    switch (ttype(rb)) {
    case LUA_TTABLE: {
        Table *h = hvalue(rb);
        tm = fasttm(L, h->metatable, TM_LEN);
        if (tm) break;  /* metamethod? break switch to call it */
        setivalue(ra, luaH_getn(h));  /* else primitive len */
        return;
    }
    case LUA_TSHRSTR: {
        setivalue(ra, tsvalue(rb)->shrlen);
        return;
    }
    case LUA_TLNGSTR: {
        setivalue(ra, tsvalue(rb)->u.lnglen);
        return;
    }
    default: {  /* try metamethod */
        tm = luaT_gettmbyobj(L, rb, TM_LEN);
        if (ttisnil(tm))  /* no metamethod? */
            luaG_typeerror(L, rb, "get length of");
        break;
    }
    }
    luaT_callTM(L, tm, rb, rb, ra, 1);
}


/*
** Integer division; return 'm // n', that is, floor(m/n).
** C division truncates its result (rounds towards zero).
** 'floor(q) == trunc(q)' when 'q >= 0' or when 'q' is integer,
** otherwise 'floor(q) == trunc(q) - 1'.
*/
lua_Integer luaV_div(lua_State *L, lua_Integer m, lua_Integer n) {
    if (l_castS2U(n) + 1u <= 1u) {  /* special cases: -1 or 0 */
        if (n == 0)
            luaG_runerror(L, "attempt to divide by zero");
        return intop(-, 0, m);   /* n==-1; avoid overflow with 0x80000...//-1 */
    }
    else {
        lua_Integer q = m / n;  /* perform C division */
        if ((m ^ n) < 0 && m % n != 0)  /* 'm/n' would be negative non-integer? */
            q -= 1;  /* correct result for different rounding */
        return q;
    }
}


/*
** Integer modulus; return 'm % n'. (Assume that C '%' with
** negative operands follows C99 behavior. See previous comment
** about luaV_div.)
*/
lua_Integer luaV_mod(lua_State *L, lua_Integer m, lua_Integer n) {
    if (l_castS2U(n) + 1u <= 1u) {  /* special cases: -1 or 0 */
        if (n == 0)
            luaG_runerror(L, "attempt to perform 'n%%0'");
        return 0;   /* m % -1 == 0; avoid overflow with 0x80000...%-1 */
    }
    else {
        lua_Integer r = m % n;
        if (r != 0 && (m ^ n) < 0)  /* 'm/n' would be non-integer negative? */
            r += n;  /* correct result for different rounding */
        return r;
    }
}


/* number of bits in an integer */
#define NBITS	cast_int(sizeof(lua_Integer) * CHAR_BIT)

/*
** Shift left operation. (Shift right just negates 'y'.)
*/
lua_Integer luaV_shiftl(lua_Integer x, lua_Integer y) {
    if (y < 0) {  /* shift right? */
        if (y <= -NBITS) return 0;
        else return intop(>> , x, -y);
    }
    else {  /* shift left */
        if (y >= NBITS) return 0;
        else return intop(<< , x, y);
    }
}


/*
** check whether cached closure in prototype 'p' may be reused, that is,
** whether there is a cached closure with the same upvalues needed by
** new closure to be created.
*/
static LClosure *getcached(Proto *p, UpVal **encup, StkId base) {
    LClosure *c = p->cache;
    if (c != nullptr) {  /* is there a cached closure? */
        int nup = p->sizeupvalues;
        Upvaldesc *uv = p->upvalues;
        int i;
        for (i = 0; i < nup; i++) {  /* check whether it has right upvalues */
            TValue *v = uv[i].instack ? base + uv[i].idx : encup[uv[i].idx]->v;
            if (c->upvals[i]->v != v)
                return nullptr;  /* wrong upvalue; cannot reuse closure */
        }
    }
    return c;  /* return cached closure (or nullptr if no cached closure) */
}


/*
** create a new Lua closure, push it in the stack, and initialize
** its upvalues. Note that the closure is not cached if prototype is
** already black (which means that 'cache' was already cleared by the
** GC).
*/
static void pushclosure(lua_State *L, Proto *p, UpVal **encup, int enc_nupvalues, StkId base,
    StkId ra) {
    int nup = p->sizeupvalues;
    Upvaldesc *uv = p->upvalues;
    int i;
    LClosure *ncl = luaF_newLclosure(L, nup);
    ncl->p = p;
    setclLvalue(L, ra, ncl);  /* anchor new closure in stack */
    for (i = 0; i < nup; i++) {  /* fill in its upvalues */
        if (uv[i].instack)  /* upvalue refers to local variable? */
            ncl->upvals[i] = luaF_findupval(L, base + uv[i].idx);
		else  /* get upvalue from enclosing function */
		{
			auto idx = uv[i].idx;
			if (idx >= enc_nupvalues || idx < 0)
			{
				L->force_stopping = true;
				auto upvalue_name = getstr(uv[i].name);
				luaG_runerror(L, "upvalue %s index out of parent closure's upvalues size", upvalue_name ? upvalue_name : "");
				return;
			}
			ncl->upvals[i] = encup[idx];
		}
        if (nullptr != ncl->upvals[i])
            ncl->upvals[i]->refcount++;
        /* new closure is white, so we do not need a barrier here */
    }
    if (!isblack(p))  /* cache will not break GC invariant? */
        p->cache = ncl;  /* save it on cache for reuse */
}

/*
** finish execution of an opcode interrupted by an yield
*/
void luaV_finishOp(lua_State *L) {
    CallInfo *ci = L->ci;
    StkId base = ci->u.l.base;
    Instruction inst = *(ci->u.l.savedpc - 1);  /* interrupted instruction */
    OpCode op = GET_OPCODE(inst);
    switch (op) {  /* finish its execution */
    case UOP_ADD: case UOP_SUB: case UOP_MUL: case UOP_DIV: case UOP_IDIV:
    case UOP_BAND: case UOP_BOR: case UOP_BXOR: case UOP_SHL: case UOP_SHR:
    case UOP_MOD: case UOP_POW:
    case UOP_UNM: case UOP_BNOT: case UOP_LEN:
    case UOP_GETTABUP: case UOP_GETTABLE: case UOP_SELF: {
        setobjs2s(L, base + GETARG_A(inst), --L->top);
        break;
    }
    case UOP_LE: case UOP_LT: case UOP_EQ: {
        int res = !l_isfalse(L->top - 1);
        L->top--;
        if (ci->callstatus & CIST_LEQ) {  /* "<=" using "<" instead? */
            lua_assert(op == UOP_LE);
            ci->callstatus ^= CIST_LEQ;  /* clear mark */
            res = !res;  /* negate result */
        }
        lua_assert(GET_OPCODE(*ci->u.l.savedpc) == UOP_JMP);
        if (res != GETARG_A(inst))  /* condition failed? */
            ci->u.l.savedpc++;  /* skip jump instruction */
        break;
    }
    case UOP_CONCAT: {
        StkId top = L->top - 1;  /* top when 'luaT_trybinTM' was called */
        int b = GETARG_B(inst);      /* first element to concatenate */
        int total = cast_int(top - 1 - (base + b));  /* yet to concatenate */
        setobj2s(L, top - 2, top);  /* put TM result in proper position */
        if (total > 1) {  /* are there elements to concat? */
            L->top = top - 1;  /* top is one after last element (at top-2) */
            luaV_concat(L, total);  /* concat them (may yield again) */
        }
        /* move final result to final position */
        setobj2s(L, ci->u.l.base + GETARG_A(inst), L->top - 1);
        L->top = ci->top;  /* restore top */
        break;
    }
    case UOP_TFORCALL: {
        lua_assert(GET_OPCODE(*ci->u.l.savedpc) == UOP_TFORLOOP);
        L->top = ci->top;  /* correct top */
        break;
    }
    case UOP_CALL: {
        if (GETARG_C(inst) - 1 >= 0)  /* nresults >= 0? */
            L->top = ci->top;  /* adjust results */
        break;
    }
    case UOP_TAILCALL: case UOP_SETTABUP: case UOP_SETTABLE:
        break;
    default: lua_assert(0);
    }
}




/*
** {==================================================================
** Function 'luaV_execute': main interpreter loop
** ===================================================================
*/


/*
** some macros for common tasks in 'luaV_execute'
*/


#define RA(i)	(base+GETARG_A(i))
#define RB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgR, base+GETARG_B(i))
#define RC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgR, base+GETARG_C(i))
#define RKB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_B(i)) ? k+INDEXK(GETARG_B(i)) : base+GETARG_B(i))
#define RKC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_C(i)) ? k+INDEXK(GETARG_C(i)) : base+GETARG_C(i))


/* execute a jump instruction */
#define dojump(ci,i,e) \
  { int a = GETARG_A(i); \
    if (a != 0) luaF_close(L, ci->u.l.base + a - 1); \
    ci->u.l.savedpc += GETARG_sBx(i) + e; }

/* for test instructions, execute the jump instruction that follows it */
#define donextjump(ci)	{ i = *ci->u.l.savedpc; dojump(ci, i, 1); }


#define Protect(x)	{ {x;}; base = ci->u.l.base; }

#define checkGC(L,c)  \
	{ luaC_condGC(L, L->top = (c),  /* limit of live values */ \
                         Protect(L->top = ci->top));  /* restore top */ \
           luai_threadyield(L); }


#define vmdispatch(o)	switch(o)
#define vmcase(l)	case l:
#define vmbreak		break


/*
** copy of 'luaV_gettable', but protecting call to potential metamethod
** (which can reallocate the stack)
*/
#define gettableProtected(L,t,k,v)  { const TValue *aux; \
  if (luaV_fastget(L,t,k,aux,luaH_get)) { setobj2s(L, v, aux); } \
    else Protect(luaV_finishget(L,t,k,v,aux)); }


/* same for 'luaV_settable' */
#define settableProtected(L,t,k,v) { const TValue *slot; \
	  Protect(        \
	if (t && ttistable(t)) {           \
		auto table_addr = (intptr_t)t->value_.gc;             \
		if (L->allow_contract_modify != table_addr && L->contract_table_addresses           \
			&& std::find(L->contract_table_addresses->begin(), L->contract_table_addresses->end(), table_addr) != L->contract_table_addresses->end()) { \
			luaG_runerror(L, "can't modify contract properties"); \
			return; \
		} \
	} \
	); \
  if (!luaV_fastset(L,t,k,slot,luaH_get,v)) \
    Protect(luaV_finishset(L,t,k,v,slot)); }
// FIXME: end duplicate code in uvm_lib.cpp

static int get_line_in_current_proto(CallInfo* ci, Proto *proto)
{
	int idx = (int)(ci->u.l.savedpc - proto->code); // proto
	if (idx < proto->sizecode)
	{
		int line_in_proto = proto->lineinfo[idx];
		return line_in_proto;
	}
	else
		return 0;
}

#define lua_check_in_vm_error(cond, error_msg) {    \
if (!(cond)) {      \
  L->force_stopping = true; \
  luaG_runerror(L, error_msg); \
  vmbreak;                                   \
     }                             \
}
void luaV_execute(lua_State *L)
{
    if (L->force_stopping)
        return;
    CallInfo *ci = L->ci;
    LClosure *cl;
    TValue *k;
    StkId base;
    ci->callstatus |= CIST_FRESH;  /* fresh invocation of 'luaV_execute" */
newframe:  /* reentry point when frame changes (call/return) */
    lua_assert(ci == L->ci);
    cl = clLvalue(ci->func);  /* local reference to function's closure */
    k = cl->p->k;  /* local reference to function's constant table */
    base = ci->u.l.base;  /* local copy of function's base */

    int insts_limit = uvm::lua::lib::get_lua_state_value(L, INSTRUCTIONS_LIMIT_LUA_STATE_MAP_KEY).int_value;
    int *stopped_pointer = uvm::lua::lib::get_lua_state_value(L, LUA_STATE_STOP_TO_RUN_IN_LVM_STATE_MAP_KEY).int_pointer_value;
    if (nullptr == stopped_pointer)
    {
        uvm::lua::lib::notify_lua_state_stop(L);
        uvm::lua::lib::resume_lua_state_running(L);
        stopped_pointer = uvm::lua::lib::get_lua_state_value(L, LUA_STATE_STOP_TO_RUN_IN_LVM_STATE_MAP_KEY).int_pointer_value;
    }
    int has_insts_limit = insts_limit > 0 ? 1 : 0;
    int *insts_executed_count = uvm::lua::lib::get_lua_state_value(L, INSTRUCTIONS_EXECUTED_COUNT_LUA_STATE_MAP_KEY).int_pointer_value;
    if (nullptr == insts_executed_count)
    {
        insts_executed_count = static_cast<int*>(lua_malloc(L, sizeof(int)));
        *insts_executed_count = 0;
        UvmStateValue lua_state_value_of_exected_count;
        lua_state_value_of_exected_count.int_pointer_value = insts_executed_count;
        uvm::lua::lib::set_lua_state_value(L, INSTRUCTIONS_EXECUTED_COUNT_LUA_STATE_MAP_KEY, lua_state_value_of_exected_count, LUA_STATE_VALUE_INT_POINTER);
    }
    if (*insts_executed_count < 0)
        *insts_executed_count = 0;

    lua_getglobal(L, "last_return");
	bool use_last_return = true; // lua_istable(L, -1);
    lua_pop(L, 1);

	int last_debug_line_in_file = -1;

    /* main loop of interpreter */
    for (;;) {
        if (!ci || ci->u.l.savedpc == nullptr) {
          global_uvm_chain_api->throw_exception(L, UVM_API_LVM_LIMIT_OVER_ERROR, "wrong bytecode instruction, can't find savedpc");
          vmbreak;
        }
        Instruction i = *(ci->u.l.savedpc++);

#ifdef DEBUG
	printf("%d\n", GET_OPCODE(i));
#endif // DEBUG

        StkId ra;

		*insts_executed_count += 1; // executed instructions count

        // limit instructions count, and executed instructions
        if (has_insts_limit && *insts_executed_count > insts_limit)
        {
            global_uvm_chain_api->throw_exception(L, UVM_API_LVM_LIMIT_OVER_ERROR, "over instructions limit");
            vmbreak;
        }
        if (stopped_pointer && *stopped_pointer > 0)
            vmbreak;
        if (L->force_stopping)
            vmbreak;
        

        // when over contract api limit, also vmbreak
        if ((GET_OPCODE(i) == UOP_CALL || GET_OPCODE(i) == UOP_TAILCALL)
            && global_uvm_chain_api->check_contract_api_instructions_over_limit(L))
        {
            global_uvm_chain_api->throw_exception(L, UVM_API_LVM_LIMIT_OVER_ERROR, "over instructions limit");
            vmbreak;
        }

        if (L->hookmask & (LUA_MASKLINE | LUA_MASKCOUNT))
            Protect(luaG_traceexec(L));
        /* WARNING: several calls may realloc the stack and invalidate 'ra' */
        ra = RA(i);
        lua_assert(base == ci->u.l.base);
        lua_assert(base <= L->top && L->top < L->stack + L->stacksize);
		
        vmdispatch(GET_OPCODE(i)) {
            vmcase(UOP_MOVE) {
                setobjs2s(L, ra, RB(i));
                vmbreak;
            }
            vmcase(UOP_LOADK) {
                TValue *rb = k + GETARG_Bx(i);
                setobj2s(L, ra, rb);
                vmbreak;
            }
            vmcase(UOP_LOADKX) {
                TValue *rb;
                lua_assert(GET_OPCODE(*ci->u.l.savedpc) == UOP_EXTRAARG);
                rb = k + GETARG_Ax(*ci->u.l.savedpc++);
                setobj2s(L, ra, rb);
                vmbreak;
            }
            vmcase(UOP_LOADBOOL) {
                setbvalue(ra, GETARG_B(i));
                if (GETARG_C(i)) ci->u.l.savedpc++;  /* skip next instruction (if C) */
                vmbreak;
            }
            vmcase(UOP_LOADNIL) {
                int b = GETARG_B(i);
                lua_check_in_vm_error(b >= 0, "loadnil instruction arg must be positive integer");
                do {
                    setnilvalue(ra++);
                } while (b--);
                vmbreak;
            }
            vmcase(UOP_GETUPVAL) {
                int b = GETARG_B(i);
                lua_check_in_vm_error(b < cl->nupvalues && b>=0, "upvalue error");
                setobj2s(L, ra, cl->upvals[b]->v);
                vmbreak;
            }
            vmcase(UOP_GETTABUP) {
                auto upval_index = GETARG_B(i);
                lua_check_in_vm_error(upval_index < cl->nupvalues && upval_index >=0, "upvalue error");
                if (nullptr == cl->upvals[upval_index])
                {
                    *stopped_pointer = 1;
                    vmbreak;
                }
                TValue *upval = cl->upvals[upval_index]->v;
                TValue *rc = RKC(i);
                gettableProtected(L, upval, rc, ra);
                vmbreak;
            }
            vmcase(UOP_GETTABLE) {
                StkId rb = RB(i);
                TValue *rc = RKC(i);
                bool istable = ttistable(rb);
                if (!istable)
                {
					luaG_runerror(L, "getfield of nil, need table here");
                    L->force_stopping = true;
                    vmbreak;
                }
                gettableProtected(L, rb, rc, ra);
                vmbreak;
            }
            vmcase(UOP_SETTABUP) {
                auto upval_index = GETARG_A(i);
                lua_check_in_vm_error(upval_index < cl->nupvalues && upval_index >=0, "upvalue error");
				if(!cl->upvals[upval_index])
				{
					luaG_runerror(L, "set upvalue of nil, need table here");
					L->force_stopping = true;
					vmbreak;
				}
                TValue *upval = cl->upvals[upval_index]->v;
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                settableProtected(L, upval, rb, rc);
                vmbreak;
            }
            vmcase(UOP_SETUPVAL) {
                auto upval_index = GETARG_B(i);
                lua_check_in_vm_error(upval_index < cl->nupvalues && upval_index>=0, "upvalue error");
                UpVal *uv = cl->upvals[upval_index];
                setobj(L, uv->v, ra);
                luaC_upvalbarrier(L, uv);
                vmbreak;
            }
            vmcase(UOP_SETTABLE) {
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                settableProtected(L, ra, rb, rc);
                vmbreak;
            }
            vmcase(UOP_NEWTABLE) {
                int b = GETARG_B(i);
                int c = GETARG_C(i);
                Table *t = luaH_new(L);
                sethvalue(L, ra, t);
                if (b != 0 || c != 0)
                    luaH_resize(L, t, luaO_fb2int(b), luaO_fb2int(c));
                checkGC(L, ra + 1);
                vmbreak;
            }
            vmcase(UOP_SELF) {
                const TValue *aux;
                StkId rb = RB(i);
                TValue *rc = RKC(i);
                TString *key = tsvalue(rc);  /* key must be a string */
                setobjs2s(L, ra + 1, rb);
                if (luaV_fastget(L, rb, key, aux, luaH_getstr)) {
                    setobj2s(L, ra, aux);
                }
                else
                {
                    Protect(luaV_finishget(L, rb, rc, ra, aux));
                }
                vmbreak;
            }
            vmcase(UOP_ADD) {
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                lua_Number nb; lua_Number nc;
                if (ttisinteger(rb) && ttisinteger(rc)) {
                    lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);
                    setivalue(ra, intop(+, ib, ic));
                }
                else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
                    setfltvalue(ra, luai_numadd(L, nb, nc));
                }
                else {
					luaG_runerror(L, "+ can only accept numbers");
                    Protect(luaT_trybinTM(L, rb, rc, ra, TM_ADD));
                }
                vmbreak;
            }
            vmcase(UOP_SUB) {
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                lua_Number nb; lua_Number nc;
                if (ttisinteger(rb) && ttisinteger(rc)) {
                    lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);
                    setivalue(ra, intop(-, ib, ic));
                }
                else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
                    setfltvalue(ra, luai_numsub(L, nb, nc));
                }
                else {
					luaG_runerror(L, "- can only accept numbers");
                    Protect(luaT_trybinTM(L, rb, rc, ra, TM_SUB));
                }
                vmbreak;
            }
            vmcase(UOP_MUL) {
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                lua_Number nb; lua_Number nc;
                if (ttisinteger(rb) && ttisinteger(rc)) {
                    lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);
                    setivalue(ra, intop(*, ib, ic));
                }
                else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
                    setfltvalue(ra, luai_nummul(L, nb, nc));
                }
                else {
					luaG_runerror(L, "* can only accept numbers");
                    Protect(luaT_trybinTM(L, rb, rc, ra, TM_MUL));
                }
                vmbreak;
            }
            vmcase(UOP_DIV) {  /* float division (always with floats) */
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                lua_Number nb; lua_Number nc;
                if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
                    setfltvalue(ra, luai_numdiv(L, nb, nc));
                }
                else {
					luaG_runerror(L, "/ can only accept numbers");
                    Protect(luaT_trybinTM(L, rb, rc, ra, TM_DIV));
                }
                vmbreak;
            }
            vmcase(UOP_BAND) {
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                lua_Integer ib; lua_Integer ic;
                if (tointeger(rb, &ib) && tointeger(rc, &ic)) {
                    setivalue(ra, intop(&, ib, ic));
                }
                else {
                    luaG_runerror(L, "& can only accept integer");
                    Protect(luaT_trybinTM(L, rb, rc, ra, TM_BAND));
                }
                vmbreak;
            }
            vmcase(UOP_BOR) {
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                lua_Integer ib; lua_Integer ic;
                if (tointeger(rb, &ib) && tointeger(rc, &ic)) {
                    setivalue(ra, intop(| , ib, ic));
                }
                else {
					luaG_runerror(L, "| can only accept integer");
                    Protect(luaT_trybinTM(L, rb, rc, ra, TM_BOR));
                }
                vmbreak;
            }
            vmcase(UOP_BXOR) {
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                lua_Integer ib; lua_Integer ic;
                if (tointeger(rb, &ib) && tointeger(rc, &ic)) {
                    setivalue(ra, intop(^, ib, ic));
                }
                else {
					luaG_runerror(L, "~ can only accept integer");
                    Protect(luaT_trybinTM(L, rb, rc, ra, TM_BXOR));
                }
                vmbreak;
            }
            vmcase(UOP_SHL) {
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                lua_Integer ib; lua_Integer ic;
                if (tointeger(rb, &ib) && tointeger(rc, &ic)) {
                    setivalue(ra, luaV_shiftl(ib, ic));
                }
                else {
					luaG_runerror(L, "<< can only accept integer");
                    Protect(luaT_trybinTM(L, rb, rc, ra, TM_SHL));
                }
                vmbreak;
            }
            vmcase(UOP_SHR) {
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                lua_Integer ib; lua_Integer ic;
                if (tointeger(rb, &ib) && tointeger(rc, &ic)) {
                    setivalue(ra, luaV_shiftl(ib, -ic));
                }
                else {
					luaG_runerror(L, ">> can only accept integer");
                    Protect(luaT_trybinTM(L, rb, rc, ra, TM_SHR));
                }
                vmbreak;
            }
            vmcase(UOP_MOD) {
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                lua_Number nb; lua_Number nc;
                if (ttisinteger(rb) && ttisinteger(rc)) {
                    lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);
                    setivalue(ra, luaV_mod(L, ib, ic));
                }
                else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
                    lua_Number m;
                    luai_nummod(L, nb, nc, m);
                    setfltvalue(ra, m);
                }
                else {
					luaG_runerror(L, "% can only accept numbers");
                    Protect(luaT_trybinTM(L, rb, rc, ra, TM_MOD));
                }
                vmbreak;
            }
            vmcase(UOP_IDIV) {  /* floor division */
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                lua_Number nb; lua_Number nc;
                if (ttisinteger(rb) && ttisinteger(rc)) {
                    lua_Integer ib = ivalue(rb); lua_Integer ic = ivalue(rc);
                    setivalue(ra, luaV_div(L, ib, ic));
                }
                else if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
                    setfltvalue(ra, luai_numidiv(L, nb, nc));
                }
                else {
					luaG_runerror(L, "// can only accept numbers");
                    Protect(luaT_trybinTM(L, rb, rc, ra, TM_IDIV));
                }
                vmbreak;
            }
            vmcase(UOP_POW) {
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                lua_Number nb; lua_Number nc;
                if (tonumber(rb, &nb) && tonumber(rc, &nc)) {
                    setfltvalue(ra, luai_numpow(L, nb, nc));
                }
                else {
					luaG_runerror(L, "^ can only accept numbers");
                    Protect(luaT_trybinTM(L, rb, rc, ra, TM_POW));
                }
                vmbreak;
            }
            vmcase(UOP_UNM) {
                TValue *rb = RB(i);
                lua_Number nb;
                if (ttisinteger(rb)) {
                    lua_Integer ib = ivalue(rb);
                    setivalue(ra, intop(-, 0, ib));
                }
                else if (tonumber(rb, &nb)) {
                    setfltvalue(ra, luai_numunm(L, nb));
                }
                else {
					luaG_runerror(L, "-<exp> can only accept numbers");
                    Protect(luaT_trybinTM(L, rb, rb, ra, TM_UNM));
                }
                vmbreak;
            }
            vmcase(UOP_BNOT) {
                TValue *rb = RB(i);
                lua_Integer ib;
                if (tointeger(rb, &ib)) {
                    setivalue(ra, intop(^, ~l_castS2U(0), ib));
                }
                else {
					luaG_runerror(L, "~<exp> can only accept numbers");
                    Protect(luaT_trybinTM(L, rb, rb, ra, TM_BNOT));
                }
                vmbreak;
            }
            vmcase(UOP_NOT) {
                TValue *rb = RB(i);
                int res = l_isfalse(rb);  /* next assignment may change this value */
                setbvalue(ra, res);
                vmbreak;
            }
            vmcase(UOP_LEN) {
                Protect(luaV_objlen(L, ra, RB(i)));
                vmbreak;
            }
            vmcase(UOP_CONCAT) {
                int b = GETARG_B(i);
                int c = GETARG_C(i);
                StkId rb;
                L->top = base + c + 1;  /* mark the end of concat operands */
                Protect(luaV_concat(L, c - b + 1));
                ra = RA(i);  /* 'luaV_concat' may invoke TMs and move the stack */
                rb = base + b;
                setobjs2s(L, ra, rb);
                checkGC(L, (ra >= rb ? ra + 1 : rb));
                L->top = ci->top;  /* restore top */
                vmbreak;
            }
            vmcase(UOP_JMP) {
                dojump(ci, i, 0); // maybe only `goto` source code line is compiled to UOP_JMP opcode line
                // uvm_api_lua_throw_exception(L, UVM_API_LVM_ERROR, "uvm lua not support goto symbol");
                vmbreak;
            }
            vmcase(UOP_EQ) {
                TValue *rb = RKB(i);
                TValue *rc = RKC(i);
                Protect(
                    if (luaV_equalobj(L, rb, rc) != GETARG_A(i))
                        ci->u.l.savedpc++;
                    else
                        donextjump(ci);
                )
                    vmbreak;
            }
            vmcase(UOP_LT) {
                Protect(
                    if (luaV_lessthan(L, RKB(i), RKC(i)) != GETARG_A(i))
                        ci->u.l.savedpc++;
                    else
                        donextjump(ci);
                )
                    vmbreak;
            }
            vmcase(UOP_LE) {
                Protect(
                    if (luaV_lessequal(L, RKB(i), RKC(i)) != GETARG_A(i))
                        ci->u.l.savedpc++;
                    else
                        donextjump(ci);
                )
                    vmbreak;
            }
            vmcase(UOP_TEST) {
                if (GETARG_C(i) ? l_isfalse(ra) : !l_isfalse(ra))
                    ci->u.l.savedpc++;
                else
                    donextjump(ci);
                vmbreak;
            }
            vmcase(UOP_TESTSET) {
                TValue *rb = RB(i);
                if (GETARG_C(i) ? l_isfalse(rb) : !l_isfalse(rb))
                    ci->u.l.savedpc++;
                else {
                    setobjs2s(L, ra, rb);
                    donextjump(ci);
                }
                vmbreak;
            }
            vmcase(UOP_CALL) {
                int b = GETARG_B(i);
                int nresults = GETARG_C(i) - 1;
                if (b != 0) L->top = ra + b;  /* else previous instruction set top */
				// int line_in_proto = get_line_in_current_proto(ci, cl->p);
                if (luaD_precall(L, ra, nresults)) {  /* C function? */
                    if (nresults >= 0)
                        L->top = ci->top;  /* adjust results */
                    Protect((void)0);  /* update 'base' */
                }
                else {  /* Lua function */
                    if (L->force_stopping)
                        vmbreak;
                    ci = L->ci;
                    goto newframe;  /* restart luaV_execute over new Lua function */
                }
                vmbreak;
            }
            vmcase(UOP_TAILCALL) {
                int b = GETARG_B(i);
                if (b != 0) L->top = ra + b;  /* else previous instruction set top */
                lua_assert(GETARG_C(i) - 1 == LUA_MULTRET);
                if (luaD_precall(L, ra, LUA_MULTRET)) {  /* C function? */
                    Protect((void)0);  /* update 'base' */
                }
                else {
                    /* tail call: put called frame (n) in place of caller one (o) */
                    CallInfo *nci = L->ci;  /* called frame */
                    CallInfo *oci = nci->previous;  /* caller frame */
                    StkId nfunc = nci->func;  /* called function */
                    StkId ofunc = oci->func;  /* caller function */
                    /* last stack slot filled by 'precall' */
                    StkId lim = nci->u.l.base + getproto(nfunc)->numparams;
                    int aux;
                    /* close all upvalues from previous call */
                    if (cl->p->sizep > 0) luaF_close(L, oci->u.l.base);
                    /* move new frame into old one */
                    for (aux = 0; nfunc + aux < lim; aux++)
                        setobjs2s(L, ofunc + aux, nfunc + aux);
                    oci->u.l.base = ofunc + (nci->u.l.base - nfunc);  /* correct base */
                    oci->top = L->top = ofunc + (L->top - nfunc);  /* correct top */
                    oci->u.l.savedpc = nci->u.l.savedpc;
                    oci->callstatus |= CIST_TAIL;  /* function was tail called */
                    ci = L->ci = oci;  /* remove new frame */
                    lua_assert(L->top == oci->u.l.base + getproto(ofunc)->maxstacksize);
                    if (L->force_stopping)
                        vmbreak;
                    goto newframe;  /* restart luaV_execute over new Lua function */
                }
                vmbreak;
            }
            vmcase(UOP_RETURN) {
                int a = GETARG_A(i);
                int b = GETARG_B(i);
                int top = lua_gettop(L);
                // return R(a), R(a+1), ... , R(a+b-2), b is return result count + 1, index from 0
                if (use_last_return && b > 1 && top >= a + 1)
                {
                    lua_getglobal(L, "_G");
                    lua_pushvalue(L, a + 1);
                    lua_setfield(L, -2, "last_return");
                    lua_pop(L, 1);
                }
                if (cl->p->sizep > 0) luaF_close(L, base);
                b = luaD_poscall(L, ci, ra, (b != 0 ? b - 1 : cast_int(L->top - ra)));

                if (ci->callstatus & CIST_FRESH)  /* local 'ci' still from callee */
                    return;  /* external invocation: return */
                else {  /* invocation via reentry: continue execution */
                    ci = L->ci;
                    if (b) L->top = ci->top;
                    lua_assert(isLua(ci));
                    lua_assert(GET_OPCODE(*((ci)->u.l.savedpc - 1)) == UOP_CALL);
                    if (L->force_stopping)
                        vmbreak;
                    goto newframe;  /* restart luaV_execute over new Lua function */
                }
            }
            vmcase(UOP_FORLOOP) {
                if (ttisinteger(ra)) {  /* integer loop? */
                    lua_Integer step = ivalue(ra + 2);
                    lua_Integer idx = intop(+, ivalue(ra), step); /* increment index */
                    lua_Integer limit = ivalue(ra + 1);
                    if ((0 < step) ? (idx <= limit) : (limit <= idx)) {
                        ci->u.l.savedpc += GETARG_sBx(i);  /* jump back */
                        chgivalue(ra, idx);  /* update internal index... */
                        setivalue(ra + 3, idx);  /* ...and external index */
                    }
                }
                else {  /* floating loop */
                    lua_Number step = fltvalue(ra + 2);
                    lua_Number idx = luai_numadd(L, fltvalue(ra), step); /* inc. index */
                    lua_Number limit = fltvalue(ra + 1);
                    if (luai_numlt(0, step) ? luai_numle(idx, limit)
                        : luai_numle(limit, idx)) {
                        ci->u.l.savedpc += GETARG_sBx(i);  /* jump back */
                        chgfltvalue(ra, idx);  /* update internal index... */
                        setfltvalue(ra + 3, idx);  /* ...and external index */
                    }
                }
                vmbreak;
            }
            vmcase(UOP_FORPREP) {
                TValue *init = ra;
                TValue *plimit = ra + 1;
                TValue *pstep = ra + 2;
                lua_Integer ilimit;
                int stopnow;
                if (ttisinteger(init) && ttisinteger(pstep) &&
                    forlimit(plimit, &ilimit, ivalue(pstep), &stopnow)) {
                    /* all values are integer */
                    lua_Integer initv = (stopnow ? 0 : ivalue(init));
                    setivalue(plimit, ilimit);
                    setivalue(init, intop(-, initv, ivalue(pstep)));
                }
                else {  /* try making all values floats */
                    lua_Number ninit; lua_Number nlimit; lua_Number nstep;
                    if (!tonumber(plimit, &nlimit))
                        luaG_runerror(L, "'for' limit must be a number");
                    setfltvalue(plimit, nlimit);
                    if (!tonumber(pstep, &nstep))
                        luaG_runerror(L, "'for' step must be a number");
                    setfltvalue(pstep, nstep);
                    if (!tonumber(init, &ninit))
                        luaG_runerror(L, "'for' initial value must be a number");
                    setfltvalue(init, luai_numsub(L, ninit, nstep));
                }
                ci->u.l.savedpc += GETARG_sBx(i);
                vmbreak;
            }
            vmcase(UOP_TFORCALL) {
                StkId cb = ra + 3;  /* call base */
                setobjs2s(L, cb + 2, ra + 2);
                setobjs2s(L, cb + 1, ra + 1);
                setobjs2s(L, cb, ra);
                L->top = cb + 3;  /* func. + 2 args (state and index) */
                Protect(luaD_call(L, cb, GETARG_C(i)));
                L->top = ci->top;
                i = *(ci->u.l.savedpc++);  /* go to next instruction */
                ra = RA(i);
                lua_assert(GET_OPCODE(i) == UOP_TFORLOOP);
                goto l_tforloop;
            }
            vmcase(UOP_TFORLOOP) {
            l_tforloop:
                if (!ttisnil(ra + 1)) {  /* continue loop? */
                    setobjs2s(L, ra, ra + 1);  /* save control variable */
                    ci->u.l.savedpc += GETARG_sBx(i);  /* jump back */
                }
                vmbreak;
            }
            vmcase(UOP_SETLIST) {
                int n = GETARG_B(i);
                int c = GETARG_C(i);
                unsigned int last;
                Table *h;
                if (n == 0) n = cast_int(L->top - ra) - 1;
                if (c == 0) {
                    lua_assert(GET_OPCODE(*ci->u.l.savedpc) == UOP_EXTRAARG);
                    c = GETARG_Ax(*ci->u.l.savedpc++);
                }
                h = hvalue(ra);
                last = ((c - 1)*LFIELDS_PER_FLUSH) + n;
                if (last > h->sizearray)  /* needs more space? */
                    luaH_resizearray(L, h, last);  /* preallocate it at once */
                for (; n > 0; n--) {
                    TValue *val = ra + n;
                    luaH_setint(L, h, last--, val);
                    luaC_barrierback(L, h, val);
                }
                L->top = ci->top;  /* correct top (in case of previous open call) */
                vmbreak;
            }
            vmcase(UOP_CLOSURE) {
                auto p_index = GETARG_Bx(i);
                lua_check_in_vm_error(p_index < cl->p->sizep, "too large sub proto index");
                Proto *p = cl->p->p[p_index];
                LClosure *ncl = getcached(p, cl->upvals, base);  /* cached closure */
                if (ncl == nullptr)  /* no match? */
                    pushclosure(L, p, cl->upvals, cl->nupvalues, base, ra);  /* create a new one */
                else
                    setclLvalue(L, ra, ncl);  /* push cashed closure */
                checkGC(L, ra + 1);
                vmbreak;
            }
            vmcase(UOP_VARARG) {
                int b = GETARG_B(i) - 1;  /* required results */
                int j;
                int n = cast_int(base - ci->func) - cl->p->numparams - 1;
                if (n < 0)  /* less arguments than parameters? */
                    n = 0;  /* no vararg arguments */
                if (b < 0) {  /* B == 0? */
                    b = n;  /* get all var. arguments */
                    Protect(luaD_checkstack(L, n));
                    ra = RA(i);  /* previous call may change the stack */
                    L->top = ra + n;
                }
                for (j = 0; j < b && j < n; j++)
                    setobjs2s(L, ra + j, base - n + j);
                for (; j < b; j++)  /* complete required results with nil */
                    setnilvalue(ra + j);
                vmbreak;
            }
            vmcase(UOP_EXTRAARG) {
                lua_assert(0);
                vmbreak;
            }

			vmcase(UOP_PUSH) {
				if (L->evalstacktop - L->evalstack >= L->evalstacksize) {
					//  ............
					luaG_runerror(L, "evalstack exceed");
					vmbreak;
				}
				setobj(L, L->evalstacktop, ra);
				L->evalstacktop += 1;
				vmbreak;
			}
			vmcase(UOP_POP) {
				if (L->evalstacktop <= L->evalstack) {
					luaG_runerror(L, "evalstack exceed when pop");
					vmbreak;
				}
				setobj(L, ra, L->evalstacktop - 1);
				L->evalstacktop -= 1;
				vmbreak;
			}
			vmcase(UOP_GETTOP) {
				if (L->evalstacktop <= L->evalstack) {
					lua_pushnil(L);
					vmbreak;
				}
				setobj(L, ra, L->evalstacktop-1);
				vmbreak;
			}
			vmcase(UOP_CMP) {
				TValue *rb = RKB(i);
				TValue *rc = RKC(i);
				Protect(
					if (luaV_equalobj(L, rb, rc)) {
						setivalue(ra, 0);
					}
					else if (!luaV_lessthan(L, rb, rc)) {
						setivalue(ra, 1);
					}
					else {
						setivalue(ra, -1);
					}
				)
				vmbreak;
			}
			vmcase(UOP_CMP_EQ) {
				TValue *rb = RKB(i);
				TValue *rc = RKC(i);
				Protect(
					if (luaV_equalobj(L, rb, rc)) {
						setivalue(ra, 1);
					}
					else {
						setivalue(ra, 0);
					}
				)
				vmbreak;
			}
			vmcase(UOP_CMP_NE) {
				TValue *rb = RKB(i);
				TValue *rc = RKC(i);
				Protect(
					if (luaV_equalobj(L, rb, rc)) {
						setivalue(ra, 0);
					}
					else {
						setivalue(ra, 1);
					}
				)
					vmbreak;
			}
			vmcase(UOP_CMP_GT) {
				TValue *rb = RKB(i);
				TValue *rc = RKC(i);
				Protect(
					if (luaV_lessthan(L, rb, rc) || luaV_equalobj(L, rb, rc)) {
						setivalue(ra, 0);
					}
					else {
						setivalue(ra, 1);
					}
					)
					vmbreak;
			}
			vmcase(UOP_CMP_LT) {
				TValue *rb = RKB(i);
				TValue *rc = RKC(i);
				Protect(
					if (luaV_lessthan(L, rb, rc)) {
						setivalue(ra, 1);
					}
					else {
						setivalue(ra, 0);
					}
					)
					vmbreak;
			}
        }
    }
}

/* }================================================================== */

