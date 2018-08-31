/**
 * lua module injector header in uvm
 * @author zoowii@uvm
 */

#ifndef uvm_api_h
#define uvm_api_h

#include <uvm/lprefix.h>

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <unordered_map>
#include <memory>

#include <uvm/lua.h>
#include <jsondiff/jsondiff.h>
#include <jsondiff/exceptions.h>

#define LOG_INFO(...)  fprintf(stderr, "[INFO] " ##__VA_ARGS__)

#define ERROR_INFO(...) fprintf(stderr, "[ERROR] " ##__VA_ARGS__)

#define LUA_MODULE_BYTE_STREAM_BUF_SIZE 1024*1024

#define LUA_VM_EXCEPTION_STRNG_MAX_LENGTH 256

#define LUA_EXCEPTION_MULTILINE_STRNG_MAX_LENGTH 4096

#define CONTRACT_ID_MAX_LENGTH 50

#define CONTRACT_NAME_MAX_LENGTH 50

#define CONTRACT_MAX_OFFLINE_API_COUNT	1024

#define LUA_FUNCTION_MAX_LOCALVARS_COUNT 128

#define LUA_MAX_LOCALVARNAME_LENGTH	128

// emiteventTypeName
#define EMIT_EVENT_NAME_MAX_COUNT 50

//  
#define CONTRACT_LEVEL_TEMP 1
#define CONTRACT_LEVEL_FOREVER 2

//  
#define CONTRACT_STATE_VALID 1
#define CONTRACT_STATE_DELETED 2

// blockchain
#define USE_TYPE_CHECK true 

// storagerecord
#define CONTRACT_STORAGE_PROPERTIES_MAX_COUNT 256

// uvmAPIAPI
#define CHAIN_GLUA_API_EACH_INSTRUCTIONS_COUNT 50

// ，register_object_in_pool
enum UvmOutsideObjectTypes
{
	OUTSIDE_STREAM_STORAGE_TYPE = 0
};

typedef enum UvmStorageValueType
{
    LVALUE_INTEGER = 0, LVALUE_NUMBER = 1, LVALUE_TABLE = 2, LVALUE_STRING = 3,
    LVALUE_BOOLEAN = 4, LVALUE_USERDATA = 5, LVALUE_NIL = 6, LVALUE_ARRAY = 7
    ,LVALUE_STREAM = 9

	,LVALUE_NOT_SUPPORT = 100
} UvmStorageValueType;

namespace uvm {
    namespace blockchain {

        enum StorageValueTypes
        {
            storage_value_null = 0,
            storage_value_int = 1,
            storage_value_number = 2,
            storage_value_bool = 3,
            storage_value_string = 4,
			storage_value_stream = 5,

            storage_value_unknown_table = 50,
            storage_value_int_table = 51,
            storage_value_number_table = 52,
            storage_value_bool_table = 53,
            storage_value_string_table = 54,
			storage_value_stream_table = 55,

            storage_value_unknown_array = 100,
            storage_value_int_array = 101,
            storage_value_number_array = 102,
            storage_value_bool_array = 103,
            storage_value_string_array = 104,
			storage_value_stream_array = 105,

            storage_value_userdata = 201,
            storage_value_not_support = 202
        };

		inline bool is_null_storage_value_type(StorageValueTypes type)
		{
			return type == StorageValueTypes::storage_value_null;
		}


        inline bool is_any_base_storage_value_type(StorageValueTypes type)
        {
            return ((type >= StorageValueTypes::storage_value_int)
                && (type <= StorageValueTypes::storage_value_stream));
        }

        inline bool is_any_table_storage_value_type(StorageValueTypes type)
        {
            return (type >= StorageValueTypes::storage_value_unknown_table)
                && (type <= StorageValueTypes::storage_value_stream_table);
        }

        inline bool is_any_array_storage_value_type(StorageValueTypes type)
        {
            return (type >= StorageValueTypes::storage_value_unknown_array)
                && (type <= StorageValueTypes::storage_value_stream_array);
        }

        inline StorageValueTypes get_storage_base_type(StorageValueTypes type)
        {
            switch (type)
            {
            case StorageValueTypes::storage_value_bool:
            case StorageValueTypes::storage_value_bool_array:
            case StorageValueTypes::storage_value_bool_table:
                return StorageValueTypes::storage_value_bool;
            case StorageValueTypes::storage_value_int:
            case StorageValueTypes::storage_value_int_array:
            case StorageValueTypes::storage_value_int_table:
                return StorageValueTypes::storage_value_int;
            case StorageValueTypes::storage_value_number:
            case StorageValueTypes::storage_value_number_array:
            case StorageValueTypes::storage_value_number_table:
                return StorageValueTypes::storage_value_number;
            case StorageValueTypes::storage_value_string:
            case StorageValueTypes::storage_value_string_array:
            case StorageValueTypes::storage_value_string_table:
                return StorageValueTypes::storage_value_string;
			case StorageValueTypes::storage_value_stream:
			case StorageValueTypes::storage_value_stream_array:
			case StorageValueTypes::storage_value_stream_table:
				return StorageValueTypes::storage_value_stream;
            default:
                return StorageValueTypes::storage_value_null;
            }
        }

        inline StorageValueTypes get_item_type_in_table_or_array(StorageValueTypes type)
        {
            switch (type)
            {
            case StorageValueTypes::storage_value_int_table:
            case StorageValueTypes::storage_value_int_array:
                return StorageValueTypes::storage_value_int;
            case StorageValueTypes::storage_value_bool_table:
            case StorageValueTypes::storage_value_bool_array:
                return StorageValueTypes::storage_value_bool;
            case StorageValueTypes::storage_value_number_table:
            case StorageValueTypes::storage_value_number_array:
                return StorageValueTypes::storage_value_number;
            case StorageValueTypes::storage_value_string_table:
            case StorageValueTypes::storage_value_string_array:
                return StorageValueTypes::storage_value_string;
			case StorageValueTypes::storage_value_stream_table:
			case StorageValueTypes::storage_value_stream_array:
				return StorageValueTypes::storage_value_stream;
            case StorageValueTypes::storage_value_unknown_table:
            case StorageValueTypes::storage_value_unknown_array:
                return StorageValueTypes::storage_value_null;
            default:
                return StorageValueTypes::storage_value_null;
            }
        }

    }
}

enum UvmTypeInfoEnum
{
	LTI_OBJECT = 0,
	LTI_NIL = 1,
	LTI_STRING = 2,
	LTI_INT = 3,
	LTI_NUMBER = 4,
	LTI_BOOL = 5,
	LTI_TABLE = 6,
	LTI_FUNCTION = 7, // coroutine as function type
	LTI_UNION = 8,
	LTI_RECORD = 9, // , type <RecordName> = { <name> : <type> , ... }
	LTI_GENERIC = 10, // ，
	LTI_ARRAY = 11, // ，
	LTI_MAP = 12, // ，，key
	LTI_LITERIAL_TYPE = 13, // ，literal type //union，: "Male" | "Female"
	LTI_STREAM = 14, // Stream，
	LTI_UNDEFINED = 100 // ，undefined
};

class UvmModuleByteStream {
public:
    bool is_bytes;
    std::vector<char> buff;
    std::vector<std::string> contract_apis;
    std::vector<std::string> offline_apis;
    std::vector<std::string> contract_emit_events;
    std::string contract_id;
    std::string contract_name;
    int  contract_level;
    int  contract_state;
    // storage
    std::map<std::string, uvm::blockchain::StorageValueTypes> contract_storage_properties;

	// APIAPI
	std::map<std::string, std::vector<UvmTypeInfoEnum>> contract_api_arg_types;

public:
    UvmModuleByteStream();
    virtual ~UvmModuleByteStream();
};

typedef UvmModuleByteStream *UvmModuleByteStreamP;

class UvmContractInfo
{
public:
    std::vector<std::string> contract_apis;
};

#define UVM_API_NO_ERROR 0
#define UVM_API_SIMPLE_ERROR 1
#define UVM_API_MEMORY_ERROR 2
#define UVM_API_LVM_ERROR 3
#define UVM_API_PARSER_ERROR 4
#define UVM_API_COMPILE_ERROR 5
#define UVM_API_LVM_LIMIT_OVER_ERROR 6
#define UVM_API_THROW_ERROR 7   // uvm


// storage structs
#define LUA_STORAGE_CHANGELIST_KEY "__lua_storage_changelist__"
#define LUA_STORAGE_READ_TABLES_KEY "__lua_storage_read_tables__"

#define GLUA_OUTSIDE_OBJECT_POOLS_KEY "__uvm_outside_object_pools__"

#define lua_storage_is_table(t) (uvm::blockchain::is_any_table_storage_value_type(t)||uvm::blockchain::is_any_array_storage_value_type(t))
#define lua_storage_is_array(t) (uvm::blockchain::is_any_array_storage_value_type(t))
#define lua_storage_is_hashtable(t) (uvm::blockchain::is_any_table_storage_value_type(t))

struct UvmStorageValue;

struct lua_table_binary_function
{	// base class for binary functions
    typedef std::string first_argument_type;
    typedef std::string second_argument_type;
    typedef bool result_type;
};

struct lua_table_less
    : public lua_table_binary_function
{	// functor for operator<
    bool operator()(const std::string& _Left, const std::string& _Right) const
    {	// apply operator< to operands
        if (_Left.length() != _Right.length())
            return _Left.length() < _Right.length();
        return _Left < _Right;
    }
};

typedef std::map<std::string, struct UvmStorageValue, struct lua_table_less> UvmTableMap;

typedef UvmTableMap* UvmTableMapP;

struct UvmStorageChangeItem;


typedef union UvmStorageValueUnion
{
    lua_Integer int_value;
    lua_Number number_value;
    bool bool_value;
    char *string_value;
    UvmTableMapP table_value;
    void *userdata_value;
    void *pointer_value;
} UvmStorageValueUnion;

typedef struct UvmStorageValue
{
    uvm::blockchain::StorageValueTypes type;
    union UvmStorageValueUnion value;
    inline UvmStorageValue()
    {
        type = uvm::blockchain::StorageValueTypes::storage_value_null;
    }
    inline static UvmStorageValue from_int(int val)
    {
        UvmStorageValue sv;
        sv.type = uvm::blockchain::StorageValueTypes::storage_value_int;
        sv.value.int_value = val;
        return sv;
    }
    inline static UvmStorageValue from_string(char *val)
    {
        UvmStorageValue sv;
        sv.type = uvm::blockchain::StorageValueTypes::storage_value_string;
        sv.value.string_value = val;
        return sv;
    }

	// 
	inline void try_parse_type(uvm::blockchain::StorageValueTypes new_type)
    {
	    switch(new_type)
	    {
		case uvm::blockchain::StorageValueTypes::storage_value_int:
			try_parse_to_int_type();
			break;
		case uvm::blockchain::StorageValueTypes::storage_value_number:
			try_parse_to_number_type();
			break;
		default:
			break;
	    }
    }

	// int
	inline void try_parse_to_int_type()
    {
	    if(type == uvm::blockchain::StorageValueTypes::storage_value_number)
	    {
			value.int_value = (lua_Integer)value.number_value;
			type = uvm::blockchain::StorageValueTypes::storage_value_int;
	    }
    }

	// number
	inline void try_parse_to_number_type()
    {
		if (type == uvm::blockchain::StorageValueTypes::storage_value_int)
		{
			value.number_value = (lua_Number)value.int_value;
			type = uvm::blockchain::StorageValueTypes::storage_value_number;
		}
    }

    inline static bool is_same_base_type_with_type_parse(uvm::blockchain::StorageValueTypes type1, uvm::blockchain::StorageValueTypes type2)
    {
        return type1 == type2;
    }
    inline bool equals(UvmStorageValue &other)
    {
        if (type != other.type)
            return false;
        switch (type)
        {
        case uvm::blockchain::StorageValueTypes::storage_value_string:
            return strcmp(value.string_value, other.value.string_value) == 0;
        case uvm::blockchain::StorageValueTypes::storage_value_int:
            return value.int_value == other.value.int_value;
        case uvm::blockchain::StorageValueTypes::storage_value_number:
            return value.number_value == other.value.number_value;
        case uvm::blockchain::StorageValueTypes::storage_value_bool:
            return value.bool_value == other.value.bool_value;
            return value.userdata_value == other.value.userdata_value;
        case uvm::blockchain::StorageValueTypes::storage_value_null:
            return true;
        default: {
            if (uvm::blockchain::is_any_table_storage_value_type(type)
                || uvm::blockchain::is_any_array_storage_value_type(type))
            {
                if (value.table_value->size() != other.value.table_value->size())
                    return false;

                for (const auto &p1 : *(value.table_value))
                {
                    auto found = other.value.table_value->find(p1.first);
                    if (found == other.value.table_value->end())
                        return false;
                    auto this_item = p1.second;
                    auto other_item = found->second;
                    if (!this_item.equals(other_item))
                        return false;
                }
                return true;
            }
            return false;
        }
        }
    }
} UvmStorageValue;

typedef struct UvmStorageChangeItem
{
    std::string contract_id;
    std::string key;
	jsondiff::DiffResult diff;
	std::string fast_map_key;
	bool is_fast_map = false;
    struct UvmStorageValue before;
    struct UvmStorageValue after;

	std::string full_key() const {
		if (is_fast_map)
			return key + "." + fast_map_key;
		else
			return key;
	}

} UvmStorageChangeItem;



typedef std::list<UvmStorageChangeItem> UvmStorageChangeList;

typedef std::list<UvmStorageChangeItem> UvmStorageTableReadList;

struct UvmStorageValue lua_type_to_storage_value_type(lua_State *L, int index);

bool luaL_commit_storage_changes(lua_State *L);

bool lua_push_storage_value(lua_State *L, const UvmStorageValue &value);

UvmStorageValue json_to_uvm_storage_value(lua_State *L, jsondiff::JsonValue json_value);
jsondiff::JsonValue uvm_storage_value_to_json(UvmStorageValue value);

typedef std::unordered_map<std::string, UvmStorageChangeItem> ContractChangesMap;

typedef std::shared_ptr<ContractChangesMap> ContractChangesMapP;

typedef std::unordered_map<std::string, ContractChangesMapP> AllContractsChangesMap;


namespace uvm {
    namespace blockchain{
        struct Code;
    }

    namespace lua {
        namespace api {

          class IUvmChainApi
          {
          public:
            /**
            * check whether the contract apis limit over, in this lua_State
            * @param L the lua stack
            * @return TRUE(1 or not 0) if over limit(will break the vm), FALSE(0) if not over limit
            */
            virtual int check_contract_api_instructions_over_limit(lua_State *L) = 0;

            /**
             * whether exception happen in L
             */
            virtual bool has_exception(lua_State *L) = 0;

            /**
            * clear exception marked
            */
            virtual void clear_exceptions(lua_State *L) = 0;


            /**
            * when exception happened, use this api to tell uvm
            * @param L the lua stack
            * @param code error code, 0 is OK, other is different error
            * @param error_format error info string, will be released by lua
            * @param ... error arguments
            */
            virtual void throw_exception(lua_State *L, int code, const char *error_format, ...) = 0;

            /**
            * get contract info stored before from uvm api
            * @param name contract name
            * @param contract_info_ret this api save the contract's api name array here if found, this var will be free by this api
            * @return TRUE(1 or not 0) if success, FALSE(0) if failed
            */
            virtual int get_stored_contract_info(lua_State *L, const char *name, std::shared_ptr<UvmContractInfo> contract_info_ret) = 0;

            virtual int get_stored_contract_info_by_address(lua_State *L, const char *address, std::shared_ptr<UvmContractInfo> contract_info_ret) = 0;

            virtual std::shared_ptr<UvmModuleByteStream> get_bytestream_from_code(lua_State *L, const uvm::blockchain::Code& code) = 0;
            /**
            * load contract lua byte stream from uvm api
            */
            virtual std::shared_ptr<UvmModuleByteStream> open_contract(lua_State *L, const char *name) = 0;

            virtual std::shared_ptr<UvmModuleByteStream> open_contract_by_address(lua_State *L, const char *address) = 0;

            /**
             * get contract address/id from uvm by contract name
             */
            virtual void get_contract_address_by_name(lua_State *L, const char *name, char *address, size_t *address_size) = 0;

            /*
             * check whether the contract exist
             */
            virtual bool check_contract_exist(lua_State *L, const char *name) = 0;

            /**
             * check contract exist by ID string address
             */
            virtual bool check_contract_exist_by_address(lua_State *L, const char *address) = 0;

            /**
             * register new storage name of contract to uvm
             */
            virtual bool register_storage(lua_State *L, const char *contract_name, const char *name) = 0;

            virtual UvmStorageValue get_storage_value_from_uvm(lua_State *L, const char *contract_name,
				const std::string& name, const std::string& fast_map_key, bool is_fast_map) = 0;

            virtual UvmStorageValue get_storage_value_from_uvm_by_address(lua_State *L, const char *contract_address,
				const std::string& name, const std::string& fast_map_key, bool is_fast_map) = 0;

            /**
             * after lua merge storage changes in lua_State, use the function to store the merged changes of storage to uvm
             */
            virtual bool commit_storage_changes_to_uvm(lua_State *L, AllContractsChangesMap &changes) = 0;

            /**
             * lua_State（），
             * lua_State
             * uvm/，，lightuserdatauvm
             */
            virtual intptr_t register_object_in_pool(lua_State *L, intptr_t object_addr, UvmOutsideObjectTypes type) = 0;

            /**
             * (register_object_in_pool，)lua_State（)
             * ，，0
             */
            virtual intptr_t is_object_in_pool(lua_State *L, intptr_t object_key, UvmOutsideObjectTypes type) = 0;

            /**
             * lua_State
             */
            virtual void release_objects_in_pool(lua_State *L) = 0;

            /************************************************************************/
            /* transfer asset from contract by account address                      */
            /************************************************************************/
            virtual lua_Integer transfer_from_contract_to_address(lua_State *L, const char *contract_address, const char *to_address,
              const char *asset_type, int64_t amount) = 0;

            /************************************************************************/
            /* transfer asset from contract by account name on chain                */
            /************************************************************************/
            virtual lua_Integer transfer_from_contract_to_public_account(lua_State *L, const char *contract_address, const char *to_account_name,
              const char *asset_type, int64_t amount) = 0;

            virtual int64_t get_contract_balance_amount(lua_State *L, const char *contract_address, const char* asset_symbol) = 0;
            virtual int64_t get_transaction_fee(lua_State *L) = 0;
            virtual uint32_t get_chain_now(lua_State *L) = 0;
            virtual uint32_t get_chain_random(lua_State *L) = 0;
            virtual std::string get_transaction_id(lua_State *L) = 0;
            virtual uint32_t get_header_block_num(lua_State *L) = 0;
            virtual uint32_t wait_for_future_random(lua_State *L, int next) = 0;

            virtual int32_t get_waited(lua_State *L, uint32_t num) = 0;

            virtual void emit(lua_State *L, const char* contract_id, const char* event_name, const char* event_param) = 0;

			virtual bool is_valid_address(lua_State *L, const char *address_str) = 0;
			virtual bool is_valid_contract_address(lua_State *L, const char *address_str) = 0;
			virtual const char *get_system_asset_symbol(lua_State *L) = 0;
			virtual uint64_t get_system_asset_precision(lua_State *L) = 0;

			virtual std::vector<unsigned char> hex_to_bytes(const std::string& hex_string) = 0;
			virtual std::string bytes_to_hex(std::vector<unsigned char> bytes) = 0;
			virtual std::string sha256_hex(const std::string& hex_string) = 0;
			virtual std::string sha1_hex(const std::string& hex_string) = 0;
			virtual std::string sha3_hex(const std::string& hex_string) = 0;
			virtual std::string ripemd160_hex(const std::string& hex_string) = 0;
          };


          extern IUvmChainApi *global_uvm_chain_api;

        }
    }
}


#endif