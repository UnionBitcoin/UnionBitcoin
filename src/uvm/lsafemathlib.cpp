#define lsafemathlib_cpp

#include "uvm/lprefix.h"


#include <stdlib.h>
#include <math.h>

#include <uvm/lua.h>

#include <uvm/lauxlib.h>
#include <uvm/lualib.h>
#include <uvm/lobject.h>
#include <boost/algorithm/hex.hpp>
#include <boost/multiprecision/cpp_int.hpp>
//#include <boost/multiprecision/cpp_dec_float.hpp>
//#include <boost/multiprecision/mpfi.hpp>
//#include <boost/multiprecision/mpfr.hpp>

// bigint stores as { hex: hex string, type: 'bigint' }
// bignumber stores as { value: base10 string, type: 'bignumber' }

typedef boost::multiprecision::int256_t sm_bigint;
//typedef boost::multiprecision::mpf_float sm_bigdecimal;


static void push_bigint(lua_State *L, sm_bigint value) {
	auto hex_str = boost::algorithm::hex(value.str());
	lua_newtable(L);
	lua_pushstring(L, hex_str.c_str());
	lua_setfield(L, -2, "hex");
	lua_pushstring(L, "bigint");
	lua_setfield(L, -2, "type");
}

//static void push_bignumber(lua_State *L, sm_bigdecimal value) {
//	auto value_str = value.str();
//	lua_newtable(L);
//	lua_pushstring(L, value_str.c_str());
//	lua_setfield(L, -2, "value");
//	lua_pushstring(L, "bignumber");
//	lua_setfield(L, -2, "type");
//}

static int safemath_bigint(lua_State *L) {
	if (lua_gettop(L) < 1) {
		luaL_argcheck(L, false, 1, "argument is empty");
		return 0;
	}
	if (lua_isinteger(L, 1)) {
		lua_Integer n = lua_tointeger(L, 1);
		auto value_hex = boost::algorithm::hex(std::to_string(n));
		lua_newtable(L);
		lua_pushstring(L, value_hex.c_str());
		lua_setfield(L, -2, "hex");
		lua_pushstring(L, "bigint");
		lua_setfield(L, -2, "type");
		return 1;
	}
	else if (lua_isstring(L, 1)) {
		std::string int_str = luaL_checkstring(L, 1);
		std::string value_hex;
		try {
			value_hex = boost::algorithm::hex(int_str);
		}
		catch (const std::exception& e) {
			luaL_error(L, "invalid int string");
			return 0;
		}
		lua_newtable(L);
		lua_pushstring(L, value_hex.c_str());
		lua_setfield(L, -2, "hex");
		lua_pushstring(L, "bigint");
		lua_setfield(L, -2, "type");
		return 1;
	}
	else {
		luaL_argcheck(L, false, 1, "first argument must be integer or hex string");
		return 0;
	}
	return 1;
}

//static int safemath_bignumber(lua_State *L) {
//	if (lua_gettop(L) < 1) {
//		luaL_argcheck(L, false, 1, "argument is empty");
//		return 0;
//	}
//	if (lua_isinteger(L, 1)) {
//		lua_Integer n = lua_tointeger(L, 1);
//		sm_bigdecimal sm_value(n);
//		push_bignumber(L, sm_value);
//		return 1;
//	}
//	else if (lua_type(L, 1) == LUA_TNUMBER || lua_type(L, 1) == LUA_TNUMFLT) {
//		lua_Number n = lua_tonumber(L, 1);
//		sm_bigdecimal sm_value(n);
//		push_bignumber(L, sm_value);
//		return 1;
//	}
//	else if (lua_isstring(L, 1)) {
//		std::string value_str = luaL_checkstring(L, 1);
//		try {
//			sm_bigdecimal sm_value(value_str);
//			push_bignumber(L, sm_value);
//			return 1;
//		}
//		catch (const std::exception& e) {
//			luaL_error(L, "invalid number string");
//			return 0;
//		}
//	}
//	else {
//		luaL_argcheck(L, false, 1, "first argument must be number or hex string");
//		return 0;
//	}
//	return 1;
//}


static bool is_valid_bigint_obj(lua_State *L, int index, std::string& out)
{
	if ((index > 0 && lua_gettop(L) < index) || (index < 0 && lua_gettop(L) < -index) || index == 0) {
		return false;
	}
	if (!lua_istable(L, index)) {
		return false;
	}
	lua_getfield(L, index, "type");
	if (lua_isnil(L, -1) || !lua_isstring(L, -1) || strcmp("bigint", luaL_checkstring(L, -1)) != 0) {
		lua_pop(L, 1);
		return false;
	}
	lua_pop(L, 1);
	lua_getfield(L, index, "hex");
	if (lua_isnil(L, -1) || !lua_isstring(L, -1) || strlen(luaL_checkstring(L, -1)) < 1) {
		lua_pop(L, 1);
		return false;
	}
	std::string hex_str = luaL_checkstring(L, -1);
	lua_pop(L, 1);
	try {
		boost::algorithm::unhex(hex_str);
		out = hex_str;
		return true;
	}
	catch (const std::exception& e) {
		return false;
	}
}

//static bool is_valid_bignumber_obj(lua_State *L, int index, std::string& out)
//{
//	if ((index > 0 && lua_gettop(L) < index) || (index < 0 && lua_gettop(L) < -index) || index == 0) {
//		return false;
//	}
//	if (!lua_istable(L, index)) {
//		return false;
//	}
//	lua_getfield(L, index, "type");
//	if (lua_isnil(L, -1) || !lua_isstring(L, -1) || strcmp("bignumber", luaL_checkstring(L, -1)) != 0) {
//		lua_pop(L, 1);
//		return false;
//	}
//	lua_pop(L, 1);
//	lua_getfield(L, index, "value");
//	if (lua_isnil(L, -1) || !lua_isstring(L, -1) || strlen(luaL_checkstring(L, -1)) < 1) {
//		lua_pop(L, 1);
//		return false;
//	}
//	std::string value_str = luaL_checkstring(L, -1);
//	lua_pop(L, 1);
//	try {
//		sm_bigdecimal(hex_str);
//		out = value_str;
//		return true;
//	}
//	catch (const std::exception& e) {
//		return false;
//	}
//}

static bool is_same_direction_int256(sm_bigint a, sm_bigint b)
{
	return (a > 0 && b > 0) || (a < 0 && b < 0);
}

static bool is_same_direction_int512(boost::multiprecision::int512_t a, boost::multiprecision::int512_t b)
{
	return (a > 0 && b > 0) || (a < 0 && b < 0);
}

static int safemath_add(lua_State *L) {
	if (lua_gettop(L) < 2) {
		luaL_error(L, "add need at least 2 argument");
	}
	std::string first_hex_str;
	std::string second_hex_str;
	if (!is_valid_bigint_obj(L, 1, first_hex_str)) {
		luaL_argcheck(L, false, 1, "invalid bigint obj");
	}
	if (!is_valid_bigint_obj(L, 2, second_hex_str)) {
		luaL_argcheck(L, false, 2, "invalid bigint obj");
	}
	auto first_int_str = boost::algorithm::unhex(first_hex_str);
	sm_bigint first_int(first_int_str);
	auto second_int_str = boost::algorithm::unhex(second_hex_str);
	sm_bigint second_int(second_int_str);
	auto result_int = first_int + second_int;
	// overflow check
	if (is_same_direction_int256(first_int, second_int)) {
		if ((first_int > 0 && result_int <= 0) || (first_int<0 && result_int >= 0)) {
			luaL_error(L, "int256 overflow");
		}
	}
	push_bigint(L, result_int);
	return 1;
}

//static int safemath_add(lua_State *L) {
//	if (lua_gettop(L) < 2) {
//		luaL_error(L, "add need at least 2 argument");
//	}
//	std::string first_value_str;
//	std::string second_value_str;
//	if (!is_valid_bignumber_obj(L, 1, first_value_str)) {
//		luaL_argcheck(L, false, 1, "invalid bignumber obj");
//	}
//	if (!is_valid_bignumber_obj(L, 2, second_value_str)) {
//		luaL_argcheck(L, false, 2, "invalid bignumber obj");
//	}
//	sm_bigdecimal first_value(first_value_str);
//	sm_bigdecimal second_value(second_value_str);
//	auto result_value = first_value + second_value;
//	push_bignumber(L, result_value);
//	return 1;
//}

static int safemath_mul(lua_State *L) {
	if (lua_gettop(L) < 2) {
		luaL_error(L, "mul need at least 2 argument");
	}
	std::string first_hex_str;
	std::string second_hex_str;
	if (!is_valid_bigint_obj(L, 1, first_hex_str)) {
		luaL_argcheck(L, false, 1, "invalid bigint obj");
	}
	if (!is_valid_bigint_obj(L, 2, second_hex_str)) {
		luaL_argcheck(L, false, 2, "invalid bigint obj");
	}
	auto first_int_str = boost::algorithm::unhex(first_hex_str);
	sm_bigint first_int(first_int_str);
	auto second_int_str = boost::algorithm::unhex(second_hex_str);
	sm_bigint second_int(second_int_str);
	auto result_int = boost::multiprecision::int512_t(first_int) * boost::multiprecision::int512_t(second_int);
	// overflow check
	sm_bigint int256_max("57896044618658097711785492504343953926634992332820282019728792003956564819968");
	sm_bigint int256_min("-115792089237316195423570985008687907853269984665640564039457584007913129639935");
	if (is_same_direction_int256(first_int, second_int) && result_int > int256_max) {
		luaL_error(L, "int256 overflow");
	}
	else if (is_same_direction_int256(first_int, second_int) && result_int < int256_min) {
		luaL_error(L, "int256 overflow");
	}
	push_bigint(L, result_int.convert_to<sm_bigint>());
	return 1;
}

//static int safemath_mul(lua_State *L) {
//	if (lua_gettop(L) < 2) {
//		luaL_error(L, "mul need at least 2 argument");
//	}
//	std::string first_value_str;
//	std::string second_value_str;
//	if (!is_valid_bignumber_obj(L, 1, first_value_str)) {
//		luaL_argcheck(L, false, 1, "invalid bignumber obj");
//	}
//	if (!is_valid_bignumber_obj(L, 2, second_value_str)) {
//		luaL_argcheck(L, false, 2, "invalid bignumber obj");
//	}
//	sm_bigdecimal first_value(first_value_str);
//	sm_bigdecimal second_value(second_value_str);
//	auto result_value = first_value * second_value;
//	push_bignumber(L, result_value);
//	return 1;
//}

static sm_bigint int256_pow(lua_State *L, sm_bigint value, sm_bigint n) {
	if (n > 100) {
		luaL_error(L, "too large value in bigint pow");
	}
	boost::multiprecision::int512_t result(value);
	boost::multiprecision::int256_t int256_max("57896044618658097711785492504343953926634992332820282019728792003956564819968");
	boost::multiprecision::int256_t int256_min("-115792089237316195423570985008687907853269984665640564039457584007913129639935");
	for (int i = 1; i < n; i++) {
		auto mid_value = result * value;
		// overflow check
		if (is_same_direction_int512(result, value) && mid_value > int256_max) {
			luaL_error(L, "int256 overflow");
		}
		else if(!is_same_direction_int512(result, value) && mid_value < int256_min) {
			luaL_error(L, "int256 overflow");
		}
		result = mid_value;
	}
	return result.convert_to<boost::multiprecision::int256_t>();
}

//static sm_bigdecimal bignumber_pow(lua_State *L, sm_bigdecimal value, sm_bigdecimal n) {
//	if (n > 100) {
//		luaL_error(L, "too large value in bignumber pow");
//	}
//	sm_bigdecimal result(value);
//	for (int i = 1; i < n; i++) {
//		auto mid_value = result * value;
//		result = mid_value;
//	}
//	return result;
//}

static int safemath_pow(lua_State *L) {
	if (lua_gettop(L) < 2) {
		luaL_error(L, "pow need at least 2 argument");
	}
	std::string first_hex_str;
	std::string second_hex_str;
	if (!is_valid_bigint_obj(L, 1, first_hex_str)) {
		luaL_argcheck(L, false, 1, "invalid bigint obj");
	}
	if (!is_valid_bigint_obj(L, 2, second_hex_str)) {
		luaL_argcheck(L, false, 2, "invalid bigint obj");
	}
	auto first_int_str = boost::algorithm::unhex(first_hex_str);
	sm_bigint first_int(first_int_str);
	auto second_int_str = boost::algorithm::unhex(second_hex_str);
	sm_bigint second_int(second_int_str);
	auto result_int = int256_pow(L, first_int, second_int);
	// overflow check
	if (result_int <= 0) {
		luaL_error(L, "int256 overflow");
	}
	push_bigint(L, result_int);
	return 1;
}

enum compare_type {
	GT,
	GE,
	LT,
	LE,
	EQ,
	NE
};

static int _safemath_compare(lua_State* L, compare_type type) {
	if (lua_gettop(L) < 2) {
		luaL_error(L, "pow need at least 2 argument");
	}
	std::string first_hex_str;
	std::string second_hex_str;
	if (!is_valid_bigint_obj(L, 1, first_hex_str)) {
		luaL_argcheck(L, false, 1, "invalid bigint obj");
	}
	if (!is_valid_bigint_obj(L, 2, second_hex_str)) {
		luaL_argcheck(L, false, 2, "invalid bigint obj");
	}
	auto first_int_str = boost::algorithm::unhex(first_hex_str);
	sm_bigint first_int(first_int_str);
	auto second_int_str = boost::algorithm::unhex(second_hex_str);
	sm_bigint second_int(second_int_str);
	bool result = false;
	if ( (type==compare_type::GT && first_int > second_int)
		|| (type == compare_type::GE && first_int >= second_int)
		|| (type == compare_type::LT && first_int < second_int)
		|| (type == compare_type::LE && first_int <= second_int)
		|| (type == compare_type::EQ && first_int == second_int)
		|| (type == compare_type::NE && first_int != second_int)
		){
		result = true;
	}
	else {
		result = false;
	}
	lua_pushboolean(L, result);
	return 1;
}

static int safemath_gt(lua_State* L) {
	return _safemath_compare(L, compare_type::GT);
}
static int safemath_ge(lua_State* L) {
	return _safemath_compare(L, compare_type::GE);
}
static int safemath_lt(lua_State* L) {
	return _safemath_compare(L, compare_type::LT);
}
static int safemath_le(lua_State* L) {
	return _safemath_compare(L, compare_type::LE);
}
static int safemath_eq(lua_State* L) {
	return _safemath_compare(L, compare_type::EQ);
}
static int safemath_ne(lua_State* L) {
	return _safemath_compare(L, compare_type::NE);
}

//static int safemath_pow(lua_State *L) {
//	if (lua_gettop(L) < 2) {
//		luaL_error(L, "pow need at least 2 argument");
//	}
//	std::string first_value_str;
//	std::string second_value_str;
//	if (!is_valid_bignumber_obj(L, 1, first_value_str)) {
//		luaL_argcheck(L, false, 1, "invalid bignumber obj");
//	}
//	if (!is_valid_bignumber_obj(L, 2, second_value_str)) {
//		luaL_argcheck(L, false, 2, "invalid bignumber obj");
//	}
//	sm_bigdecimal first_value(first_value_str);
//	sm_bigdecimal second_value(second_value_str);
//	auto result_value = bignumber_pow(L, first_value, second_value);
//	push_bignumber(L, result_value);
//	return 1;
//}

static int safemath_div(lua_State *L) {
	if (lua_gettop(L) < 2) {
		luaL_error(L, "div need at least 2 argument");
	}
	std::string first_hex_str;
	std::string second_hex_str;
	if (!is_valid_bigint_obj(L, 1, first_hex_str)) {
		luaL_argcheck(L, false, 1, "invalid bigint obj");
	}
	if (!is_valid_bigint_obj(L, 2, second_hex_str)) {
		luaL_argcheck(L, false, 2, "invalid bigint obj");
	}
	auto first_int_str = boost::algorithm::unhex(first_hex_str);
	sm_bigint first_int(first_int_str);
	auto second_int_str = boost::algorithm::unhex(second_hex_str);
	sm_bigint second_int(second_int_str);
	if (second_int.is_zero()) {
		luaL_error(L, "div by 0 error");
	}
	sm_bigint result_int;
	try {
		result_int = first_int / second_int;
	}
	catch (const std::exception& e) {
		luaL_error(L, "bigint divide error");
	}
	// overflow check
	if (is_same_direction_int256(first_int, second_int)) {
		if ((first_int > 0 && result_int <= 0) || (first_int<0 && result_int >= 0)) {
			luaL_error(L, "int256 overflow");
		}
	}
	push_bigint(L, result_int);
	return 1;
}

//static int safemath_div(lua_State *L) {
//	if (lua_gettop(L) < 2) {
//		luaL_error(L, "div need at least 2 argument");
//	}
//	std::string first_value_str;
//	std::string second_value_str;
//	if (!is_valid_bignumber_obj(L, 1, first_value_str)) {
//		luaL_argcheck(L, false, 1, "invalid bignumber obj");
//	}
//	if (!is_valid_bignumber_obj(L, 2, second_value_str)) {
//		luaL_argcheck(L, false, 2, "invalid bignumber obj");
//	}
//	sm_bigdecimal first_value(first_value_str);
//	sm_bigdecimal second_value(second_value_str);
//	if (second_value.is_zero()) {
//		luaL_error(L, "div by 0 error");
//	}
//	sm_bigdecimal result_value;
//	try {
//		result_value = first_value / second_value;
//	}
//	catch (const std::exception& e) {
//		luaL_error(L, "bignumber divid by zero error");
//	}
//	push_bignumber(L, result_value);
//	return 1;
//}

static int safemath_rem(lua_State *L) {
	if (lua_gettop(L) < 2) {
		luaL_error(L, "rem need at least 2 argument");
	}
	std::string first_hex_str;
	std::string second_hex_str;
	if (!is_valid_bigint_obj(L, 1, first_hex_str)) {
		luaL_argcheck(L, false, 1, "invalid bigint obj");
	}
	if (!is_valid_bigint_obj(L, 2, second_hex_str)) {
		luaL_argcheck(L, false, 2, "invalid bigint obj");
	}
	auto first_int_str = boost::algorithm::unhex(first_hex_str);
	sm_bigint first_int(first_int_str);
	auto second_int_str = boost::algorithm::unhex(second_hex_str);
	sm_bigint second_int(second_int_str);
	if (second_int == 0) {
		luaL_error(L, "rem by 0 error");
	}
	auto result_int = first_int % second_int;
	// overflow check
	if (is_same_direction_int256(first_int, second_int)) {
		if ((first_int > 0 && result_int <= 0) || (first_int<0 && result_int >= 0)) {
			luaL_error(L, "int256 overflow");
		}
	}
	push_bigint(L, result_int);
	return 1;
}

//static int safemath_rem(lua_State *L) {
//	if (lua_gettop(L) < 2) {
//		luaL_error(L, "rem need at least 2 argument");
//	}
//	std::string first_value_str;
//	std::string second_value_str;
//	if (!is_valid_bignumber_obj(L, 1, first_value_str)) {
//		luaL_argcheck(L, false, 1, "invalid bignumber obj");
//	}
//	if (!is_valid_bignumber_obj(L, 2, second_value_str)) {
//		luaL_argcheck(L, false, 2, "invalid bignumber obj");
//	}
//	sm_bigdecimal first_value(first_value_str);
//	sm_bigdecimal second_value(second_value_str);
//	if (second_value == 0) {
//		luaL_error(L, "rem by 0 error");
//	}
//	auto result_int = first_value.convert_to<sm_bigint>() % second_value.convert_to<sm_bigint>();
//	sm_bigdecimal result_value(result_int);
//	push_bignumber(L, result_value);
//	return 1;
//}

static int safemath_sub(lua_State *L) {
	if (lua_gettop(L) < 2) {
		luaL_error(L, "sub need at least 2 argument");
	}
	std::string first_hex_str;
	std::string second_hex_str;
	if (!is_valid_bigint_obj(L, 1, first_hex_str)) {
		luaL_argcheck(L, false, 1, "invalid bigint obj");
	}
	if (!is_valid_bigint_obj(L, 2, second_hex_str)) {
		luaL_argcheck(L, false, 2, "invalid bigint obj");
	}
	auto first_int_str = boost::algorithm::unhex(first_hex_str);
	sm_bigint first_int(first_int_str);
	auto second_int_str = boost::algorithm::unhex(second_hex_str);
	sm_bigint second_int(second_int_str);
	auto result_int = first_int - second_int;
	// overflow check
	if (!is_same_direction_int256(first_int, second_int)) {
		if ((first_int > 0 && result_int <= 0) || (first_int<0 && result_int >= 0)) {
			luaL_error(L, "int256 overflow");
		}
	}
	push_bigint(L, result_int);
	return 1;
}

//static int safemath_sub(lua_State *L) {
//	if (lua_gettop(L) < 2) {
//		luaL_error(L, "sub need at least 2 argument");
//	}
//	std::string first_value_str;
//	std::string second_value_str;
//	if (!is_valid_bignumber_obj(L, 1, first_value_str)) {
//		luaL_argcheck(L, false, 1, "invalid bignumber obj");
//	}
//	if (!is_valid_bignumber_obj(L, 2, second_value_str)) {
//		luaL_argcheck(L, false, 2, "invalid bignumber obj");
//	}
//	sm_bigdecimal first_value(first_value_str);
//	sm_bigdecimal second_value(second_value_str);
//	auto result_value = first_value - second_value;
//	push_bignumber(L, result_value);
//	return 1;
//}

static int safemath_toint(lua_State* L) {
	std::string hex_str;
	if (!is_valid_bigint_obj(L, 1, hex_str)) {
		luaL_argcheck(L, false, 1, "invalid bigint object");
	}
	auto value_str = boost::algorithm::unhex(hex_str);
	sm_bigint bigint_value(value_str);
	auto value = bigint_value.convert_to<lua_Integer>();
	lua_pushinteger(L, value);
	return 1;
}

//static int safemath_toint(lua_State* L) {
//	std::string value_str;
//	if (!is_valid_bignumber_obj(L, 1, value_str)) {
//		luaL_argcheck(L, false, 1, "invalid bignumber object");
//	}
//	sm_bigdecimal big_value(value_str);
//	auto value = big_value.convert_to<lua_Integer>();
//	lua_pushinteger(L, value);
//	return 1;
//}

static int safemath_tohex(lua_State* L) {
	std::string hex_str;
	if (!is_valid_bigint_obj(L, 1, hex_str)) {
		luaL_argcheck(L, false, 1, "invalid bigint object");
	}
	lua_pushstring(L, hex_str.c_str());
	return 1;
}

//static int safemath_tohex(lua_State* L) {
//	std::string value_str;
//	if (!is_valid_bignumber_obj(L, 1, value_str)) {
//		luaL_argcheck(L, false, 1, "invalid bignumber object");
//	}
//	lua_pushstring(L, value_str.c_str());
//	return 1;
//}

static int safemath_tostring(lua_State* L) {
	std::string hex_str;
	if (is_valid_bigint_obj(L, 1, hex_str)) {
		auto value_str = boost::algorithm::unhex(hex_str);
		sm_bigint bigint_value(value_str);
		auto bigint_value_str = bigint_value.str();
		lua_pushstring(L, bigint_value_str.c_str());
		return 1;
	}
	/*else if (is_valid_bignumber_obj(L, 1, hex_str)) {
		auto value_str = hex_str;
		lua_pushstring(L, value_str.c_str());
		return 1;
	}*/
	else {
		luaL_error(L, "invalid bignumber value");
		return 0;
	}
}

static int safemath_min(lua_State *L) {
	int n = lua_gettop(L);  /* number of arguments */
	int imin = 1;  /* index of current minimum value */
	std::string first_hex_str;
	if (!is_valid_bigint_obj(L, 1, first_hex_str)) {
		luaL_argcheck(L, false, 1, "bigint value expected");
	}
	auto first_int_str = boost::algorithm::unhex(first_hex_str);
	sm_bigint min_value(first_int_str);
	luaL_argcheck(L, n >= 1, 1, "value expected");
	for (int i = 2; i <= n; i++) {
		std::string hex_value;
		if (!is_valid_bigint_obj(L, i, hex_value)) {
			luaL_argcheck(L, false, i, "bigint value expected");
		}
		auto int_str = boost::algorithm::unhex(hex_value);
		sm_bigint int_value(int_str);
		if (int_value < min_value) {
			imin = i;
			min_value = int_value;
		}
	}
	lua_pushvalue(L, imin);
	return 1;
}

//static int safemath_min(lua_State *L) {
//	int n = lua_gettop(L);  /* number of arguments */
//	int imin = 1;  /* index of current minimum value */
//	std::string first_value_str;
//	if (!is_valid_bignumber_obj(L, 1, first_value_str)) {
//		luaL_argcheck(L, false, 1, "bignumber value expected");
//	}
//	sm_bigdecimal min_value(first_value_str);
//	luaL_argcheck(L, n >= 1, 1, "value expected");
//	for (int i = 2; i <= n; i++) {
//		std::string value_str;
//		if (!is_valid_bignumber_obj(L, i, value_str)) {
//			luaL_argcheck(L, false, i, "bignumber value expected");
//		}
//		sm_bigdecimal big_value(value_str);
//		if (big_value < min_value) {
//			imin = i;
//			min_value = big_value;
//		}
//	}
//	lua_pushvalue(L, imin);
//	return 1;
//}


static int safemath_max(lua_State *L) {
	int n = lua_gettop(L);  /* number of arguments */
	int imax = 1;  /* index of current max value */
	std::string first_hex_str;
	if (!is_valid_bigint_obj(L, 1, first_hex_str)) {
		luaL_argcheck(L, false, 1, "bigint value expected");
	}
	auto first_int_str = boost::algorithm::unhex(first_hex_str);
	sm_bigint max_value(first_int_str);
	luaL_argcheck(L, n >= 1, 1, "value expected");
	for (int i = 2; i <= n; i++) {
		std::string hex_value;
		if (!is_valid_bigint_obj(L, i, hex_value)) {
			luaL_argcheck(L, false, i, "bigint value expected");
		}
		auto int_str = boost::algorithm::unhex(hex_value);
		sm_bigint int_value(int_str);
		if (int_value > max_value) {
			imax = i;
			max_value = int_value;
		}
	}
	lua_pushvalue(L, imax);
	return 1;
}

//static int safemath_max(lua_State *L) {
//	int n = lua_gettop(L);  /* number of arguments */
//	int imax = 1;  /* index of current max value */
//	std::string first_value_str;
//	if (!is_valid_bignumber_obj(L, 1, first_value_str)) {
//		luaL_argcheck(L, false, 1, "bignumber value expected");
//	}
//	sm_bigdecimal max_value(first_value_str);
//	luaL_argcheck(L, n >= 1, 1, "value expected");
//	for (int i = 2; i <= n; i++) {
//		std::string value_str;
//		if (!is_valid_bignumber_obj(L, i, value_str)) {
//			luaL_argcheck(L, false, i, "bignumber value expected");
//		}
//		sm_bigdecimal big_value(value_str);
//		if (big_value > max_value) {
//			imax = i;
//			max_value = big_value;
//		}
//	}
//	lua_pushvalue(L, imax);
//	return 1;
//}


static const luaL_Reg safemathlib[] = {
	{ "bigint", safemath_bigint },
	/*{ "bignumber", safemath_bignumber },
	{ "iadd", safemath_iadd },
	{ "isub", safemath_isub },
	{ "imul", safemath_imul },
	{ "imin", safemath_imin },
	{ "imax", safemath_imax },
	{ "idiv", safemath_idiv },
	{ "irem", safemath_irem },
	{ "ipow", safemath_ipow },
	{ "itoint", safemath_itoint },
	{ "itohex", safemath_itohex },*/
	{ "add", safemath_add },
	{ "sub", safemath_sub },
	{ "mul", safemath_mul },
	{ "min", safemath_min },
	{ "max", safemath_max },
	{ "div", safemath_div },
	{ "rem", safemath_rem },
	{ "pow", safemath_pow },
	{ "gt", safemath_gt },
	{ "ge", safemath_ge },
	{ "lt", safemath_lt },
	{ "le", safemath_le },
	{ "eq", safemath_eq },
	{ "ne", safemath_ne },
	{ "toint", safemath_toint },
	{ "tohex", safemath_tohex },
	{ "tostring", safemath_tostring },
	{ nullptr, nullptr }
};


/*
** Open math library
*/
LUAMOD_API int luaopen_safemath(lua_State *L) {
	luaL_newlib(L, safemathlib);
	return 1;
}

