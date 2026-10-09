#include "json.h"

#include <charconv>

namespace
{
	void appendUtf8(std::string &out, uint32_t codePoint)
	{
		if (codePoint < 0x80)
			out.push_back(static_cast<char>(codePoint));
		else if (codePoint < 0x800)
		{
			out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
			out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		}
		else if (codePoint < 0x10000)
		{
			out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
			out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		}
		else
		{
			out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
			out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		}
	}

	class Parser
	{
	public:
		explicit Parser(std::string_view text) : text(text) {}

		bool parse(json::Value &out)
		{
			if (!parseValue(out, 0))
				return false;
			skipSpace();
			return index >= text.size();
		}

	private:
		json::Value::Kind kindOf(char c) const
		{
			switch (c)
			{
			case '{':
				return json::Value::Kind::Object;
			case '[':
				return json::Value::Kind::Array;
			case '"':
				return json::Value::Kind::String;
			case 't':
			case 'f':
				return json::Value::Kind::Bool;
			case 'n':
				return json::Value::Kind::Null;
			default:
				return json::Value::Kind::Number;
			}
		}

		void skipSpace()
		{
			while (index < text.size() && (text[index] == ' ' || text[index] == '\t' || text[index] == '\r' || text[index] == '\n'))
				++index;
		}

		bool parseValue(json::Value &out, int depth)
		{
			if (depth > 64)
				return false;
			skipSpace();
			if (index >= text.size())
				return false;
			switch (kindOf(text[index]))
			{
			case json::Value::Kind::Object:
				return parseObject(out, depth);
			case json::Value::Kind::Array:
				return parseArray(out, depth);
			case json::Value::Kind::String:
				out.kind = json::Value::Kind::String;
				return parseString(out.text);
			case json::Value::Kind::Bool:
				out.kind = json::Value::Kind::Bool;
				return parseBool(out.boolean);
			case json::Value::Kind::Null:
				out.kind = json::Value::Kind::Null;
				return parseNull();
			default:
				out.kind = json::Value::Kind::Number;
				return parseNumber(out.number);
			}
		}

		bool parseObject(json::Value &out, int depth)
		{
			out.kind = json::Value::Kind::Object;
			++index;
			skipSpace();
			if (index < text.size() && text[index] == '}')
			{
				++index;
				return true;
			}
			while (true)
			{
				skipSpace();
				std::string key;
				if (!parseString(key))
					return false;
				skipSpace();
				if (index >= text.size() || text[index] != ':')
					return false;
				++index;
				json::Value value;
				if (!parseValue(value, depth + 1))
					return false;
				out.fields.emplace_back(std::move(key), std::move(value));
				skipSpace();
				if (index >= text.size())
					return false;
				if (text[index] == ',')
				{
					++index;
					continue;
				}
				if (text[index] == '}')
				{
					++index;
					return true;
				}
				return false;
			}
		}

		bool parseArray(json::Value &out, int depth)
		{
			out.kind = json::Value::Kind::Array;
			++index;
			skipSpace();
			if (index < text.size() && text[index] == ']')
			{
				++index;
				return true;
			}
			while (true)
			{
				json::Value value;
				if (!parseValue(value, depth + 1))
					return false;
				out.items.push_back(std::move(value));
				skipSpace();
				if (index >= text.size())
					return false;
				if (text[index] == ',')
				{
					++index;
					continue;
				}
				if (text[index] == ']')
				{
					++index;
					return true;
				}
				return false;
			}
		}

		bool parseBool(bool &out)
		{
			if (text.substr(index, 4) == "true")
			{
				index += 4;
				out = true;
				return true;
			}
			if (text.substr(index, 5) == "false")
			{
				index += 5;
				out = false;
				return true;
			}
			return false;
		}

		bool parseNull()
		{
			if (text.substr(index, 4) != "null")
				return false;
			index += 4;
			return true;
		}

		bool parseNumber(double &out)
		{
			const size_t begin = index;
			while (index < text.size())
			{
				const char c = text[index];
				if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')
					++index;
				else
					break;
			}
			if (index == begin)
				return false;
			const char *first = text.data() + begin;
			const char *last = text.data() + index;
			const auto result = std::from_chars(first, last, out);
			return result.ec == std::errc() && result.ptr == last;
		}

		bool parseHex4(uint32_t &out)
		{
			if (index + 4 > text.size())
				return false;
			out = 0;
			for (int i = 0; i < 4; ++i)
			{
				const char c = text[index++];
				out <<= 4;
				if (c >= '0' && c <= '9')
					out |= static_cast<uint32_t>(c - '0');
				else if (c >= 'a' && c <= 'f')
					out |= static_cast<uint32_t>(c - 'a' + 10);
				else if (c >= 'A' && c <= 'F')
					out |= static_cast<uint32_t>(c - 'A' + 10);
				else
					return false;
			}
			return true;
		}

		bool parseString(std::string &out)
		{
			if (index >= text.size() || text[index] != '"')
				return false;
			++index;
			out.clear();
			while (index < text.size())
			{
				const char c = text[index++];
				if (c == '"')
					return true;
				if (c != '\\')
				{
					out.push_back(c);
					continue;
				}
				if (index >= text.size())
					return false;
				const char escape = text[index++];
				switch (escape)
				{
				case '"':
					out.push_back('"');
					break;
				case '\\':
					out.push_back('\\');
					break;
				case '/':
					out.push_back('/');
					break;
				case 'b':
					out.push_back('\b');
					break;
				case 'f':
					out.push_back('\f');
					break;
				case 'n':
					out.push_back('\n');
					break;
				case 'r':
					out.push_back('\r');
					break;
				case 't':
					out.push_back('\t');
					break;
				case 'u':
				{
					uint32_t codePoint = 0;
					if (!parseHex4(codePoint))
						return false;
					if (codePoint >= 0xD800 && codePoint <= 0xDBFF && index + 6 <= text.size() && text[index] == '\\' && text[index + 1] == 'u')
					{
						index += 2;
						uint32_t low = 0;
						if (!parseHex4(low))
							return false;
						if (low >= 0xDC00 && low <= 0xDFFF)
							codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
						else
							codePoint = 0xFFFD;
					}
					appendUtf8(out, codePoint);
					break;
				}
				default:
					return false;
				}
			}
			return false;
		}

		std::string_view text;
		size_t index = 0;
	};
}

namespace json
{
	const Value *Value::find(std::string_view key) const
	{
		if (kind != Kind::Object)
			return nullptr;
		for (const auto &[name, value] : fields)
			if (name == key)
				return &value;
		return nullptr;
	}

	const Value *Value::at(size_t index) const
	{
		if (kind != Kind::Array || index >= items.size())
			return nullptr;
		return &items[index];
	}

	std::string_view Value::string(std::string_view fallback) const
	{
		return kind == Kind::String ? std::string_view(text) : fallback;
	}

	int64_t Value::integer(int64_t fallback) const
	{
		return kind == Kind::Number ? static_cast<int64_t>(number) : fallback;
	}

	bool Value::flag(bool fallback) const
	{
		return kind == Kind::Bool ? boolean : fallback;
	}

	bool parse(std::string_view text, Value &out)
	{
		Parser parser(text);
		return parser.parse(out);
	}
}
