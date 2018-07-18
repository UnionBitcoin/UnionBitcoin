#include <jsondiff/jsondiff.h>
#include <jsondiff/exceptions.h>
#include <jsondiff/diff_result.h>
#include <jsondiff/helper.h>

#include <fjson/io/json.hpp>
#include <fjson/string.hpp>
#include <fjson/variant.hpp>
#include <fjson/variant_object.hpp>

namespace jsondiff
{
	JsonDiff::JsonDiff()
	{

	}

	JsonDiff::~JsonDiff()
	{

	}

	DiffResultP JsonDiff::diff_by_string(const std::string &old_json_str, const std::string &new_json_str)
	{
		return diff(json_loads(old_json_str), json_loads(new_json_str));
	}

	DiffResultP JsonDiff::diff(const JsonValue& old_json, const JsonValue& new_json)
	{
		auto old_json_type = guess_json_value_type(old_json);
		auto new_json_type = guess_json_value_type(new_json);

		if (is_scalar_json_value_type(old_json_type) || old_json_type != new_json_type)
		{
			// old值是基本类型 或old和new值的类型不一样
			// should return undefined for two identical values
			// should return { __old: <old value>, __new : <new value> } object for two different numbers

			if (old_json_type != new_json_type || json_dumps(old_json) != json_dumps(new_json))
			{
				fjson::mutable_variant_object result_json;
				result_json[JSONDIFF_KEY_OLD_VALUE] = old_json;
				result_json[JSONDIFF_KEY_NEW_VALUE] = new_json;
				return std::make_shared<DiffResult>(result_json);
			}
			else
			{
				// identical scalar values
				return DiffResult::make_undefined_diff_result();
			}
		}
		else if (old_json_type == JsonValueType::JVT_OBJECT)
		{
			// old和new都是object类型时
			// should return undefined for two objects with identical contents
			// should return undefined for two object hierarchies with identical contents
			// should return { <key>__deleted: <old value> } when the second object is missing a key
			// should return { <key>__added: <new value> } when the first object is missing a key
			// should return { <key>: { __old: <old value>, __new : <new value> } } for two objects with diffent scalar values for a key
			// should return { <key>: <diff> } with a recursive diff for two objects with diffent values for a key
			auto a_obj = old_json.as<fjson::mutable_variant_object>();
			auto b_obj = new_json.as<fjson::mutable_variant_object>();
			bool same = false;
			fjson::mutable_variant_object diff_json;
			for (auto i = a_obj.begin(); i != a_obj.end(); i++)
			{
				auto a_i_value = i->value();
				auto a_i_key = i->key();
				if (b_obj.find(a_i_key) == b_obj.end())
				{
					// 存在于old不存在于new
					diff_json[a_i_key + JSONDIFF_KEY_DELETED_POSTFIX] = a_i_value;
				}
				else
				{
					// old和new中都有这个key
					auto sub_diff_value = diff(a_i_value, b_obj[a_i_key]);
					if (sub_diff_value->is_undefined()) // same elements
						continue;
					// 修改
					diff_json[a_i_key] = sub_diff_value->value();
				}
			}
			for (auto j = b_obj.begin(); j != b_obj.end(); j++)
			{
				auto key = j->key();
				if (a_obj.find(key) == a_obj.end())
				{
					// 不存在于old但是存在于new
					diff_json[key + JSONDIFF_KEY_ADDED_POSTFIX] = j->value();
				}
			}
			if (diff_json.size() < 1)
				return DiffResult::make_undefined_diff_result();
			return std::make_shared<DiffResult>(diff_json);
		}
		else if (old_json_type == JsonValueType::JVT_ARRAY)
		{
			// old和new都是array类型时
			// with arrays of scalars
			//   should return undefined for two arrays with identical contents
			//   should return[..., ['-', remove_from_position_index, <removed item>], ...] for two arrays when the second array is missing a value
			//   should return[..., ['+', insert_position_index, <added item>], ...] for two arrays when the second one has an extra value
			//   should return[..., ['+', insert_position_index, <added item>]] for two arrays when the second one has an extra value at the end(edge case test)
			// with arrays of objects
			//   should return undefined for two arrays with identical contents
			//   should return[..., ['-', remove_from_position_index, <removed item>], ...] for two arrays when the second array is missing a value
			//   should return[..., ['+', insert_position_index, <added item>], ...] for two arrays when the second array has an extra value
			//   should return[..., ['~', position_index, <diff>], ...] for two arrays when an item has been modified(note: involves a crazy heuristic)

			auto a_array = old_json.as<fjson::variants>();
			auto b_array = new_json.as<fjson::variants>();

			// TODO: 当两个array的大部分元素相同时，但是可能前方插入部分元素，这时候应该尽量减少diff大小

			// 一个array有多项变化的时候， diff里的索引是用原始对象的index

			fjson::variants diff_json;
			for (size_t i = 0; i < a_array.size(); i++)
			{
				if (i >= b_array.size())
				{
					// 删除元素
					fjson::variants item_diff;
					item_diff.push_back("-");
					item_diff.push_back((int)i);
					item_diff.push_back(a_array[i]);
					diff_json.push_back(item_diff);
				}
				else
				{
					auto item_value_diff = diff(a_array[i], b_array[i]);
					if (item_value_diff->is_undefined()) // 没有发生改变
						continue;
					// 修改元素
					fjson::variants item_diff;
					item_diff.push_back("~");
					item_diff.push_back((int)i);
					item_diff.push_back(item_value_diff->value());
					diff_json.push_back(item_diff);
				}
			}
			for (size_t i = a_array.size(); i < b_array.size(); i++)
			{
				// 不存在于old但是存在于new中
				fjson::variants item_diff;
				item_diff.push_back("+");
				item_diff.push_back((int)i);
				item_diff.push_back(b_array[i]);
				diff_json.push_back(item_diff);
			}
			if (diff_json.size() < 1)
			{
				return DiffResult::make_undefined_diff_result();
			}
			return std::make_shared<DiffResult>(diff_json);
		}
		else
		{
			throw JsonDiffException(std::string("not supported json value type to diff ") + json_dumps(old_json));
		}
	}

	JsonValue JsonDiff::patch_by_string(const std::string& old_json_value, DiffResultP diff_info)
	{
		return patch(json_loads(old_json_value), diff_info);
	}

	JsonValue JsonDiff::patch(const JsonValue& old_json, const DiffResultP& diff_info)
	{
		auto old_json_type = guess_json_value_type(old_json);
		auto diff_json = diff_info->value();
		auto result = json_deep_clone(old_json);
		if (diff_info->is_undefined() || diff_json.is_null())
			return result;
		
		auto old_json_str = json_dumps(old_json);
		auto diff_json_str = json_dumps(diff_json);
		
		if (is_scalar_json_value_type(old_json_type) || is_scalar_value_diff_format(diff_json))
		{ // TODO: 这个判断要修改得简单准确一点，修改diffjson格式，区分{__old: ..., __new: ...}和普通object diff
			if (!diff_json.is_object())
				throw JsonDiffException("wrong format of diffjson of scalar json value");
			result = diff_json[JSONDIFF_KEY_NEW_VALUE];
		}
		else if (old_json_type == JsonValueType::JVT_OBJECT)
		{
			auto old_json_obj = old_json.as<fjson::mutable_variant_object>();
			auto diff_json_obj = diff_json.as<fjson::mutable_variant_object>();
			auto result_obj = result.as<fjson::mutable_variant_object>();
			for (auto i = diff_json_obj.begin(); i != diff_json_obj.end(); i++)
			{
				auto key = i->key();
				auto diff_item = i->value();
				// 如果key是 <key>__deleted 或者 <key>__added，则是删除或者添加，否则是修改现有key的值
				if (utils::string_ends_with(key, JSONDIFF_KEY_DELETED_POSTFIX) && key.size() > strlen(JSONDIFF_KEY_DELETED_POSTFIX))
				{
					auto old_key = utils::string_without_ext(key, JSONDIFF_KEY_DELETED_POSTFIX);
					if (old_json_obj.find(old_key) != old_json_obj.end())
					{
						// 是删除属性操作
						result_obj.erase(old_key);
						continue;
					}
				}
				else if (utils::string_ends_with(key, JSONDIFF_KEY_ADDED_POSTFIX) && key.size() > strlen(JSONDIFF_KEY_ADDED_POSTFIX))
				{
					auto old_key = utils::string_without_ext(key, JSONDIFF_KEY_ADDED_POSTFIX);
					// 是增加属性操作
					result_obj[old_key] = diff_item;
					continue;
				}
				// 可能是修改现有key的值
				if (old_json_obj.find(key) == old_json_obj.end())
					throw JsonDiffException("wrong format of diffjson of this old version json");
				auto sub_item_new = patch(old_json_obj[key], std::make_shared<DiffResult>(diff_item));
				result_obj[key] = sub_item_new;
			}
			return result_obj;
		}
		else if (old_json_type == JsonValueType::JVT_ARRAY)
		{
			auto old_json_array = old_json.as<fjson::variants>();
			auto diff_json_array = diff_json.as<fjson::variants>();
			auto result_array = result.as<fjson::variants>();
			for (size_t i = 0; i < diff_json_array.size(); i++)
			{
				if (!diff_json_array[i].is_array())
					throw JsonDiffException("diffjson format error for array diff");
				auto diff_item = diff_json_array[i].as<fjson::variants>();
				if (diff_item.size() != 3)
					throw JsonDiffException("diffjson format error for array diff");
				auto op_item = diff_item[0].as_string();
				auto pos = diff_item[1].as_uint64();
				auto inner_diff_json = diff_item[2];
				// FIXME； 一个array有多项变化的时候， diff里的索引是用原始对象的index，所以这里应该找出 pos => old_json中同值的pos
				if (op_item == std::string("+"))
				{
					// 添加元素
					result_array.insert(result_array.begin() + pos, inner_diff_json);
				}
				else if (op_item == std::string("-"))
				{
					// 删除元素
					result_array.erase(result_array.begin() + pos);
				}
				else if (op_item == std::string("~"))
				{
					// 修改元素
					auto sub_item_new = patch(old_json_array[i], std::make_shared<DiffResult>(inner_diff_json));
					result_array[pos] = sub_item_new;
				}
				else
				{
					throw JsonDiffException(std::string("not supported diff array op now: ") + op_item);
				}
			}
			return result_array;
		}
		else
		{
			throw JsonDiffException(std::string("not supported json value type to merge patch ") + json_dumps(old_json));
		}
		return result;
	}

	JsonValue JsonDiff::rollback_by_string(const std::string& new_json_value, DiffResultP diff_info)
	{
		return rollback(json_loads(new_json_value), diff_info);
	}

	JsonValue JsonDiff::rollback(const JsonValue& new_json, DiffResultP diff_info)
	{
		auto new_json_type = guess_json_value_type(new_json);
		auto diff_json = diff_info->value();
		auto result = json_deep_clone(new_json);
		if (diff_info->is_undefined() || diff_json.is_null())
			return result;

		auto new_json_str = json_dumps(new_json);
		auto diff_json_str = json_dumps(diff_json);

		if (is_scalar_json_value_type(new_json_type) || is_scalar_value_diff_format(diff_json))
		{ // TODO: 这个判断要修改得简单准确一点，修改diffjson格式，区分{__old: ..., __new: ...}和普通object diff
			if (!diff_json.is_object())
				throw JsonDiffException("wrong format of diffjson of scalar json value");
			result = diff_json[JSONDIFF_KEY_OLD_VALUE];
		}
		else if (new_json_type == JsonValueType::JVT_OBJECT)
		{
			auto new_json_obj = new_json.as<fjson::mutable_variant_object>();
			auto diff_json_obj = diff_json.as<fjson::mutable_variant_object>();
			auto result_obj = result.as<fjson::mutable_variant_object>();
			for (auto i = diff_json_obj.begin(); i != diff_json_obj.end(); i++)
			{
				auto key = i->key();
				auto diff_item = i->value();
				// 如果key是 <key>__deleted 或者 <key>__added，则是删除或者添加，否则是修改现有key的值
				if (utils::string_ends_with(key, JSONDIFF_KEY_ADDED_POSTFIX) && key.size() > strlen(JSONDIFF_KEY_ADDED_POSTFIX))
				{
					auto origin_key = utils::string_without_ext(key, JSONDIFF_KEY_ADDED_POSTFIX);
					if (new_json_obj.find(origin_key) != new_json_obj.end())
					{
						// 是增加属性操作，需要回滚
						result_obj.erase(origin_key);
						continue;
					}
				}
				else if (utils::string_ends_with(key, JSONDIFF_KEY_DELETED_POSTFIX) && key.size() > strlen(JSONDIFF_KEY_DELETED_POSTFIX))
				{
					auto origin_key = utils::string_without_ext(key, JSONDIFF_KEY_DELETED_POSTFIX);
					// 是删除属性操作，需要回滚
					result_obj[origin_key] = diff_item;
					continue;
				}
				// 可能是修改现有key的值
				if (new_json_obj.find(key) == new_json_obj.end())
					throw JsonDiffException("wrong format of diffjson of this old version json");
				auto sub_item_new = rollback(new_json_obj[key], std::make_shared<DiffResult>(diff_item));
				result_obj[key] = sub_item_new;
			}
			return result_obj;
		}
		else if (new_json_type == JsonValueType::JVT_ARRAY)
		{
			auto new_json_array = new_json.as<fjson::variants>();
			auto diff_json_array = diff_json.as<fjson::variants>();
			auto result_array = result.as<fjson::variants>();
			for (size_t i = 0; i < diff_json_array.size(); i++)
			{
				if (!diff_json_array[i].is_array())
					throw JsonDiffException("diffjson format error for array diff");
				auto diff_item = diff_json_array[i].as<fjson::variants>();
				if (diff_item.size() != 3)
					throw JsonDiffException("diffjson format error for array diff");
				auto op_item = diff_item[0].as_string();
				auto pos = diff_item[1].as_uint64(); // pos是old的pos， FIXME： 新旧对象的pos不一定一样
				auto inner_diff_json = diff_item[2];
				// FIXME； 一个array有多项变化的时候， diff里的索引是用原始对象的index，所以这里应该找出 pos => old_json中同值的pos
				if (op_item == std::string("-"))
				{
					// 删除元素，需要回滚
					result_array.insert(result_array.begin() + pos, inner_diff_json);
				}
				else if (op_item == std::string("+"))
				{
					// 添加元素，需要回滚
					result_array.erase(result_array.begin() + pos);
				}
				else if (op_item == std::string("~"))
				{
					// 修改元素
					auto sub_item_new = rollback(new_json_array[i], std::make_shared<DiffResult>(inner_diff_json));
					result_array[pos] = sub_item_new;
				}
				else
				{
					throw JsonDiffException(std::string("not supported diff array op now: ") + op_item);
				}
			}
			return result_array;
		}
		else
		{
			throw JsonDiffException(std::string("not supported json value type to rollback diff from ") + json_dumps(new_json));
		}
		return result;
	}
}
