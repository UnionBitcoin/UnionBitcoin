#ifndef uvm_lib_h
#define uvm_lib_h

#include <uvm/lprefix.h>


#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <list>
#include <vector>
#include <stack>
#include <string>
#include <unordered_map>
#include <set>

#include <uvm/lua.h>
#include <uvm/lhashmap.h>
#include <uvm/lstate.h>
#include <uvm/llimits.h>
#include <uvm/lobject.h>
#include <uvm/uvm_api.h>

#define BOOL_VAL(val) ((val)>0?true:false)

#define ADDRESS_CONTRACT_PREFIX "@pointer_"

#define STREAM_CONTRACT_PREFIX "@stream_"

/**
 * wrapper contract name, so it wan't conflict with lua inner module names
 */
#define CONTRACT_NAME_WRAPPER_PREFIX "@g_"

#define CURRENT_CONTRACT_NAME	"@self"

#define UVM_CONTRACT_INITING "uvm_contract_ininting"

#define STARTING_CONTRACT_ADDRESS "starting_contract_address"

#define LUA_STATE_DEBUGGER_INFO	"lua_state_debugger_info"


/**
* in lua_State scope, share some values, after close lua_State, you must release these shared values
* all keys and values need copied to shared pool
*/
enum UvmStateValueType {
    LUA_STATE_VALUE_INT = 1,
    LUA_STATE_VALUE_INT_POINTER = 2,
    LUA_STATE_VALUE_STRING = 3,
    LUA_STATE_VALUE_POINTER = 4,
    LUA_STATE_VALUE_nullptr = 0
};

typedef union _UvmStateValue {
    int int_value;
    int *int_pointer_value;
    const char *string_value;
    void *pointer_value;
} UvmStateValue;

typedef struct _UvmStateValueNode {
    enum UvmStateValueType type;
    UvmStateValue value;
} UvmStateValueNode;


namespace uvm
{
    namespace lua
    {
        namespace lib
        {

			// contract's special api names
			extern std::vector<std::string> contract_special_api_names;
			// special contract api names with int argument
			extern std::vector<std::string> contract_int_argument_special_api_names;
			// special contract api names with string argument
			extern std::vector<std::string> contract_string_argument_special_api_names;

            class UvmStateScope
            {
            private:
                lua_State *_L;
                bool _use_contract;
            public:
                UvmStateScope(bool use_contract = true);
                UvmStateScope(const UvmStateScope &other);
                ~UvmStateScope();

                inline lua_State *L() const { return this->_L; }

                /************************************************************************/
                /* use other to replace stdin                                           */
                /************************************************************************/
                void change_in_file(FILE *in);
                /************************************************************************/
                /* use other to replace stdout                                          */
                /************************************************************************/
                void change_out_file(FILE *out);
                /************************************************************************/
                /* use other to replace stderr                                          */
                /************************************************************************/
                void change_err_file(FILE *err);

                /************************************************************************/
                /* whether in sandbox(need every api check)                             */
                /************************************************************************/
                bool in_sandbox() const;
                void enter_sandbox();
                void exit_sandbox();

                /************************************************************************/
                /* commit all storage changes happened in the lua stack                 */
                /************************************************************************/
                bool commit_storage_changes();

                /************************************************************************/
                /* every lua stack has a key=>value map                                 */
                /************************************************************************/
                UvmStateValueNode get_value_node(const char *key);
                /************************************************************************/
                /* every lua stack has a key=>value map                                 */
                /************************************************************************/
                UvmStateValue get_value(const char *key);
                /************************************************************************/
                /* every lua stack has a key=>value map                                 */
                /************************************************************************/
                void set_value(const char *key, UvmStateValue value, enum UvmStateValueType type);

                /************************************************************************/
                /* set how many lua vm instructions can run in the lua stack            */
                /************************************************************************/
                void set_instructions_limit(int limit);
                /************************************************************************/
                /* the the max limit instructions count in the lua stack                */
                /************************************************************************/
                int get_instructions_limit() const;
                /************************************************************************/
                /* get how many lua vm instructions ran now in the lua stack            */
                /************************************************************************/
                int get_instructions_executed_count() const;
                /************************************************************************/
                /* check whether the uvm apis over limit(maybe uvm limit api called count) */
                /************************************************************************/
                int check_uvm_contract_api_instructions_over_limit();

                /************************************************************************/
                /* notify the lua stack to stop                                         */
                /************************************************************************/
                void notify_stop();
                /************************************************************************/
                /* whether the lua stack waiting to stop                                */
                /************************************************************************/
                bool check_notified_stop();
                /************************************************************************/
                /* cancel stopping of the lua stack                                     */
                /************************************************************************/
                void resume_running();

				// ，，IO，
				void add_system_extra_libs();

                bool run_compiledfile(const char *filename);
                bool run_compiled_bytestream(void *stream_addr);

                /************************************************************************/
                /* bind C function to Lua global variable                               */
                /************************************************************************/
                void add_global_c_function(const char *name, lua_CFunction func);
                void add_global_string_variable(const char *name, const char *str);
                void add_global_int_variable(const char *name, lua_Integer num);
                void add_global_bool_variable(const char *name, bool value);
                void add_global_number_variable(const char *name, lua_Number num);
                /************************************************************************/
                /* bind C module to Lua, then user can use require '<module_name>' to load the module */
                /************************************************************************/
                void register_module(const char *name, lua_CFunction openmodule_func);

                /************************************************************************/
                /* execute contract's api by contract name and api name                 */
                /************************************************************************/
                int execute_contract_api(const char *contract_name, const char *api_name, const char *arg1, std::string *result_json_string);
                /************************************************************************/
                /* execute contract's api by contract address and api name              */
                /************************************************************************/
                int execute_contract_api_by_address(const char *address, const char *api_name, const char *arg1, std::string *result_json_string);

                /************************************************************************/
                /* execute contract's init function by byte stream.
                   if you use this, notice the life cycle of the stream */
                /************************************************************************/
                bool execute_contract_init(UvmModuleByteStreamP stream, const char *arg1, std::string *result_json_string);
                /************************************************************************/
                /* execute contract's start function by byte stream,
                   if you use this, notice the life cycle of the stream */
                /************************************************************************/
                bool execute_contract_start(UvmModuleByteStreamP stream, const char *arg1, std::string *result_json_string);
                /************************************************************************/
                /* execute contract's init function by contract address                 */
                /************************************************************************/
                bool execute_contract_init_by_address(const char *contract_address, const char *arg1, std::string *result_json_string);
                /************************************************************************/
                /* execute contract's start function by contract address                */
                /************************************************************************/
                bool execute_contract_start_by_address(const char *contract_address, const char *arg1, std::string *result_json_string);

                /************************************************************************/
                /* whether contract's bytecode stream right                             */
                /************************************************************************/
                bool check_contract_bytecode_stream(UvmModuleByteStreamP stream);
            };

			class UvmByteStream
			{
			private:
				size_t _pos;
				std::vector<char> _buff;
			public:
				inline UvmByteStream(): _pos(0) {}
				inline virtual ~UvmByteStream(){}

				inline bool eof() const
				{
					return _pos >= _buff.size();
				}

				inline char current() const
				{
					return eof() ? -1 : _buff[_pos];
				}

				inline bool next()
				{
					if (eof())
						return false;
					++_pos;
					return true;
				}

				inline size_t pos() const
				{
					return _pos;
				}

				inline void reset_pos()
				{
					_pos = 0; 
				}

				inline size_t size() const
				{
					return _buff.size();
				}

				inline std::vector<char>::const_iterator begin() const
				{
					return _buff.begin();
				}

				inline std::vector<char>::const_iterator end() const
				{
					return _buff.end();
				}

				inline void push(char c)
				{
					_buff.push_back(c);
				}

				inline void push_some(std::vector<char> const &data)
				{
					_buff.resize(_buff.size() + data.size());
					memcpy(_buff.data() + _buff.size(), data.data(), sizeof(char) * data.size());
				}

				inline bool equals(UvmByteStream *other)
				{
                    if (!other)
                        return false;
					if (this == other)
						return true;
					if (this->size() != other->size())
						return false;
					return this->_buff == other->_buff;
				}

			};

            lua_State *create_lua_state(bool use_contract = true);

            bool commit_storage_changes(lua_State *L);

            void close_lua_state(lua_State *L);

            /**
            * share some values in L
            */
            void close_all_lua_state_values();
            void close_lua_state_values(lua_State *L);

            UvmStateValueNode get_lua_state_value_node(lua_State *L, const char *key);
            UvmStateValue get_lua_state_value(lua_State *L, const char *key);
            void set_lua_state_instructions_limit(lua_State *L, int limit);

            int get_lua_state_instructions_limit(lua_State *L);

            int get_lua_state_instructions_executed_count(lua_State *L);

            void enter_lua_sandbox(lua_State *L);

            void exit_lua_sandbox(lua_State *L);

            bool check_in_lua_sandbox(lua_State *L);

            /**
             * notify lvm to stop running the lua stack
             */
            void notify_lua_state_stop(lua_State *L);

            /**
             * check whether the lua state notified stop before
             */
            bool check_lua_state_notified_stop(lua_State *L);

            /**
             * resume lua_State to be available running again
             */
            void resume_lua_state_running(lua_State *L);

            void set_lua_state_value(lua_State *L, const char *key, UvmStateValue value, enum UvmStateValueType type);

            UvmTableMapP create_managed_lua_table_map(lua_State *L);

            char *malloc_managed_string(lua_State *L, size_t size, const char *init_data=nullptr);

			char *malloc_and_copy_string(lua_State *L, const char *init_data);

            UvmModuleByteStream *malloc_managed_byte_stream(lua_State *L);

            bool run_compiledfile(lua_State *L, const char *filename);
            bool run_compiled_bytestream(lua_State *L, void *stream_addr);

            void add_global_c_function(lua_State *L, const char *name, lua_CFunction func);
            void add_global_string_variable(lua_State *L, const char *name, const char *str);
            void add_global_int_variable(lua_State *L, const char *name, lua_Integer num);
            void add_global_number_variable(lua_State *L, const char *name, lua_Number num);
            void add_global_bool_variable(lua_State *L, const char *name, bool value);

			std::string get_global_string_variable(lua_State *L, const char *name);
			
			void remove_global_variable(lua_State *L, const char *name);
            void register_module(lua_State *L, const char *name, lua_CFunction openmodule_func);
			// add more system lib, eg. net, http, io, etc.
			void add_system_extra_libs(lua_State *L);

			// reset gas used
			void reset_lvm_instructions_executed_count(lua_State *L);

            // lvmadd_count
            void increment_lvm_instructions_executed_count(lua_State *L, int add_count);

            int execute_contract_api(lua_State *L, const char *contract_name, const char *api_name, const char *arg1, std::string *result_json_string);

			int execute_contract_api_by_stream(lua_State *L, UvmModuleByteStreamP stream, const char *api_name, const char *arg1, std::string *result_json_string);

            const char *get_contract_id_in_api(lua_State *L);

            // UvmStorageValue luvm_get_storage(lua_State *L, const char *contract_id, const char *name);
            
			//bool luvm_set_storage(lua_State *L, const char *contract_id, const char *name, UvmStorageValue value);

            void free_bytecode_stream(UvmModuleByteStreamP stream);

            /**
            * diff from execute_contract_api is the contract bytestream is loaded by pointer and uvm
            */
            LUA_API int execute_contract_api_by_address(lua_State *L, const char *contract_address,
				const char *api_name, const char *arg1, std::string *result_json_string);

            bool execute_contract_init(lua_State *L, const char *name, UvmModuleByteStreamP stream, const char *arg1, std::string *result_json_string);
            bool execute_contract_start(lua_State *L, const char *name, UvmModuleByteStreamP stream, const char *arg1, std::string *result_json_string);

            bool execute_contract_init_by_address(lua_State *L, const char *contract_address, const char *arg1, std::string *result_json_string);
            bool execute_contract_start_by_address(lua_State *L, const char *contract_address, const char *arg1, std::string *result_json_string);

			bool call_last_contract_api(lua_State* L, const std::string& contract_id, const std::string& api_name, const std::string& api_arg, std::string* result_json_string);

			// whether the head of call stack is contract's init API
			bool is_calling_contract_init_api(lua_State *L);

			// get the head contract address of call stack
			std::string get_starting_contract_address(lua_State *L);

			struct contract_info_stack_entry {
				std::string contract_id;
				std::string api_name;
			};
			// contract id stack of API call stack
			std::stack<contract_info_stack_entry> *get_using_contract_id_stack(lua_State *L, bool init_if_not_exist=true);

			// get top contract address of call stack
			std::string get_current_using_contract_id(lua_State *L);

            /**
            * load one chunk from lua bytecode file
            */
            LClosure* luaU_undump_from_file(lua_State *L, const char *binary_filename, const char* name);

            /**
             * load one chunk from lua bytecode stream
             */
            LClosure *luaU_undump_from_stream(lua_State *L, UvmModuleByteStreamP stream, const char *name);

            /**
             * secure apis
             */

            /**
             * check contract lua bytecode file(whether safe)
             */
            bool check_contract_bytecode_file(lua_State *L, const char *binary_filename);

            /**
             * check contract bytecode(whether safe)
             */
            bool check_contract_bytecode_stream(lua_State *L, UvmModuleByteStreamP stream, char *error = nullptr);

            /**
             * check contract lua bytecode proto is right(whether safe)
             */
            bool check_contract_proto(lua_State *L, Proto *proto, char *error = nullptr, std::list<Proto*> *parents = nullptr);

            std::string wrap_contract_name(const char *contract_name);

            std::string unwrap_any_contract_name(const char *contract_name);

            std::string unwrap_contract_name(const char *wrappered_contract_name);

            const std::string get_typed_lua_lib_code();

#define lerror_set(L, error, error_format, ...) do {			 \
     if (nullptr != error && strlen(error) < 1)					\
     {						\
        char error_msg[LUA_COMPILE_ERROR_MAX_LENGTH+1];		 \
        memset(error_msg, 0x0, sizeof(error_msg));		     \
        snprintf(error_msg, LUA_COMPILE_ERROR_MAX_LENGTH, error_format, ##__VA_ARGS__);				\
        error_msg[LUA_COMPILE_ERROR_MAX_LENGTH-1] = '\-';       \
        memcpy(error, error_msg, sizeof(char)*(1 + strlen(error_msg)));								\
     }												\
     global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, error_format, ##__VA_ARGS__);		\
} while(0)

#define lcompile_error_set(L, error, error_format, ...) do {	   \
    lerror_set(L, error, error_format, ##__VA_ARGS__);		   \
    if (strlen(L->compile_error) < 1)						  \
    {														   \
        snprintf(L->compile_error, LUA_COMPILE_ERROR_MAX_LENGTH-1, error_format, ##__VA_ARGS__);			\
    }															\
} while(0)

#define lcompile_error_get(L, error) {					   \
    if (error && strlen(error)<1 && strlen(L->compile_error) > 0)			\
    {															   \
        memcpy(error, L->compile_error, sizeof(char) * (strlen(L->compile_error) + 1));			\
    } else if(nullptr != error && strlen(error)<1 && strlen(L->runerror) > 0)			   \
	{			\
		 memcpy(error, L->runerror, sizeof(char) * (strlen(L->runerror) + 1));			\
			}		\
}

#define lmalloc_error(L) do { \
		global_uvm_chain_api->throw_exception(L, UVM_API_SIMPLE_ERROR, "malloc memory error"); \
	} while(0)

#define GLUA_TYPE_NAMESPACE_PREFIX "$type$"
#define GLUA_TYPE_NAMESPACE_PREFIX_WRAP(name) "$type$" #name ""

        }
    }
}


#endif