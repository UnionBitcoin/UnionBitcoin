#include <uvm/lprefix.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <list>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <thread>
#include <fstream>

#include <uvm/uvm_api.h>
#include <uvm/uvm_lib.h>
#include <uvm/uvm_lutil.h>
#include <uvm/lobject.h>
#include <uvm/lzio.h>
#include <uvm/lundump.h>
#include <uvm/lapi.h>
#include <uvm/lopcodes.h>
#include <uvm/lstate.h>
#include <uvm/ldebug.h>
#include <uvm/lauxlib.h>
#include <uvm/lualib.h>
#include <uvm/lfunc.h>
#include <uvm/uvm_storage.h>

namespace uvm
{
    namespace lua
    {
      namespace api
      {
        IUvmChainApi *global_uvm_chain_api = nullptr;
      }

      using uvm::lua::api::global_uvm_chain_api;

		namespace lib
		{

			std::vector<std::string> contract_special_api_names = { "init", "on_deposit", "on_deposit_asset", "on_destroy", "on_upgrade" };
			std::vector<std::string> contract_int_argument_special_api_names = { "on_deposit" };
			std::vector<std::string> contract_string_argument_special_api_names = { "on_deposit_asset" };

#define LUA_IN_SANDBOX_STATE_KEY "lua_in_sandbox"
            // storagecontract idstate key
#define LUA_MAYBE_CHANGE_STORAGE_CONTRACT_IDS_STATE_KEY "maybe_change_storage_contract_ids_state"

            static const char *globalvar_whitelist[] = {
                "print", "pprint", "table", "string", "time", "math", "safemath", "json", "type", "require", "Array", "Stream",
                "import_contract_from_address", "import_contract", "emit", "is_valid_address", "is_valid_contract_address",
				"get_prev_call_frame_contract_address", "get_prev_call_frame_api_name", "get_contract_call_frame_stack_size",
                "uvm", "storage", "exit", "self", "debugger", "exit_debugger",
                "caller", "caller_address",
                "contract_transfer", "contract_transfer_to", "transfer_from_contract_to_address",
				"transfer_from_contract_to_public_account",
                "get_chain_random", "get_transaction_fee", "fast_map_get", "fast_map_set",
                "get_transaction_id", "get_header_block_num", "wait_for_future_random", "get_waited",
                "get_contract_balance_amount", "get_chain_now", "get_current_contract_address", "get_system_asset_symbol", "get_system_asset_precision",
                "pairs", "ipairs", "pairsByKeys", "collectgarbage", "error", "getmetatable", "_VERSION",
                "tostring", "tojsonstring", "tonumber", "tointeger", "todouble", "totable", "toboolean",
                "next", "rawequal", "rawlen", "rawget", "rawset", "select",
                "setmetatable",
				"hex_to_bytes", "bytes_to_hex", "sha256_hex", "sha1_hex", "sha3_hex", "ripemd160_hex"
            };

            typedef lua_State* L_Key1;

            typedef std::unordered_map<std::string, UvmStateValueNode> L_VM1;

            typedef std::shared_ptr<L_VM1> L_V1;

            typedef std::unordered_map<L_Key1, L_V1> LStatesMap;

            static LStatesMap states_map;

            static LStatesMap *get_lua_states_value_hashmap()
            {
                return &states_map;
            }

            static std::mutex states_map_mutex;

            static L_V1 create_value_map_for_lua_state(lua_State *L)
            {
                LStatesMap *states_map = get_lua_states_value_hashmap();
                auto it = states_map->find(L);
                if (it == states_map->end())
                {
                    states_map_mutex.lock();
                    L_V1 map = std::make_shared<L_VM1>();
                    states_map->insert(std::make_pair(L, map));
                    states_map_mutex.unlock();
                    return map;
                }
                else
                    return it->second;
            }

			// transfer from contract to account
			static int transfer_from_contract_to_public_account(lua_State *L)
            {
				if (lua_gettop(L) < 3)
				{
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "transfer_from_contract_to_public_account need 3 arguments");
					return 0;
				}
				const char *contract_id = get_contract_id_in_api(L);
				if (nullptr == contract_id)
				{
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "contract transfer must be called in contract api");
					return 0;
				}
				const char *to_account_name = luaL_checkstring(L, 1);
				const char *asset_type = luaL_checkstring(L, 2);
				auto amount_str = luaL_checkinteger(L, 3);
				if (amount_str <= 0)
				{
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "amount must be positive");
					return 0;
				}
				lua_Integer transfer_result = uvm::lua::api::global_uvm_chain_api->transfer_from_contract_to_public_account(L, contract_id, to_account_name, asset_type, amount_str);
				lua_pushinteger(L, transfer_result);
				return 1;
            }

            /************************************************************************/
            /* transfer from contract to address                                    */
            /************************************************************************/
            static int transfer_from_contract_to_address(lua_State *L)
            {
                if (lua_gettop(L) < 3)
                {
                    uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "transfer_from_contract_to_address need 3 arguments");
                    return 0;
                }
                const char *contract_id = get_contract_id_in_api(L);
                if (!contract_id)
                {
                    uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "contract transfer must be called in contract api");
                    return 0;
                }
                const char *to_address = luaL_checkstring(L, 1);
                const char *asset_type = luaL_checkstring(L, 2);
                auto amount_str = luaL_checkinteger(L, 3);
                if (amount_str <= 0)
                {
                    uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "amount must be positive");
                    return 0;
                }
                lua_Integer transfer_result = uvm::lua::api::global_uvm_chain_api->transfer_from_contract_to_address(L, contract_id, to_address, asset_type, amount_str);
                lua_pushinteger(L, transfer_result);
                return 1;
            }

            static int get_contract_address_lua_api(lua_State *L)
            {
                const char *cur_contract_id = get_contract_id_in_api(L);
                if (!cur_contract_id)
                {
                    uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "can't get current contract address");
                    return 0;
                }
                lua_pushstring(L, cur_contract_id);
                return 1;
            }

			static std::string get_prev_call_frame_contract_id(lua_State *L)
			{
				auto contract_id_stack = get_using_contract_id_stack(L, true);
				if (!contract_id_stack || contract_id_stack->size()<2)
					return "";
				auto top = contract_id_stack->top();
				contract_id_stack->pop();
				auto prev = contract_id_stack->top();
				contract_id_stack->push(top);
				return prev.contract_id;
			}

			static std::string get_prev_call_frame_api_name(lua_State *L)
			{
				auto contract_id_stack = get_using_contract_id_stack(L, true);
				if (!contract_id_stack || contract_id_stack->size()<2)
					return "";
				auto top = contract_id_stack->top();
				contract_id_stack->pop();
				auto prev = contract_id_stack->top();
				contract_id_stack->push(top);
				return prev.api_name;
			}

			static const char *get_prev_call_frame_contract_id_in_api(lua_State *L)
			{
				const auto &contract_id = get_prev_call_frame_contract_id(L);
				auto contract_id_str = malloc_and_copy_string(L, contract_id.c_str());
				return contract_id_str;
			}

			static const char *get_prev_call_frame_api_name_in_api(lua_State *L)
			{
				const auto &api_name = get_prev_call_frame_api_name(L);
				auto api_name_str = malloc_and_copy_string(L, api_name.c_str());
				return api_name_str;
			}

			static int get_prev_call_frame_contract_address_lua_api(lua_State *L)
			{
				auto prev_contract_id = get_prev_call_frame_contract_id_in_api(L);
				if (!prev_contract_id || strlen(prev_contract_id)< 1)
				{
					lua_pushnil(L);
					return 1;
				}
				lua_pushstring(L, prev_contract_id);
				return 1;
			}

			static int get_prev_call_frame_api_name_lua_api(lua_State *L)
			{
				auto prev_api_name = get_prev_call_frame_api_name_in_api(L);
				if (!prev_api_name || strlen(prev_api_name)< 1)
				{
					lua_pushnil(L);
					return 1;
				}
				lua_pushstring(L, prev_api_name);
				return 1;
			}

			static int get_contract_call_frame_stack_size(lua_State *L)
			{
				auto contract_id_stack = get_using_contract_id_stack(L, true);
				auto count = contract_id_stack ? contract_id_stack->size() : 0;
				lua_pushinteger(L, count);
				return 1;
			}

			static int get_system_asset_symbol(lua_State *L)
			{
				const char *system_asset_symbol = uvm::lua::api::global_uvm_chain_api->get_system_asset_symbol(L);
				lua_pushstring(L, system_asset_symbol);
				return 1;
			}

			static int get_system_asset_precision(lua_State *L)
			{
				auto precision = uvm::lua::api::global_uvm_chain_api->get_system_asset_precision(L);
				lua_pushinteger(L, precision);
				return 1;
			}

            static int get_contract_balance_amount(lua_State *L)
            {
                if (lua_gettop(L) > 0 && !lua_isstring(L, 1))
                {
                    uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR,
                        "get_contract_balance_amount need 1 string argument of contract address");
                    return 0;
                }

				auto contract_address = luaL_checkstring(L, 1);
                if (strlen(contract_address) < 1)
                {
                    uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR,
                        "contract address can't be empty");
                    return 0;
                }

                if (lua_gettop(L) < 2 || !lua_isstring(L, 2))
                {
                    uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "get balance amount need asset symbol");
                    return 0;
                }

                auto assert_symbol = luaL_checkstring(L, 2);
                
                auto result = uvm::lua::api::global_uvm_chain_api->get_contract_balance_amount(L, contract_address, assert_symbol);
                lua_pushinteger(L, result);
                return 1;
            }

			static int hex_to_bytes(lua_State *L) {
				if (lua_gettop(L) < 1 || !lua_isstring(L, 1)) {
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR,
						"hex_to_bytes need 1 hex string argument");
					return 0;
				}
				auto hex_str = luaL_checkstring(L, 1);
				try {
					auto result = uvm::lua::api::global_uvm_chain_api->hex_to_bytes(hex_str);
					lua_newtable(L);
					for (size_t i = 0; i < result.size(); i++) {
						lua_pushinteger(L, (lua_Integer)result[i]);
						lua_seti(L, -2, i + 1);
					}
					return 1;
				}
				catch (const std::exception& e) {
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR,
						e.what());
					return 0;
				}
			}

			static int bytes_to_hex(lua_State *L) {
				if (lua_gettop(L) < 1 || !lua_istable(L, 1)) {
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR,
						"bytes_to_hex need 1 int array argument");
					return 0;
				}
				try {
					UvmTableMapP map = luaL_create_lua_table_map_in_memory_pool(L);
					std::list<const void*> jsons;
					luaL_traverse_table_with_nested(L, 1, lua_table_to_map_traverser_with_nested, map, jsons, 0);
					std::vector<unsigned char> bytes;
					size_t i = 1;
					while (true) {
						const auto& key = std::to_string(i);
						if (map->find(key) != map->end()) {
							const auto& value = map->at(key);
							unsigned char byte_value;
							if (value.type == uvm::blockchain::StorageValueTypes::storage_value_int) {
								byte_value = (unsigned char)value.value.int_value;
							}
							else if (value.type == uvm::blockchain::StorageValueTypes::storage_value_number) {
								byte_value = (unsigned char)value.value.number_value;
							}
							else {
								uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR,
									"invalid byte int bytes_to_hex's argument");
								return 0;
							}
							bytes.push_back(byte_value);
						}
						else {
							break;
						}
						i++;
					}
					const auto& result = uvm::lua::api::global_uvm_chain_api->bytes_to_hex(bytes);
					lua_pushstring(L, result.c_str());
					return 1;
				}
				catch (const std::exception& e) {
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR,
						e.what());
					return 0;
				}
			}

			static int sha256_hex(lua_State* L) {
				if (lua_gettop(L) < 1 || !lua_isstring(L, 1)) {
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR,
						"sha256_hex need 1 string argument");
					return 0;
				}
				try {
					auto hex_str = luaL_checkstring(L, 1);
					const auto& result = uvm::lua::api::global_uvm_chain_api->sha256_hex(hex_str);
					lua_pushstring(L, result.c_str());
					return 1;
				}
				catch (const std::exception& e) {
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR,
						e.what());
					return 0;
				}
			}
			static int sha1_hex(lua_State* L) {
				if (lua_gettop(L) < 1 || !lua_isstring(L, 1)) {
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR,
						"sha1_hex need 1 string argument");
					return 0;
				}
				try {
					auto hex_str = luaL_checkstring(L, 1);
					const auto& result = uvm::lua::api::global_uvm_chain_api->sha1_hex(hex_str);
					lua_pushstring(L, result.c_str());
					return 1;
				}
				catch (const std::exception& e) {
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR,
						e.what());
					return 0;
				}
			}
			static int sha3_hex(lua_State* L) {
				if (lua_gettop(L) < 1 || !lua_isstring(L, 1)) {
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR,
						"sha3_hex need 1 string argument");
					return 0;
				}
				try {
					auto hex_str = luaL_checkstring(L, 1);
					const auto& result = uvm::lua::api::global_uvm_chain_api->sha3_hex(hex_str);
					lua_pushstring(L, result.c_str());
					return 1;
				}
				catch (const std::exception& e) {
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR,
						e.what());
					return 0;
				}
			}
			static int ripemd160_hex(lua_State* L) {
				if (lua_gettop(L) < 1 || !lua_isstring(L, 1)) {
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR,
						"ripemd160_hex need 1 string argument");
					return 0;
				}
				try {
					auto hex_str = luaL_checkstring(L, 1);
					const auto& result = uvm::lua::api::global_uvm_chain_api->ripemd160_hex(hex_str);
					lua_pushstring(L, result.c_str());
					return 1;
				}
				catch (const std::exception& e) {
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR,
						e.what());
					return 0;
				}
			}

            // pair: (value_string, is_upvalue)
            typedef std::unordered_map<std::string, std::pair<std::string, bool>> LuaDebuggerInfoList;

            static LuaDebuggerInfoList g_debug_infos;
            static bool g_debug_running = false;

            static LuaDebuggerInfoList *get_lua_debugger_info_list_in_state(lua_State *L)
            {
                return &g_debug_infos;
            }

            static int enter_lua_debugger(lua_State *L)
            {
                LuaDebuggerInfoList *debugger_info = get_lua_debugger_info_list_in_state(L);
                if (!debugger_info)
                    return 0;
                if (lua_gettop(L) > 0 && lua_isstring(L, 1))
                {
                    // if has string parameter, use this function to get the value of the name in running debugger;
                    auto found = debugger_info->find(luaL_checkstring(L, 1));
                    if (found == debugger_info->end())
                        return 0;
                    lua_pushstring(L, found->second.first.c_str());
                    lua_pushboolean(L, found->second.second ? 1 : 0);
                    return 2;
                }

                debugger_info->clear();
                g_debug_running = true;

                // get all info snapshot in current lua_State stack
                // and then pause the thread(but other thread can use this lua_State
                // maybe you want to start a REPL before real enter debugger

                // first need back to caller func frame

                CallInfo *ci = L->ci;
                CallInfo *oci = ci->previous;
                Proto *np = getproto(ci->func);
                Proto *p = getproto(oci->func);
                LClosure *ocl = clLvalue(oci->func);
                int real_localvars_count = (int)(ci->func - oci->func - 1);
                int linedefined = p->linedefined;
                L->ci = oci;
                int top = lua_gettop(L);

                // capture locals vars and values(need get localvar name from whereelse)
                for (int i = 0; i < top; ++i)
                {
                    luaL_tojsonstring(L, i + 1, nullptr);
                    const char *value_str = luaL_checkstring(L, -1);
                    lua_pop(L, 1);
                    // if the debugger position is before the localvar position, ignore the next localvars
                    if (i >= real_localvars_count) // the localvars is after the debugger() call
                        break;
                    // get local var name
                    if (i < p->sizelocvars)
                    {
                        LocVar localvar = p->locvars[i];
                        const char *varname = getstr(localvar.varname);
						if(L->out)
							fprintf(L->out, "[debugger]%s=%s\n", varname, value_str);
                        debugger_info->insert(std::make_pair(varname, std::make_pair(value_str, false)));
                    }
                }
                // capture upvalues vars and values(need get upvalue name from whereelse)
                L->ci = ci;
                for (int i = 0; i < p->sizeupvalues; ++i)
                {
                    Upvaldesc upval = p->upvalues[i];
                    const char *upval_name = getstr(upval.name);
                    if (std::string(upval_name) == "_ENV")
                        continue;
                    UpVal *upv = ocl->upvals[upval.idx];
                    lua_pushinteger(L, 1);
                    setobj2s(L, ci->func + 1, upv->v);
                    auto value_string1 = luaL_tojsonstring(L, -1, nullptr);
                    lua_pop(L, 2);
					if(L->out)
						fprintf(L->out, "[debugger-upvalue]%s=%s\n", upval_name, value_string1);
                    debugger_info->insert(std::make_pair(upval_name, std::make_pair(value_string1, true)));
                }
                L->ci = ci;
                return 0;
            }

            static int exit_lua_debugger(lua_State *L)
            {
                g_debug_infos.clear();
                g_debug_running = false;
                return 0;
            }

            static int panic_message(lua_State *L)
            {
                auto msg = luaL_checkstring(L, -1);
                if (nullptr != msg)
                    uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, msg);
                return 0;
            }

            static int get_chain_now(lua_State *L)
            {
                auto time = uvm::lua::api::global_uvm_chain_api->get_chain_now(L);
                lua_pushinteger(L, time);
                return 1;
            }

            static int get_chain_random(lua_State *L)
            {
                auto rand = uvm::lua::api::global_uvm_chain_api->get_chain_random(L);
                lua_pushinteger(L, rand);
                return 1;
            }
            static int get_transaction_id(lua_State *L)
            {
                std::string tid = uvm::lua::api::global_uvm_chain_api->get_transaction_id(L);
                lua_pushstring(L, tid.c_str());
                return 1;
            }
            static int get_transaction_fee(lua_State *L)
            {
                int64_t res = uvm::lua::api::global_uvm_chain_api->get_transaction_fee(L);
                lua_pushinteger(L, res);
                return 1;
            }
            static int get_header_block_num(lua_State *L)
            {
                auto result = uvm::lua::api::global_uvm_chain_api->get_header_block_num(L);
                lua_pushinteger(L, result);
                return 1;
            }

            static int wait_for_future_random(lua_State *L)
            {
                if (lua_gettop(L) < 1 || !lua_isinteger(L, 1))
                {
                    uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "wait_for_future_random need a integer param");
                    return 0;
                }
                auto next = luaL_checkinteger(L, 1);
                if (next <= 0)
                {
                    uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "wait_for_future_random first param must be positive number");
                    return 0;
                }
                auto result = uvm::lua::api::global_uvm_chain_api->wait_for_future_random(L, (int)next);
                lua_pushinteger(L, result);
                return 1;
            }

			/**
			 * get pseudo random number generate by some block(maybe future block or past block's data
			 */
            static int get_waited_block_random(lua_State *L)
            {
                if (lua_gettop(L) < 1 || !lua_isinteger(L, 1))
                {
                    uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "get_waited need a integer param");
                    return 0;
                }
                auto num = luaL_checkinteger(L, 1);
                auto result = uvm::lua::api::global_uvm_chain_api->get_waited(L, (uint32_t)num);
                lua_pushinteger(L, result);
                return 1;
            }

            static int emit_uvm_event(lua_State *L)
            {
                if (lua_gettop(L) < 2 && (!lua_isstring(L, 1) || !lua_isstring(L, 2)))
                {
                    uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "emit need 2 string params");
                    return 0;
                }
                const char *contract_id = get_contract_id_in_api(L);
                const char *event_name = luaL_checkstring(L, 1);
                const char *event_param = luaL_checkstring(L, 2);
				if (!contract_id || strlen(contract_id) < 1)
					return 0;
                uvm::lua::api::global_uvm_chain_api->emit(L, contract_id, event_name, event_param);
                return 0;
            }

			static int is_valid_address(lua_State *L)
			{
				if (lua_gettop(L) < 1 || !lua_isstring(L, 1))
				{
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "is_valid_address need a param of address string");
					return 0;
				}
				auto address = luaL_checkstring(L, 1);
				auto result = uvm::lua::api::global_uvm_chain_api->is_valid_address(L, address);
				lua_pushboolean(L, result ? 1 : 0);
				return 1;
			}

			static int is_valid_contract_address(lua_State *L)
			{
				if (lua_gettop(L) < 1 || !lua_isstring(L, 1))
				{
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "is_valid_contract_address need a param of address string");
					return 0;
				}
				auto address = luaL_checkstring(L, 1);
				auto result = uvm::lua::api::global_uvm_chain_api->is_valid_contract_address(L, address);
				lua_pushboolean(L, result ? 1 : 0);
				return 1;
			}

			static int uvm_core_lib_Stream_size(lua_State *L)
            {
				auto stream = (UvmByteStream*) luaL_checkudata(L, 1, "UvmByteStream_metatable");
				if(uvm::lua::api::global_uvm_chain_api->is_object_in_pool(L, (intptr_t) stream,
					UvmOutsideObjectTypes::OUTSIDE_STREAM_STORAGE_TYPE)>0)
				{
					auto stream_size = stream->size();
					lua_pushinteger(L, stream_size);
					return 1;
				}
				else
				{
					luaL_argerror(L, 1, "Stream expected");
					return 0;
				}
            }

			static int uvm_core_lib_Stream_eof(lua_State *L)
			{
				auto stream = (UvmByteStream*)luaL_checkudata(L, 1, "UvmByteStream_metatable");
				if (uvm::lua::api::global_uvm_chain_api->is_object_in_pool(L, (intptr_t)stream,
					UvmOutsideObjectTypes::OUTSIDE_STREAM_STORAGE_TYPE)>0)
				{
					lua_pushboolean(L, stream->eof());
					return 1;
				}
				else
				{
					luaL_argerror(L, 1, "Stream expected");
					return 0;
				}
			}

			static int uvm_core_lib_Stream_current(lua_State *L)
			{
				auto stream = (UvmByteStream*)luaL_checkudata(L, 1, "UvmByteStream_metatable");
				if (uvm::lua::api::global_uvm_chain_api->is_object_in_pool(L, (intptr_t)stream,
					UvmOutsideObjectTypes::OUTSIDE_STREAM_STORAGE_TYPE)>0)
				{
					lua_pushinteger(L, stream->current());
					return 1;
				}
				else
				{
					luaL_argerror(L, 1, "Stream expected");
					return 0;
				}
			}

			static int uvm_core_lib_Stream_next(lua_State *L)
			{
				auto stream = (UvmByteStream*)luaL_checkudata(L, 1, "UvmByteStream_metatable");
				if (uvm::lua::api::global_uvm_chain_api->is_object_in_pool(L, (intptr_t)stream,
					UvmOutsideObjectTypes::OUTSIDE_STREAM_STORAGE_TYPE)>0)
				{
					lua_pushboolean(L, stream->next());
					return 1;
				}
				else
				{
					luaL_argerror(L, 1, "Stream expected");
					return 0;
				}
			}

			static int uvm_core_lib_Stream_pos(lua_State *L)
			{
				auto stream = (UvmByteStream*)luaL_checkudata(L, 1, "UvmByteStream_metatable");
				if (uvm::lua::api::global_uvm_chain_api->is_object_in_pool(L, (intptr_t)stream,
					UvmOutsideObjectTypes::OUTSIDE_STREAM_STORAGE_TYPE)>0)
				{
					lua_pushinteger(L, stream->pos());
					return 1;
				}
				else
				{
					luaL_argerror(L, 1, "Stream expected");
					return 0;
				}
			}

			static int uvm_core_lib_Stream_reset_pos(lua_State *L)
			{
				auto stream = (UvmByteStream*)luaL_checkudata(L, 1, "UvmByteStream_metatable");
				if (uvm::lua::api::global_uvm_chain_api->is_object_in_pool(L, (intptr_t)stream,
					UvmOutsideObjectTypes::OUTSIDE_STREAM_STORAGE_TYPE)>0)
				{
					stream->reset_pos();
					return 0;
				}
				else
				{
					luaL_argerror(L, 1, "Stream expected");
					return 0;
				}
			}

			static int uvm_core_lib_Stream_push(lua_State *L)
			{
				auto stream = (UvmByteStream*)luaL_checkudata(L, 1, "UvmByteStream_metatable");
				auto c = luaL_checkinteger(L, 2);
				if (uvm::lua::api::global_uvm_chain_api->is_object_in_pool(L, (intptr_t)stream,
					UvmOutsideObjectTypes::OUTSIDE_STREAM_STORAGE_TYPE)>0)
				{
					stream->push((char)c);
					return 0;
				}
				else
				{
					luaL_argerror(L, 1, "Stream expected");
					return 0;
				}
			}

			static int uvm_core_lib_Stream_push_string(lua_State *L)
			{
				auto stream = (UvmByteStream*)luaL_checkudata(L, 1, "UvmByteStream_metatable");
				auto argstr = luaL_checkstring(L, 2);
				if (uvm::lua::api::global_uvm_chain_api->is_object_in_pool(L, (intptr_t)stream,
					UvmOutsideObjectTypes::OUTSIDE_STREAM_STORAGE_TYPE)>0)
				{
					for(size_t i=0;i<strlen(argstr);++i)
					{
						stream->push(argstr[i]);
					}
					return 0;
				}
				else
				{
					luaL_argerror(L, 1, "Stream expected");
					return 0;
				}
			}

			/**
			 * constructor of Stream record type. use lightuserdata to avoiding crash in tostring
			 */
			static int uvm_core_lib_Stream(lua_State *L)
            {
				auto stream = new UvmByteStream();
				uvm::lua::api::global_uvm_chain_api->register_object_in_pool(L, (intptr_t) stream, UvmOutsideObjectTypes::OUTSIDE_STREAM_STORAGE_TYPE);
				lua_pushlightuserdata(L, (void*) stream);
				luaL_getmetatable(L, "UvmByteStream_metatable");
				lua_setmetatable(L, -2);
				return 1;
            }

			// function Array(props) return props or {}; end
			static int uvm_core_lib_Array(lua_State *L)
            {
	            if(lua_gettop(L)<1 || lua_isnil(L, 1) || !lua_istable(L, 1))
	            {
					lua_createtable(L, 0, 0);
					return 1;
	            }
				else
				{
					lua_pushvalue(L, 1);
					return 1;
				}
            }

			static int uvm_core_lib_Hashmap(lua_State *L)
            {
	            if(lua_gettop(L)<1 || !lua_istable(L, 1))
	            {
					lua_createtable(L, 0, 0);
					return 1;
	            }
				else
				{
					lua_pushvalue(L, 1);
					return 1;
				}
            }

			// contract::__index: function (t, k) return t._data[k]; end
			static int uvm_core_lib_contract_metatable_index(lua_State *L)
            {
				// top:2: table, key
				lua_pushstring(L, "id");
				lua_rawget(L, 1);
				auto contract_id_in_contract = lua_tostring(L, 3);
				lua_pop(L, 1);

				auto key = luaL_checkstring(L, 2);
				lua_getfield(L, 1, "_data");

				lua_pushstring(L, "id");
				lua_rawget(L, 3);
				auto contract_id_in_data_prop = lua_tostring(L, 4);
				lua_pop(L, 1);

				auto contract_id = contract_id_in_contract ? contract_id_in_contract : contract_id_in_data_prop;

				lua_getfield(L, 3, key);
				
				auto tmp_global_key = "_uvm_core_lib_contract_metatable_index_tmp_value";
				lua_setglobal(L, tmp_global_key);
				lua_pop(L, 1);
				lua_getglobal(L, tmp_global_key);
				lua_pushnil(L);
				lua_setglobal(L, tmp_global_key);

				return 1;
            }

			/*
			contract::__newindex:
			function(t, k, v)
				if not t._data then
					print("empty _data\n")
				end
				if k == 'id' or k == 'name' or k == 'storage' then
					if not t._data[k] then
						t._data[k] = v
						return
					end
					error("attempt to update a read-only table!")
					return
				else
					t._data[k] = v
				end
			end
			*/
			static int uvm_core_lib_contract_metatable_newindex(lua_State *L)
			{
				lua_getfield(L, 1, "_data"); // stack: t, k, v, _data
				if(lua_isnil(L, 4))
				{
					if (L->out)
						fprintf(L->out, "empty _data\n");
					lua_pop(L, 1);
					return 0;
				}
				auto key = luaL_checkstring(L, 2);
				std::string key_str(key);
				if(key == "id" || key == "name" || key == "storage")
				{
					lua_getfield(L, 4, key); // stack: t, k, v, _data, _data[k]
					if(lua_isnil(L, 5))
					{
						lua_pop(L, 1); // stack: t, k, v, _data
						lua_pushvalue(L, 3); // stack: t, k, v, _data, v
						lua_setfield(L, 4, key); // stack: t, k, v, _data
						lua_pop(L, 1);
						return 0;
					}
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "attempt to update a read-only table!");
					lua_pop(L, 2); // stack: t, k, v
					return 0;
				}
				else
				{
					lua_pushvalue(L, 3); // stack: t, k, v, _data, v
					lua_setfield(L, 4, key); // stack: t, k, v, _data
					lua_pop(L, 1); // stack: t, k, v
					return 0;
				}
			}

			// read meta method of storage
			// storage::__index: function(s, key)
			//    if type(key) ~= 'string' then
			//    uvm.error('only string can be storage key')
			//	  return nil
			//	  end
			//	  return uvm.get_storage(s.contract, key)
			//	  end
			static int uvm_core_lib_storage_metatable_index(lua_State *L)
            {
				if(!lua_isstring(L, 2))
				{
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "only string can be storage key");
					L->force_stopping = true;
					lua_pushnil(L);
					return 1;
				}
				auto key = luaL_checkstring(L, 2); // top=2
				lua_getfield(L, 1, "contract"); // top=3
				lua_getfield(L, 3, "id");
				auto contract_id = luaL_checkstring(L, -1);
				lua_pop(L, 1);
				lua_pushvalue(L, 2); // top=4
				auto ret_count = uvm::lib::uvmlib_get_storage_impl(L, contract_id, key, "", false); // top=ret_count + 4
				if(ret_count>0)
				{
					auto tmp_global_key = "_uvm_core_lib_uvm_get_storage_tmp_value";
					lua_setglobal(L, tmp_global_key); // top=ret_count + 3
					lua_pop(L, ret_count + 1); // top=2
					lua_getglobal(L, tmp_global_key); // top=3
					lua_pushnil(L); // top=4
					lua_setglobal(L, tmp_global_key); // top=3
					return 1;
				} else
				{
					lua_pop(L, 2);
					return 0;
				}
            }

			// write meta method of storage
			// storage::__newindex: function(s, key, val)
			// if type(key) ~= 'string' then
			//	uvm.error('only string can be storage key')
			//	return nil
			//	end
			//	return uvm.set_storage(s.contract, key, val)
			//	end
			static int uvm_core_lib_storage_metatable_new_index(lua_State *L)
            {
				if (!lua_isstring(L, 2))
				{
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "only string can be storage key");
					L->force_stopping = true;
					lua_pushnil(L);
					return 1;
				}

                auto key = luaL_checkstring(L, 2); // top=3, self, key, value
                lua_getfield(L, 1, "contract"); // top=4, self, key, value, contract
                lua_getfield(L, 4, "id"); // top=5, self, key, value, contract, contract_id
                auto contract_id = luaL_checkstring(L, -1);
                lua_pop(L, 2); // top=3, self, key, value
                return uvm::lib::uvmlib_set_storage_impl(L, contract_id, key, "", false, 3);
            }

			static int uvm_core_lib_pairs_by_keys_func_loader(lua_State *L)
            {
				lua_getglobal(L, "__real_pairs_by_keys_func");
				bool exist = !lua_isnil(L, -1) && (lua_isfunction(L, -1) || lua_iscfunction(L, -1));
				lua_pop(L, 1);
				if (exist)
					return 0; 
				// pairsByKeys' iterate order is number first(than string), short string first(than long string), little ASCII string first
				const char *code = R"END(
function __real_pairs_by_keys_func(t)
	local hashes = {}  
	local n = nil
	local int_key_size = 0;
	local int_hashes = {}
    for n in __old_pairs(t) do
		if type(n) == 'number' then
			int_key_size = int_key_size + 1
			int_hashes[#int_hashes+1] = n
		else
			hashes[#hashes+1] = tostring(n)
		end
    end  
	table.sort(int_hashes)
    table.sort(hashes)  
	local k = 0
	while k < #hashes do
		k = k + 1
	end
    local i = 0  
    return function()  
        i = i + 1  
		if i <= int_key_size then
			return int_hashes[i], t[int_hashes[i]]
		end
        return hashes[i-int_key_size], t[hashes[i-int_key_size]] 
    end  
end
)END";
				luaL_dostring(L, code);
				return 0;
            }

			/*
			function pairsByKeys(t)
				uvm_core_lib_pairs_by_keys_func_loader()
				return __real_pairs_by_keys_func(t)
			end 
			*/
			static int uvm_core_lib_pairs_by_keys(lua_State *L)
            {
				lua_getglobal(L, "uvm_core_lib_pairs_by_keys_func_loader");
				lua_call(L, 0, 0);
				lua_getglobal(L, "__real_pairs_by_keys_func");
				lua_pushvalue(L, 1);
				lua_call(L, 1, 1);
				return 1;
            }

			static int gas_penalty_threshold = 100000; // when over this gas threshold, some api like fast_map_get/fast_map_set will cost more gas

			static int fast_map_get(lua_State *L)
			{
				// fast_map_get(storage_name, key)
				auto common_gas = 50;
				if (uvm::lua::lib::get_lua_state_instructions_executed_count(L) > gas_penalty_threshold) {
					uvm::lua::lib::increment_lvm_instructions_executed_count(L, 1000 * common_gas - 1);
				}
				else {
					uvm::lua::lib::increment_lvm_instructions_executed_count(L, common_gas - 1);
				}
				if (lua_gettop(L) < 2 || !lua_isstring(L, 1) || !lua_isstring(L, 2)) {
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "invalid arguments of fast_map_get");
					L->force_stopping = true;
					lua_pushnil(L);
					return 1;
				}
				auto cur_contract_id = get_current_using_contract_id(L);
				auto storage_name = luaL_checkstring(L, 1);
				auto fast_map_key = luaL_checkstring(L, 2);
				return uvm::lib::uvmlib_get_storage_impl(L, cur_contract_id.c_str(), storage_name, fast_map_key, true);
			}

			static int fast_map_set(lua_State *L)
			{
				// fast_map_set(storage, key, value)
				auto common_gas = 100;
				if (uvm::lua::lib::get_lua_state_instructions_executed_count(L) > gas_penalty_threshold) {
					uvm::lua::lib::increment_lvm_instructions_executed_count(L, 1000 * common_gas - 1);
				}
				else {
					uvm::lua::lib::increment_lvm_instructions_executed_count(L, common_gas - 1);
				}
				if (lua_gettop(L) < 3 || !lua_isstring(L, 1) || !lua_isstring(L, 2)) {
					uvm::lua::api::global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "invalid arguments of fast_map_set");
					L->force_stopping = true;
					lua_pushnil(L);
					return 1;
				}
				auto cur_contract_id = get_current_using_contract_id(L);
				auto storage_name = luaL_checkstring(L, 1);
				auto fast_map_key = luaL_checkstring(L, 2);
				auto value_index = 3;
				uvm::lib::uvmlib_set_storage_impl(L, cur_contract_id.c_str(), storage_name, fast_map_key, true, value_index);
				return 0;
			}

            lua_State *create_lua_state(bool use_contract)
            {
                lua_State *L = luaL_newstate();
                luaL_openlibs(L);
                // run init lua code here, eg. init storage api, load some modules
				add_global_c_function(L, "debugger", &enter_lua_debugger);
				add_global_c_function(L, "exit_debugger", &exit_lua_debugger);
				lua_createtable(L, 0, 0);
				lua_setglobal(L, "last_return"); // func's last return value in this global variable
				add_global_c_function(L, "Array", &uvm_core_lib_Array);
				add_global_c_function(L, "Map", &uvm_core_lib_Hashmap);

				lua_getglobal(L, "uvm");
				if (lua_isnil(L, -1)) {
					lua_createtable(L, 0, 0);
					lua_setglobal(L, "uvm");
				}
				lua_pop(L, 1);

				luaL_newmetatable(L, "UvmByteStream_metatable");
				lua_pushcfunction(L, &uvm_core_lib_Stream_size);
				lua_setfield(L, -2, "size");
				lua_pushcfunction(L, &uvm_core_lib_Stream_eof);
				lua_setfield(L, -2, "eof");
				lua_pushcfunction(L, &uvm_core_lib_Stream_current);
				lua_setfield(L, -2, "current");
				lua_pushcfunction(L, &uvm_core_lib_Stream_pos);
				lua_setfield(L, -2, "pos");
				lua_pushcfunction(L, &uvm_core_lib_Stream_next);
				lua_setfield(L, -2, "next");
				lua_pushcfunction(L, &uvm_core_lib_Stream_push);
				lua_setfield(L, -2, "push");
				lua_pushcfunction(L, &uvm_core_lib_Stream_push_string);
				lua_setfield(L, -2, "push_string");
				lua_pushcfunction(L, &uvm_core_lib_Stream_reset_pos);
				lua_setfield(L, -2, "reset_pos"); 

				lua_pushvalue(L, -1);
				lua_setfield(L, -2, "__index");
				lua_pop(L, 1);

                lua_pushinteger(L, 0);
				lua_setglobal(L, "__real_pairs_by_keys_func");

				add_global_c_function(L, "Stream", &uvm_core_lib_Stream);
				add_global_c_function(L, "uvm_core_lib_pairs_by_keys_func_loader", &uvm_core_lib_pairs_by_keys_func_loader);
				
				lua_createtable(L, 0, 0);
				lua_pushcfunction(L, &uvm_core_lib_storage_metatable_index);
				lua_setfield(L, -2, "__index");
				lua_pushcfunction(L, &uvm_core_lib_storage_metatable_new_index);
				lua_setfield(L, -2, "__newindex");
				lua_getglobal(L, "uvm");
				lua_pushvalue(L, -2);
				lua_setfield(L, -2, "storage_mt");
				lua_pop(L, 2);

				lua_createtable(L, 0, 0);
				lua_pushcfunction(L, &uvm_core_lib_contract_metatable_index);
				lua_setfield(L, -2, "__index");
				lua_pushcfunction(L, &uvm_core_lib_contract_metatable_newindex);
				lua_setfield(L, -2, "__newindex");
				lua_setglobal(L, "contract_mt");

				add_global_c_function(L, "fast_map_get", &fast_map_get);
				add_global_c_function(L, "fast_map_set", &fast_map_set);


				/*
				__old_pairs = pairs
				pairs = pairsByKeys
				*/
				lua_getglobal(L, "pairs");
				lua_setglobal(L, "__old_pairs");
				lua_pushcfunction(L, &uvm_core_lib_pairs_by_keys);
				lua_setglobal(L, "pairs");

				add_global_c_function(L, "hex_to_bytes", hex_to_bytes);
				add_global_c_function(L, "bytes_to_hex", bytes_to_hex);
				add_global_c_function(L, "sha256_hex", sha256_hex);
				add_global_c_function(L, "sha1_hex", sha1_hex);
				add_global_c_function(L, "sha3_hex", sha3_hex);
				add_global_c_function(L, "ripemd160_hex", ripemd160_hex);

				reset_lvm_instructions_executed_count(L);
                lua_atpanic(L, panic_message);
                if (use_contract)
                {
                    add_global_c_function(L, "transfer_from_contract_to_address", transfer_from_contract_to_address);
					add_global_c_function(L, "transfer_from_contract_to_public_account", transfer_from_contract_to_public_account);
                    add_global_c_function(L, "get_contract_balance_amount", get_contract_balance_amount);
                    add_global_c_function(L, "get_chain_now", get_chain_now);
                    add_global_c_function(L, "get_chain_random", get_chain_random);
                    add_global_c_function(L, "get_current_contract_address", get_contract_address_lua_api);
                    add_global_c_function(L, "get_transaction_id", get_transaction_id);
                    add_global_c_function(L, "get_header_block_num", get_header_block_num);
                    add_global_c_function(L, "wait_for_future_random", wait_for_future_random);
                    add_global_c_function(L, "get_waited", get_waited_block_random);
                    add_global_c_function(L, "get_transaction_fee", get_transaction_fee);
                    add_global_c_function(L, "emit", emit_uvm_event);
					add_global_c_function(L, "is_valid_address", is_valid_address);
					add_global_c_function(L, "is_valid_contract_address", is_valid_contract_address);
					add_global_c_function(L, "get_prev_call_frame_contract_address", get_prev_call_frame_contract_address_lua_api);
					add_global_c_function(L, "get_prev_call_frame_api_name", get_prev_call_frame_api_name_lua_api);
					add_global_c_function(L, "get_contract_call_frame_stack_size", get_contract_call_frame_stack_size),
					add_global_c_function(L, "get_system_asset_symbol", get_system_asset_symbol);
					add_global_c_function(L, "get_system_asset_precision", get_system_asset_precision);

                }
                return L;
            }

            bool commit_storage_changes(lua_State *L)
            {
                if (!uvm::lua::api::global_uvm_chain_api->has_exception(L))
                {
                    return luaL_commit_storage_changes(L);
                }
                return false;
            }

            void close_lua_state(lua_State *L)
            {
                luaL_commit_storage_changes(L);
				uvm::lua::api::global_uvm_chain_api->release_objects_in_pool(L);
                LStatesMap *states_map = get_lua_states_value_hashmap();
                if (nullptr != states_map)
                {
                    auto lua_table_map_list_p = get_lua_state_value(L, LUA_TABLE_MAP_LIST_STATE_MAP_KEY).pointer_value;
                    if (nullptr != lua_table_map_list_p)
                    {
                        auto list_p = (std::list<UvmTableMapP>*) lua_table_map_list_p;
                        for (auto it = list_p->begin(); it != list_p->end(); ++it)
                        {
                            UvmTableMapP lua_table_map = *it;
                            // lua_table_map->~UvmTableMap();
                            // lua_free(L, lua_table_map);
                            delete lua_table_map;
                        }
                        delete list_p;
                    }

                    L_V1 map = create_value_map_for_lua_state(L);
                    for (auto it = map->begin(); it != map->end(); ++it)
                    {
                        if (it->second.type == LUA_STATE_VALUE_INT_POINTER)
                        {
                            lua_free(L, it->second.value.int_pointer_value);
                            it->second.value.int_pointer_value = nullptr;
                        }
                    }
                    // close values in state values(some pointers need free), eg. storage infos, contract infos
                    UvmStateValueNode storage_changelist_node = get_lua_state_value_node(L, LUA_STORAGE_CHANGELIST_KEY);
                    if (storage_changelist_node.type == LUA_STATE_VALUE_POINTER && nullptr != storage_changelist_node.value.pointer_value)
                    {
                        UvmStorageChangeList *list = (UvmStorageChangeList*)storage_changelist_node.value.pointer_value;
                        for (auto it = list->begin(); it != list->end(); ++it)
                        {
                            UvmStorageValue before = it->before;
                            UvmStorageValue after = it->after;
                            if (lua_storage_is_table(before.type))
                            {
                                // free_lua_table_map(L, before.value.table_value);
                            }
                            if (lua_storage_is_table(after.type))
                            {
                                // free_lua_table_map(L, after.value.table_value);
                            }
                        }
                        list->~UvmStorageChangeList();
                        lua_free(L, list);
                    }

                    UvmStateValueNode storage_table_read_list_node = get_lua_state_value_node(L, LUA_STORAGE_READ_TABLES_KEY);
                    if (storage_table_read_list_node.type == LUA_STATE_VALUE_POINTER && nullptr != storage_table_read_list_node.value.pointer_value)
                    {
                        UvmStorageTableReadList *list = (UvmStorageTableReadList*)storage_table_read_list_node.value.pointer_value;
                        list->~UvmStorageTableReadList();
                        lua_free(L, list);
                    }

                    int *insts_executed_count = get_lua_state_value(L, INSTRUCTIONS_EXECUTED_COUNT_LUA_STATE_MAP_KEY).int_pointer_value;
                    if (nullptr != insts_executed_count)
                    {
                        lua_free(L, insts_executed_count);
                    }
                    int *stopped_pointer = uvm::lua::lib::get_lua_state_value(L, LUA_STATE_STOP_TO_RUN_IN_LVM_STATE_MAP_KEY).int_pointer_value;
                    if (nullptr != stopped_pointer)
                    {
                        lua_free(L, stopped_pointer);
                    }
                    
                    states_map->erase(L);
                }

                lua_close(L);
            }

            /**
            * share some values in L
            */
            void close_all_lua_state_values()
            {
                LStatesMap *states_map = get_lua_states_value_hashmap();
                states_map->clear();
            }
            void close_lua_state_values(lua_State *L)
            {
                LStatesMap *states_map = get_lua_states_value_hashmap();
                states_map->erase(L);
            }

            UvmStateValueNode get_lua_state_value_node(lua_State *L, const char *key)
            {
                UvmStateValue nil_value = { 0 };
                UvmStateValueNode nil_value_node;
                nil_value_node.type = LUA_STATE_VALUE_nullptr;
                nil_value_node.value = nil_value;
                if (nullptr == L || nullptr == key || strlen(key) < 1)
                {
                    return nil_value_node;
                }

                LStatesMap *states_map = get_lua_states_value_hashmap();
                L_V1 map = create_value_map_for_lua_state(L);
                std::string key_str(key);
                auto it = map->find(key_str);
                if (it == map->end())
                    return nil_value_node;
                else
                    return map->at(key_str);
            }

            UvmStateValue get_lua_state_value(lua_State *L, const char *key)
            {
                return get_lua_state_value_node(L, key).value;
            }
            void set_lua_state_instructions_limit(lua_State *L, int limit)
            {
                UvmStateValue value = { limit };
                set_lua_state_value(L, INSTRUCTIONS_LIMIT_LUA_STATE_MAP_KEY, value, LUA_STATE_VALUE_INT);
            }

            int get_lua_state_instructions_limit(lua_State *L)
            {
                return get_lua_state_value(L, INSTRUCTIONS_LIMIT_LUA_STATE_MAP_KEY).int_value;
            }

            int get_lua_state_instructions_executed_count(lua_State *L)
            {
                int *insts_executed_count = get_lua_state_value(L, INSTRUCTIONS_EXECUTED_COUNT_LUA_STATE_MAP_KEY).int_pointer_value;
                if (nullptr == insts_executed_count)
                {
                    return 0;
                }
                if (*insts_executed_count < 0)
                    return 0;
                else
                    return *insts_executed_count;
            }

            void enter_lua_sandbox(lua_State *L)
            {
                UvmStateValue value;
                value.int_value = 1;
                set_lua_state_value(L, LUA_IN_SANDBOX_STATE_KEY, value, LUA_STATE_VALUE_INT);
            }

            void exit_lua_sandbox(lua_State *L)
            {
                UvmStateValue value;
                value.int_value = 0;
                set_lua_state_value(L, LUA_IN_SANDBOX_STATE_KEY, value, LUA_STATE_VALUE_INT);
            }

            bool check_in_lua_sandbox(lua_State *L)
            {
                return get_lua_state_value_node(L, LUA_IN_SANDBOX_STATE_KEY).value.int_value > 0;
            }

            /**
            * notify lvm to stop running the lua stack
            */
            void notify_lua_state_stop(lua_State *L)
            {
                int *pointer = get_lua_state_value(L, LUA_STATE_STOP_TO_RUN_IN_LVM_STATE_MAP_KEY).int_pointer_value;
                if (nullptr == pointer)
                {
                    pointer = (int*)lua_malloc(L, sizeof(int));
                    *pointer = 1;
                    UvmStateValue value;
                    value.int_pointer_value = pointer;
                    set_lua_state_value(L, LUA_STATE_STOP_TO_RUN_IN_LVM_STATE_MAP_KEY, value, LUA_STATE_VALUE_INT_POINTER);
                }
                else
                {
                    *pointer = 1;
                }
            }

            /**
            * check whether the lua state notified stop before
            */
            bool check_lua_state_notified_stop(lua_State *L)
            {
                int *pointer = get_lua_state_value(L, LUA_STATE_STOP_TO_RUN_IN_LVM_STATE_MAP_KEY).int_pointer_value;
                if (nullptr == pointer)
                    return false;
                return (*pointer) > 0;
            }

            /**
            * resume lua_State to be available running again
            */
            void resume_lua_state_running(lua_State *L)
            {
                int *pointer = get_lua_state_value(L, LUA_STATE_STOP_TO_RUN_IN_LVM_STATE_MAP_KEY).int_pointer_value;
                if (nullptr != pointer)
                {
                    *pointer = 0;
                }
            }

            void set_lua_state_value(lua_State *L, const char *key, UvmStateValue value, enum UvmStateValueType type)
            {
                if (nullptr == L || nullptr == key || strlen(key) < 1)
                {
                    return;
                }

                L_V1 map = create_value_map_for_lua_state(L);
                UvmStateValueNode node_v;
                node_v.type = type;
                node_v.value = value;
                if (node_v.type == LUA_STATE_VALUE_STRING)
                    node_v.value.string_value = uvm::lua::lib::malloc_and_copy_string(L, value.string_value);
                std::string key_str(key);
                map->erase(key_str);
                map->insert(std::make_pair(key_str, node_v));
            }

            static const char* reader_of_stream(lua_State *L, void *ud, size_t *size)
            {
                UNUSED(L);
                UvmModuleByteStream *stream = static_cast<UvmModuleByteStream*>(ud);
                if (!stream)
                    return nullptr;
				if (size)
					*size = stream->buff.size();
                return stream->buff.data();
            }

            LClosure* luaU_undump_from_file(lua_State *L, const char *binary_filename, const char* name)
            {
                ZIO z;
                FILE *f = fopen(binary_filename, "rb");
                if (nullptr == f)
                    return nullptr;
				auto stream = std::make_shared<UvmModuleByteStream>();
                if (nullptr == stream)
                    return nullptr;
				auto f_cur = ftell(f);
				fseek(f, 0, SEEK_END);
				auto f_size = ftell(f);
				fseek(f, f_cur, 0);
				stream->buff.resize(stream->buff.size() + f_size);
                fread(stream->buff.data(), f_size, 1, f);
                fseek(f, 0, SEEK_END); // seek to end of file
                stream->is_bytes = true;
                luaZ_init(L, &z, reader_of_stream, (void*)stream.get());
                luaZ_fill(&z);
                LClosure *closure = luaU_undump(L, &z, name);
				if (!closure)
					return nullptr;
                fclose(f);
                return closure;
            }

            void free_bytecode_stream(UvmModuleByteStreamP stream)
            {
				delete stream;
            }

            LClosure *luaU_undump_from_stream(lua_State *L, UvmModuleByteStream *stream, const char *name)
            {
                ZIO z;
                luaZ_init(L, &z, reader_of_stream, (void*) stream);
                luaZ_fill(&z);
                auto cl = luaU_undump(L, &z, name);
				return cl;
            }

#define UPVALNAME_OF_PROTO(proto, x) (((proto)->upvalues[x].name) ? getstr((proto)->upvalues[x].name) : "-")
#define MYK(x)		(-1-(x))

            static const size_t globalvar_whitelist_count = sizeof(globalvar_whitelist) / sizeof(globalvar_whitelist[0]);


            static const std::string TYPED_LUA_LIB_CODE = R"END(type Contract<S> = {
	id: string,
	name: string,
	storage: S
}
)END";

            const std::string get_typed_lua_lib_code()
            {
                return TYPED_LUA_LIB_CODE;
            }

            static bool upval_defined_in_parent(lua_State *L, Proto *parent, std::list<Proto*> *all_protos, Upvaldesc upvaldesc)
            {
                // whether the upval defined in parent proto
                if (!L || !parent)
                    return false;
                if (upvaldesc.instack > 0)
                {
                    if (upvaldesc.idx < parent->sizelocvars)
                    {
                        return true;
                    }
                    else
                        return false;
                }
                else
                {
                    if (!all_protos || all_protos->size() < 1)
                        return false;
                    if (upvaldesc.idx < parent->sizeupvalues)
                    {
                        Upvaldesc parent_upvaldesc = parent->upvalues[upvaldesc.idx];
                        for (const auto &pp : *all_protos)
                        {
                            std::list<Proto*> remaining;
                            for (const auto &pp2 : *all_protos)
                            {
                                if (remaining.size() >= all_protos->size() - 1)
                                    break;
                                remaining.push_back(pp2);
                            }
                            return upval_defined_in_parent(L, pp, &remaining, parent_upvaldesc);
                        }
                        return false;
                    }
                    else
                        return false;
                }
            }

            bool check_contract_proto(lua_State *L, Proto *proto, char *error, std::list<Proto*> *parents)
            {
                // for all sub function in proto, check whether the contract bytecode meet our provision
                int i, proto_size = proto->sizep;
                // int const_size = proto->sizek;
                int localvar_size = proto->sizelocvars;
                int upvalue_size = proto->sizeupvalues;

                if (localvar_size > LUA_FUNCTION_MAX_LOCALVARS_COUNT)
                {
                    lcompile_error_set(L, error, "too many local vars in function, limit is %d", LUA_FUNCTION_MAX_LOCALVARS_COUNT);
                    return false;
                }

                std::list<std::string> localvars;
                for (i = 0; i < localvar_size; i++)
                {
                    // int startpc = proto->locvars[i].startpc;
                    // int endpc = proto->locvars[i].endpc;
                    std::string localvarname(getstr(proto->locvars[i].varname));
                }

                std::list<std::tuple<std::string, int, int>> upvalues;
                for (i = 0; i < upvalue_size; i++)
                {
                    std::string upvalue_name(UPVALNAME_OF_PROTO(proto, i));
                    int instack = proto->upvalues[i].instack;
                    int idx = proto->upvalues[i].idx;
                    upvalues.push_back(std::make_tuple(upvalue_name, instack, idx));
                }


                const Instruction* code = proto->code;
                int pc;
                int code_size = proto->sizecode;

				bool is_importing_contract = false;
				bool is_importing_contract_address = false;

                for (pc = 0; pc < code_size; pc++)
                {
                    Instruction i = code[pc];
                    OpCode o = GET_OPCODE(i);
                    int a = GETARG_A(i);
                    int b = GETARG_B(i);
                    int c = GETARG_C(i);
                    int ax = GETARG_Ax(i);
                    int bx = GETARG_Bx(i);
                    int sbx = GETARG_sBx(i);
                    int line = getfuncline(proto, pc);

					if(is_importing_contract)
					{
						is_importing_contract = false;
						// LOADK
						if(getOpMode(o) == UOP_LOADK)
						{
							int idx = MYK(INDEXK(bx));
							int idx_in_kst = -idx - 1;
							if (idx_in_kst >= 0 && idx_in_kst < proto->sizek)
							{
								const char *contract_name = getstr(tsvalue(&proto->k[idx_in_kst]));
								if (contract_name && !uvm::lua::api::global_uvm_chain_api->check_contract_exist(L, contract_name))
								{
									lcompile_error_set(L, error, "Can't find contract %s", contract_name);
									return false;
								}
							}
						}
					}
					else if(is_importing_contract_address)
					{
						is_importing_contract_address = false;
						// LOADK
						if (getOpMode(o) == UOP_LOADK)
						{
							int idx = MYK(INDEXK(bx));
							int idx_in_kst = -idx - 1;
							if (idx_in_kst >= 0 && idx_in_kst < proto->sizek)
							{
								const char *contract_address = getstr(tsvalue(&proto->k[idx_in_kst]));
								if (contract_address && !uvm::lua::api::global_uvm_chain_api->check_contract_exist_by_address(L, contract_address))
								{
									lcompile_error_set(L, error, "Can't find contract address %s", contract_address);
									return false;
								}
							}
						}
					}

                    switch (getOpMode(o))
                    {
                    case iABC:
                        break;
                    case iABx:
                        break;
                    case iAsBx:
                        break;
                    case iAx:
                        break;
                    }
                    switch (o)
                    {
                    case UOP_GETUPVAL:
                    {
                        // when instack=1, find in parent localvars, when instack=0, find in parent upval pool
                        if (a == 0)
                            break;
						if (b >= proto->sizeupvalues || b < 0)
						{
							lcompile_error_set(L, error, "upvalue error");
							return false;
						}
                        const char *upvalue_name = UPVALNAME_OF_PROTO(proto, b);
                        int cidx = MYK(INDEXK(b));
                        if (nullptr == proto->k)
                            break;
                        // const char *cname = getstr(tsvalue(&proto->k[-cidx-1]));
                        const char *cname = upvalue_name;
                        bool in_whitelist = false;
                        for (size_t i = 0; i < globalvar_whitelist_count; ++i)
                        {
                            if (strcmp(cname, globalvar_whitelist[i]) == 0)
                            {
                                in_whitelist = true;
                                break;
                            }
                        }
                        if (strcmp(upvalue_name, "_ENV") == 0)
                        {
                            in_whitelist = true; // whether this can do? maybe need to get what property are fetching
                        }
                        if (strcmp(upvalue_name, "-") == 0)
							continue;
                        if (!in_whitelist)
                        {
                            Upvaldesc upvaldesc = proto->upvalues[c];
                            // check in parent proto, whether defined in parent proto
                            bool upval_defined = (parents && parents->size() > 0) ? upval_defined_in_parent(L, *parents->rbegin(), parents, upvaldesc) : false;
                            if (!upval_defined)
                            {
                                lcompile_error_set(L, error, "use global variable %s not in whitelist", cname);
                                return false;
                            }
                        }
                    }	 break;
                    case UOP_SETUPVAL:
                    {
						if (b >= proto->sizeupvalues || b < 0)
						{
							lcompile_error_set(L, error, "upvalue error");
							return false;
						}
                        const char *upvalue_name = UPVALNAME_OF_PROTO(proto, b);
                        // not support change _ENV or _G
                        if (strcmp("_ENV", upvalue_name) == 0
                            || strcmp("_G", upvalue_name) == 0)
                        {
                            lcompile_error_set(L, error, "_ENV or _G set %s is forbidden", upvalue_name);
                            return false;
                        }
                        break;
                    }
                    case UOP_GETTABUP:
                    {
						if (b >= proto->sizeupvalues || b < 0)
						{
							lcompile_error_set(L, error, "upvalue error");
							return false;
						}
                        const char *upvalue_name = UPVALNAME_OF_PROTO(proto, b);
                        if (ISK(c)){
                            int cidx = MYK(INDEXK(c));
                            const char *cname = getstr(tsvalue(&proto->k[-cidx - 1]));
                            bool in_whitelist = false;
                            for (size_t i = 0; i < globalvar_whitelist_count; ++i)
                            {
                                if (strcmp(cname, globalvar_whitelist[i]) == 0 || strcmp(cname, "-") == 0)
                                {
                                    in_whitelist = true;
                                    break;
                                }
                            }
                            if (!in_whitelist && (strcmp(upvalue_name, "_ENV") == 0 || strcmp(upvalue_name, "_G") == 0))
                            {
                                lcompile_error_set(L, error, "use global variable %s not in whitelist", cname);
                                return false;
                            }
							if(strcmp(cname, "import_contract")==0)
								is_importing_contract = true;
							else if (strcmp(cname, "import_contract_address") == 0)
								is_importing_contract_address = true;
                        }
                        else
                        {

                        }
                    }
                    break;
                    case UOP_SETTABUP:
                    {
						if (a >= proto->sizeupvalues || a < 0)
						{
							lcompile_error_set(L, error, "upvalue error");
							return false;
						}
                        const char *upvalue_name = UPVALNAME_OF_PROTO(proto, a);
                        // not support change _ENV or _G
                        if (strcmp("_ENV", upvalue_name) == 0
                            || strcmp("_G", upvalue_name) == 0)
                        {
                            if (ISK(b)){
                                int bidx = MYK(INDEXK(b));
                                const char *bname = getstr(tsvalue(&proto->k[-bidx - 1]));
                                lcompile_error_set(L, error, "_ENV or _G set %s is forbidden", bname);
                                return false;

                            }
                            else
                            {
                                lcompile_error_set(L, error, "_ENV or _G set %s is forbidden", upvalue_name);
                                return false;
                            }
                        }
                        // not support change _ENV or _G
                        /*
                        if (strcmp("_ENV", upvalue_name) == 0
                        || strcmp("_G", upvalue_name) == 0)
                        {
                        lcompile_error_set(L, error, "_ENV or _G set %s is forbidden", upvalue_name);
                        return false;
                        }
                        */
                    }
                    break;
                    default:
                        break;
                    }
                }

                // check sub protos
                for (i = 0; i < proto_size; i++)
                {
                    std::list<Proto*> sub_parents;
                    if (parents)
                    {
                        for (auto it = parents->begin(); it != parents->end(); ++it)
                        {
                            sub_parents.push_back(*it);
                        }
                    }
                    sub_parents.push_back(proto);
                    if (!check_contract_proto(L, proto->p[i], error, &sub_parents)) {
                        return false;
                    }
                }

                return true;
            }

            bool check_contract_bytecode_file(lua_State *L, const char *binary_filename)
            {
                LClosure *closure = luaU_undump_from_file(L, binary_filename, "check_contract");
                if (!closure)
                    return false;
                return check_contract_proto(L, closure->p);
            }

            bool check_contract_bytecode_stream(lua_State *L, UvmModuleByteStream *stream, char *error)
            {
                LClosure *closure = luaU_undump_from_stream(L, stream, "check_contract");
                if (!closure)
                    return false;
                return check_contract_proto(L, closure->p, error);
            }

			std::stack<contract_info_stack_entry> *get_using_contract_id_stack(lua_State *L, bool init_if_not_exist)
            {
				std::stack<contract_info_stack_entry> *contract_id_stack = nullptr;
				auto contract_id_stack_value_in_state_map = uvm::lua::lib::get_lua_state_value(L, GLUA_CONTRACT_API_CALL_STACK_STATE_MAP_KEY);
				if (!contract_id_stack_value_in_state_map.pointer_value)
				{
					if (!init_if_not_exist)
						return nullptr;
					contract_id_stack = new std::stack<contract_info_stack_entry>();
					if (!contract_id_stack)
					{
						lua_set_run_error(L, "allocate contract id stack memory error");
						return nullptr;
					}
					contract_id_stack_value_in_state_map.pointer_value = (void*)contract_id_stack;
					uvm::lua::lib::set_lua_state_value(L, GLUA_CONTRACT_API_CALL_STACK_STATE_MAP_KEY, contract_id_stack_value_in_state_map, UvmStateValueType::LUA_STATE_VALUE_POINTER);
				}
				else
					contract_id_stack = (std::stack<contract_info_stack_entry>*) (contract_id_stack_value_in_state_map.pointer_value);
				return contract_id_stack;
            }

			std::string get_current_using_contract_id(lua_State *L)
            {
				auto contract_id_stack = get_using_contract_id_stack(L, true);
				if (!contract_id_stack || contract_id_stack->size()<1)
					return "";
				return contract_id_stack->top().contract_id;
            }

            UvmTableMapP create_managed_lua_table_map(lua_State *L)
            {
                return luaL_create_lua_table_map_in_memory_pool(L);
            }

            char *malloc_managed_string(lua_State *L, size_t size, const char *init_data)
            {
                char *data = (char *)lua_malloc(L, size);
				memset(data, 0x0, size);
				if(nullptr != init_data)
					memcpy(data, init_data, strlen(init_data));
				return data;
            }

			char *malloc_and_copy_string(lua_State *L, const char *init_data)
            {
				return malloc_managed_string(L, sizeof(char) * (strlen(init_data) + 1), init_data);
            }

            UvmModuleByteStream *malloc_managed_byte_stream(lua_State *L)
            {
                return (UvmModuleByteStream*)lua_calloc(L, 1, sizeof(UvmModuleByteStream));
            }

            bool run_compiledfile(lua_State *L, const char *filename)
            {
                return lua_docompiledfile(L, filename) == 0;
            }
            bool run_compiled_bytestream(lua_State *L, void *stream_addr)
            {
                return lua_docompiled_bytestream(L, stream_addr) == 0;
            }
            void add_global_c_function(lua_State *L, const char *name, lua_CFunction func)
            {
                lua_pushcfunction(L, func);
                lua_setglobal(L, name);
            }
            void add_global_string_variable(lua_State *L, const char *name, const char *str)
            {
                if (!name || !str)
                    return;
                lua_pushstring(L, str);
                lua_setglobal(L, name);
            }
            void add_global_int_variable(lua_State *L, const char *name, lua_Integer num)
            {
                if (!name)
                    return;
                lua_pushinteger(L, num);
                lua_setglobal(L, name);
            }
            void add_global_number_variable(lua_State *L, const char *name, lua_Number num)
            {
                if (!name)
                    return;
                lua_pushnumber(L, num);
                lua_setglobal(L, name);
            }

            void add_global_bool_variable(lua_State *L, const char *name, bool value)
            {
                if (!name)
                    return;
                lua_pushboolean(L, value ? 1 : 0);
                lua_setglobal(L, name);
            }

			std::string get_global_string_variable(lua_State *L, const char *name)
            {
				if (!name)
					return "";
				lua_getglobal(L, name);
				if(lua_isnil(L, -1))
				{
					lua_pop(L, 1);
					return "";
				}
				std::string value(luaL_checkstring(L, -1));
				lua_pop(L, 1);
				return value;
            }

			void remove_global_variable(lua_State *L, const char *name)
            {
				if (!name)
					return;
				lua_pushnil(L);
				lua_setglobal(L, name);
            }

            void register_module(lua_State *L, const char *name, lua_CFunction openmodule_func)
            {
                luaL_requiref(L, name, openmodule_func, 0);
            }

			void add_system_extra_libs(lua_State *L)
            {
				register_module(L, "os", luaopen_os);
				register_module(L, "io", luaopen_io);
				register_module(L, "net", luaopen_net);
				register_module(L, "http", luaopen_http);
				register_module(L, "jsonrpc", luaopen_jsonrpc);
            }

			void reset_lvm_instructions_executed_count(lua_State *L)
            {
				int *insts_executed_count = get_lua_state_value(L, INSTRUCTIONS_EXECUTED_COUNT_LUA_STATE_MAP_KEY).int_pointer_value;
				if (insts_executed_count)
				{
					*insts_executed_count = 0;
				}
            }

            void increment_lvm_instructions_executed_count(lua_State *L, int add_count)
            {
              int *insts_executed_count = get_lua_state_value(L, INSTRUCTIONS_EXECUTED_COUNT_LUA_STATE_MAP_KEY).int_pointer_value;
              if (insts_executed_count)
              {
                *insts_executed_count = *insts_executed_count + add_count;
              }
            }

            int execute_contract_api(lua_State *L, const char *contract_name,
				const char *api_name, const char *arg1, std::string *result_json_string)
            {
                auto contract_address = malloc_managed_string(L, CONTRACT_ID_MAX_LENGTH + 1);
                memset(contract_address, 0x0, CONTRACT_ID_MAX_LENGTH + 1);
                size_t address_size = 0;
                uvm::lua::api::global_uvm_chain_api->get_contract_address_by_name(L, contract_name, contract_address, &address_size);
                if (address_size > 0)
                {
                    UvmStateValue value;
                    value.string_value = contract_address;
                    set_lua_state_value(L, STARTING_CONTRACT_ADDRESS, value, LUA_STATE_VALUE_STRING);
                }
                return lua_execute_contract_api(L, contract_name, api_name, arg1, result_json_string);
            }

            LUA_API int execute_contract_api_by_address(lua_State *L, const char *contract_address,
				const char *api_name, const char *arg1, std::string *result_json_string)
            {
                UvmStateValue value;
                auto str = malloc_managed_string(L, strlen(contract_address) + 1);
                memset(str, 0x0, strlen(contract_address) + 1);
                strncpy(str, contract_address, strlen(contract_address));
                value.string_value = str;
                set_lua_state_value(L, STARTING_CONTRACT_ADDRESS, value, LUA_STATE_VALUE_STRING);
                return lua_execute_contract_api_by_address(L, contract_address, api_name, arg1, result_json_string);
            }

            int execute_contract_api_by_stream(lua_State *L, UvmModuleByteStream *stream,
                const char *api_name, const char *arg1, std::string *result_json_string)
            {
                return lua_execute_contract_api_by_stream(L, stream, api_name, arg1, result_json_string);
            }

			bool is_calling_contract_init_api(lua_State *L)
            {
				const auto &state_node = get_lua_state_value_node(L, UVM_CONTRACT_INITING);
				return state_node.type == LUA_STATE_VALUE_INT && state_node.value.int_value > 0;
            }

			std::string get_starting_contract_address(lua_State *L)
            {
				auto starting_contract_address_node = uvm::lua::lib::get_lua_state_value_node(L, STARTING_CONTRACT_ADDRESS);
				if (starting_contract_address_node.type == UvmStateValueType::LUA_STATE_VALUE_STRING)
				{
					return starting_contract_address_node.value.string_value;
				}
				else
					return "";
            }

            bool execute_contract_init_by_address(lua_State *L, const char *contract_address, const char *arg1, std::string *result_json_string)
            {
                UvmStateValue state_value;
                state_value.int_value = 1;
                set_lua_state_value(L, UVM_CONTRACT_INITING, state_value, LUA_STATE_VALUE_INT);
                int status = execute_contract_api_by_address(L, contract_address, "init", arg1, result_json_string);
                state_value.int_value = 0;
                set_lua_state_value(L, UVM_CONTRACT_INITING, state_value, LUA_STATE_VALUE_INT);
                return status == 0;
            }
            bool execute_contract_start_by_address(lua_State *L, const char *contract_address, const char *arg1, std::string *result_json_string)
            {
                return execute_contract_api_by_address(L, contract_address, "start", arg1, result_json_string) == LUA_OK;
            }

            bool execute_contract_init(lua_State *L, const char *name, UvmModuleByteStreamP stream, const char *arg1, std::string *result_json_string)
            {
                UvmStateValue state_value;
                state_value.int_value = 1;
                set_lua_state_value(L, UVM_CONTRACT_INITING, state_value, LUA_STATE_VALUE_INT);
                int status = execute_contract_api_by_stream(L, stream, "init", arg1, result_json_string);
                state_value.int_value = 0;
                set_lua_state_value(L, UVM_CONTRACT_INITING, state_value, LUA_STATE_VALUE_INT);
                return status == 0;
            }
            bool execute_contract_start(lua_State *L, const char *name, UvmModuleByteStreamP stream, const char *arg1, std::string *result_json_string)
            {
                return execute_contract_api_by_stream(L, stream, "start", arg1, result_json_string) == LUA_OK;
            }

            std::string wrap_contract_name(const char *contract_name)
            {
                if (uvm::util::starts_with(contract_name, STREAM_CONTRACT_PREFIX) || uvm::util::starts_with(contract_name, ADDRESS_CONTRACT_PREFIX))
                    return std::string(contract_name);
                return std::string(CONTRACT_NAME_WRAPPER_PREFIX) + std::string(contract_name);
            }

            std::string unwrap_any_contract_name(const char *contract_name)
            {
                std::string wrappered_name_str(contract_name);
                if (wrappered_name_str.find(CONTRACT_NAME_WRAPPER_PREFIX) != std::string::npos)
                {
                    return wrappered_name_str.substr(strlen(CONTRACT_NAME_WRAPPER_PREFIX));
                }
                else if (wrappered_name_str.find(ADDRESS_CONTRACT_PREFIX) != std::string::npos)
                    return wrappered_name_str.substr(strlen(ADDRESS_CONTRACT_PREFIX));
                else if (wrappered_name_str.find(STREAM_CONTRACT_PREFIX) != std::string::npos)
                    return wrappered_name_str.substr(strlen(STREAM_CONTRACT_PREFIX));
                else
                    return wrappered_name_str;
            }

            std::string unwrap_contract_name(const char *wrappered_contract_name)
            {
                std::string wrappered_name_str(wrappered_contract_name);
                if (wrappered_name_str.find(CONTRACT_NAME_WRAPPER_PREFIX) != std::string::npos)
                {
                    return wrappered_name_str.substr(strlen(CONTRACT_NAME_WRAPPER_PREFIX));
                }
                else
                    return wrappered_name_str;
            }

            /**
            * only used in contract api directly
            */
            const char *get_contract_id_in_api(lua_State *L)
            {
				const auto &contract_id = get_current_using_contract_id(L);
				auto contract_id_str = malloc_and_copy_string(L, contract_id.c_str());
				return contract_id_str;
            }

        }
    }
}