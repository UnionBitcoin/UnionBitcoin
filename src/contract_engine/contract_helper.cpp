#include <contract_engine/contract_helper.hpp>
#include <cstdlib>
#include <vector>
#include <fc/array.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <fc/crypto/elliptic.hpp>
#include <fc/crypto/base58.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <boost/uuid/sha1.hpp>
#include <exception>

int gpcread(void* ptr, size_t element_size, size_t count, GpcBuffer* gpc_buffer)
{
    if(!ptr)
        return 0;
    if(gpc_buffer->eof() || ((gpc_buffer->data.size()-gpc_buffer->pos) < element_size))
        return 0;
    if(element_size*count <= (gpc_buffer->data.size()-gpc_buffer->pos))
    {
        memcpy(ptr, gpc_buffer->data.data()+gpc_buffer->pos, element_size*count);
        gpc_buffer->pos += element_size*count;
        return count;
    }
    size_t available_count = (gpc_buffer->data.size()-gpc_buffer->pos) / element_size;
    memcpy(ptr, gpc_buffer->data.data()+gpc_buffer->pos, element_size * available_count);
    gpc_buffer->pos += element_size * available_count;
    return available_count;
}

int ContractHelper::common_fread_int(GpcBuffer* fp, int* dst_int)
{
    int ret;
    unsigned char uc4, uc3, uc2, uc1;

    ret = (int)gpcread(&uc4, sizeof(unsigned char), 1, fp);
    if (ret != 1)
        return ret;
    ret = (int)gpcread(&uc3, sizeof(unsigned char), 1, fp);
    if (ret != 1)
        return ret;
    ret = (int)gpcread(&uc2, sizeof(unsigned char), 1, fp);
    if (ret != 1)
        return ret;
    ret = (int)gpcread(&uc1, sizeof(unsigned char), 1, fp);
    if (ret != 1)
        return ret;

    *dst_int = (uc4 << 24) + (uc3 << 16) + (uc2 << 8) + uc1;

    return 1;
}

int ContractHelper::common_fread_octets(GpcBuffer* fp, void* dst_stream, int len)
{
    return (int)gpcread(dst_stream, len, 1, fp);
}


#define PRINTABLE_CHAR(chr) \
if (chr >= 0 && chr <= 9)  \
    chr = chr + '0'; \
else \
    chr = chr + 'a' - 10;

std::string ContractHelper::to_printable_hex(unsigned char chr)
{
    unsigned char high = chr >> 4;
    unsigned char low = chr & 0x0F;
    char tmp[16];

    PRINTABLE_CHAR(high);
    PRINTABLE_CHAR(low);

    snprintf(tmp, sizeof(tmp), "%c%c", high, low);
    return std::string(tmp);
}

#define INIT_API_FROM_FILE(dst_set, except_1, except_2, except_3)\
{\
read_count = common_fread_int(f, &api_count); \
if (read_count != 1)\
{\
throw uvm::core::UvmException(except_1); \
}\
for (int i = 0; i < api_count; i++)\
{\
int api_len = 0; \
read_count = common_fread_int(f, &api_len); \
if (read_count != 1)\
{\
throw uvm::core::UvmException(except_2); \
}\
api_buf = (char*)malloc(api_len + 1); \
if (api_buf == NULL) \
{ \
FC_ASSERT(api_buf == NULL, "malloc fail!"); \
}\
read_count = common_fread_octets(f, api_buf, api_len); \
if (read_count != 1)\
{\
free(api_buf); \
throw uvm::core::UvmException(except_3); \
}\
api_buf[api_len] = '\0'; \
dst_set.insert(std::string(api_buf)); \
free(api_buf); \
}\
}

#define INIT_STORAGE_FROM_FILE(dst_map, except_1, except_2, except_3, except_4)\
{\
read_count = common_fread_int(f, &storage_count); \
if (read_count != 1)\
{\
throw uvm::core::UvmException(except_1); \
}\
for (int i = 0; i < storage_count; i++)\
{\
int storage_name_len = 0; \
read_count = common_fread_int(f, &storage_name_len); \
if (read_count != 1)\
{\
throw uvm::core::UvmException(except_2); \
}\
storage_buf = (char*)malloc(storage_name_len + 1); \
if (storage_buf == NULL) \
{ \
FC_ASSERT(storage_buf == NULL, "malloc fail!"); \
}\
read_count = common_fread_octets(f, storage_buf, storage_name_len); \
if (read_count != 1)\
{\
free(storage_buf); \
throw uvm::core::UvmException(except_3); \
}\
storage_buf[storage_name_len] = '\0'; \
read_count = common_fread_int(f, (int*)&storage_type); \
if (read_count != 1)\
{\
free(storage_buf); \
throw uvm::core::UvmException(except_4); \
}\
dst_map.insert(std::make_pair(std::string(storage_buf), storage_type)); \
free(storage_buf); \
}\
}

uvm::blockchain::Code ContractHelper::load_contract_from_gpc_data(const std::vector<unsigned char>& gpc_data)
{
    GpcBuffer gpc_buffer;
    gpc_buffer.data = gpc_data;
    gpc_buffer.pos = 0;
    auto f = &gpc_buffer;
    uvm::blockchain::Code code;

    unsigned int digest[5];
    int read_count = 0;
    for (int i = 0; i < 5; ++i)
    {
        read_count = common_fread_int(f, (int*)&digest[i]);
        if (read_count != 1)
        {
            throw uvm::core::UvmException("Read verify code fail!");
        }
    }

    int len = 0;
    read_count = common_fread_int(f, &len);
    if (read_count != 1 || len < 0 || (len >= (f->size() - f->pos)))
    {
        throw uvm::core::UvmException("Read bytescode len fail!");
    }

    code.code.resize(len);
    read_count = common_fread_octets(f, code.code.data(), len);
    if (read_count != 1)
    {
        throw uvm::core::UvmException("Read bytescode fail!");
    }

    boost::uuids::detail::sha1 sha;
    unsigned int check_digest[5];
    sha.process_bytes(code.code.data(), code.code.size());
    sha.get_digest(check_digest);
    if (memcmp((void*)digest, (void*)check_digest, sizeof(unsigned int) * 5))
    {
        throw uvm::core::UvmException("Verify bytescode SHA1 fail!");
    }

    for (int i = 0; i < 5; ++i)
    {
        unsigned char chr1 = (check_digest[i] & 0xFF000000) >> 24;
        unsigned char chr2 = (check_digest[i] & 0x00FF0000) >> 16;
        unsigned char chr3 = (check_digest[i] & 0x0000FF00) >> 8;
        unsigned char chr4 = (check_digest[i] & 0x000000FF);

        code.code_hash = code.code_hash + to_printable_hex(chr1) + to_printable_hex(chr2) +
                         to_printable_hex(chr3) + to_printable_hex(chr4);
    }

    int api_count = 0;
    char* api_buf = nullptr;

    INIT_API_FROM_FILE(code.abi, "read_api_count_fail", "read_api_len_fail", "read_api_fail");
    INIT_API_FROM_FILE(code.offline_abi, "read_offline_api_count_fail", "read_offline_api_len_fail", "read_offline_api_fail");
    INIT_API_FROM_FILE(code.events, "read_events_count_fail", "read_events_len_fail", "read_events_fail");

    int storage_count = 0;
    char* storage_buf = nullptr;
    uvm::blockchain::StorageValueTypes storage_type;

    INIT_STORAGE_FROM_FILE(code.storage_properties, "read_storage_count_fail", "read_storage_name_len_fail", "read_storage_name_fail", "read_storage_type_fail");

    return code;
}

struct ContractCreateDigestInfo
{
    std::string caller_address;
    std::string tx_hash;
    size_t contract_op_vout_index;
};

FC_REFLECT(::ContractCreateDigestInfo, (caller_address)(tx_hash)(contract_op_vout_index));

template <typename T> std::vector<char> encoder_result_to_vector(const T& item)
{
	std::vector<char> result;
	result.resize(item.data_size());
	memcpy(result.data(), item.data(), result.size());
	return result;
}

std::string ContractHelper::generate_contract_address(const std::string& caller_address, const CTransaction& txBitcoin, size_t contract_op_vout_index)
{
	// contract address = CON + base58(ripemd160(sha256(info)) + prefix_4_bytes(sha256(ripemd160(sha256(info))))
    fc::sha256::encoder enc;
    ContractCreateDigestInfo info;
    info.caller_address = caller_address;
    info.tx_hash = txBitcoin.GetHash().GetHex();
    info.contract_op_vout_index = contract_op_vout_index;
    fc::raw::pack(enc, info);
	const auto& info2_result = enc.result(); //  info160 = sha256(info)
	const auto& info2 = encoder_result_to_vector(info2_result);
    fc::ripemd160::encoder info2_encoder;
    fc::raw::pack(info2_encoder, info2);
    const auto& a_result = info2_encoder.result(); // a = ripemd160(sha256(info))
	const std::vector<char> a = encoder_result_to_vector(a_result);
	fc::sha256::encoder enc_of_a;
	fc::raw::pack(enc_of_a, a);
	const auto& b = enc_of_a.result(); // b = sha256(a)
	std::vector<char> first_4_bytes_of_b;
	first_4_bytes_of_b.resize(4);
	memcpy(first_4_bytes_of_b.data(), b.data(), 4);
	std::vector<char> c;
	c.resize(a.size() + first_4_bytes_of_b.size());
	memcpy(c.data(), a.data(), a.size());
	memcpy(c.data() + a.size(), first_4_bytes_of_b.data(), first_4_bytes_of_b.size());
	std::string addr = std::string("CON") + fc::to_base58(c.data(), c.size());
    return addr;
}

bool ContractHelper::is_valid_contract_address_format(const std::string& address)
{
    if(address.empty() || address.size() > CONTRACT_ID_MAX_LENGTH)
        return false;
	std::vector<char> c;
	c.resize(100);
	size_t decoded_size = 0;
	std::string prefix("CON");
	if (address.length() < prefix.length())
		return false;
	try {
		decoded_size = fc::from_base58(address.substr(prefix.length()), c.data(), c.size());
		if (decoded_size > c.size() || decoded_size <= 4 + prefix.length())
			return false;
		c.resize(decoded_size);
	}
	catch (const fc::parse_error_exception& e)
	{
		return false;
	}
	if (c.size() <= 4)
		return false;
	std::vector<char> a;
	a.resize(c.size() - 4);
	memcpy(a.data(), c.data(), a.size());
	std::vector<char> first_4_bytes_of_b;
	first_4_bytes_of_b.resize(4);
	memcpy(first_4_bytes_of_b.data(), c.data() + a.size(), c.size() - a.size());
	fc::sha256::encoder enc_of_a;
	fc::raw::pack(enc_of_a, a);
	auto b = enc_of_a.result();
	if (b.data_size() < 4)
		return false;
	std::vector<char> first_4_bytes_of_b_calculated;
	first_4_bytes_of_b_calculated.resize(4);
	memcpy(first_4_bytes_of_b_calculated.data(), b.data(), 4);
	return first_4_bytes_of_b_calculated == first_4_bytes_of_b;
}

bool ContractHelper::is_valid_contract_name_format(const std::string& name)
{
    if(name.size() < 2 || name.size() > 30)
        return false;
    // first char must be ascii character or '_'
    if(!(std::isalpha(name[0]) || name[0] == '_'))
        return false;
    // other position chars must be ascii character or '_' or digit
    for(size_t i=1;i<name.size();i++) {
        if(!(std::isalpha(name[i]) || name[i] == '_' || std::isdigit(name[i])))
            return false;
    }
    return true;
}

bool ContractHelper::is_valid_contract_desc_format(const std::string& desc)
{
    if(desc.size() > 100)
        return false;
    for(size_t i=0;i<desc.size();i++) {
        if(!(std::isalpha(desc[i]) || desc[i] == '_' || std::isdigit(desc[i]) || desc[i]==' ' || desc[i] == '\n' || desc[i] == '\r'))
            return false;
    }
    return true;
}

bool ContractHelper::is_valid_deposit_memo_format(const std::string& memo)
{
    if(memo.size() > 100)
        return false;
    return true;
}

std::string ContractHelper::storage_to_json_string(const StorageValue &storage_value)
{
    return jsondiff::json_dumps(storage_value);
}
ContractDataValue ContractHelper::vch_to_contract_data(const valtype &vch_value)
{
    const auto &vch_str = ValtypeUtils::vch_to_string(vch_value);
    return jsondiff::json_loads(vch_str);
}
valtype ContractHelper::contract_data_to_vch(const ContractDataValue &value)
{
    const auto& str = jsondiff::json_dumps(value);
    std::vector<unsigned char> data(str.size() + 1);
    memcpy(data.data(), str.c_str(), str.size());
    data[str.size()] = '\0';
    return data;
}