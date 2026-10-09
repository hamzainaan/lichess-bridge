#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace json
{
	struct Value
	{
		enum class Kind : uint8_t
		{
			Null,
			Bool,
			Number,
			String,
			Array,
			Object
		};

		Kind kind = Kind::Null;
		bool boolean = false;
		double number = 0.0;
		std::string text;
		std::vector<Value> items;
		std::vector<std::pair<std::string, Value>> fields;

		const Value *find(std::string_view key) const;
		const Value *at(size_t index) const;
		std::string_view string(std::string_view fallback = {}) const;
		int64_t integer(int64_t fallback = 0) const;
		bool flag(bool fallback = false) const;
	};

	bool parse(std::string_view text, Value &out);

	inline const Value *find(const Value &value, std::string_view key)
	{
		return value.find(key);
	}

	inline std::string_view string(const Value &value, std::string_view key, std::string_view fallback = {})
	{
		const Value *field = value.find(key);
		return field ? field->string(fallback) : fallback;
	}

	inline int64_t integer(const Value &value, std::string_view key, int64_t fallback = 0)
	{
		const Value *field = value.find(key);
		return field ? field->integer(fallback) : fallback;
	}

	inline bool flag(const Value &value, std::string_view key, bool fallback = false)
	{
		const Value *field = value.find(key);
		return field ? field->flag(fallback) : fallback;
	}
}
