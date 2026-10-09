#include "chat.h"

#include "util.h"

#include <cctype>
#include <cstdlib>

namespace
{
	std::vector<std::string> splitFields(std::string_view spec, size_t maxFields)
	{
		std::vector<std::string> parts;
		size_t start = 0;
		while (parts.size() + 1 < maxFields)
		{
			const size_t colon = spec.find(':', start);
			if (colon == std::string_view::npos)
				break;
			parts.push_back(util::trim(spec.substr(start, colon - start)));
			start = colon + 1;
		}
		parts.push_back(util::trim(spec.substr(start)));
		return parts;
	}

	bool isValueName(const std::string &name)
	{
		for (const std::string &known : chat::valueNames())
			if (known == name)
				return true;
		return false;
	}

	class ConditionParser
	{
	public:
		ConditionParser(const std::string &input, const ChatState &state) : input(input), state(state) {}

		bool run(bool &result)
		{
			if (!parseOr(result))
				return false;
			skipSpace();
			if (position != input.size())
			{
				error = "unexpected '" + input.substr(position) + "' in condition";
				return false;
			}
			return true;
		}

		std::string error;

	private:
		void skipSpace()
		{
			while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position])))
				++position;
		}

		bool peek(std::string_view token)
		{
			skipSpace();
			return input.compare(position, token.size(), token) == 0;
		}

		bool consume(std::string_view token)
		{
			skipSpace();
			if (input.compare(position, token.size(), token) != 0)
				return false;
			position += token.size();
			return true;
		}

		bool parseOr(bool &out)
		{
			if (!parseAnd(out))
				return false;
			while (peek("||"))
			{
				consume("||");
				bool rhs = false;
				if (!parseAnd(rhs))
					return false;
				out = out || rhs;
			}
			return true;
		}

		bool parseAnd(bool &out)
		{
			if (!parseUnary(out))
				return false;
			while (peek("&&"))
			{
				consume("&&");
				bool rhs = false;
				if (!parseUnary(rhs))
					return false;
				out = out && rhs;
			}
			return true;
		}

		bool parseUnary(bool &out)
		{
			skipSpace();
			if (peek("("))
			{
				consume("(");
				if (!parseOr(out))
					return false;
				if (!consume(")"))
					return error = "missing ')' in condition", false;
				return true;
			}
			if (peek("!") && !peek("!="))
			{
				consume("!");
				bool inner = false;
				if (!parseUnary(inner))
					return false;
				out = !inner;
				return true;
			}
			return parseComparison(out);
		}

		bool parseComparison(bool &out)
		{
			skipSpace();
			const size_t start = position;
			while (position < input.size() && (std::isalpha(static_cast<unsigned char>(input[position])) || input[position] == '_'))
				++position;
			if (position == start)
				return error = "expected a condition name", false;
			const std::string name = input.substr(start, position - start);

			static constexpr std::string_view operators[] = { "!=", "==", ">=", "<=", ">", "<" };
			std::string op;
			skipSpace();
			for (std::string_view candidate : operators)
			{
				if (input.compare(position, candidate.size(), candidate) == 0)
				{
					op = std::string(candidate);
					position += candidate.size();
					break;
				}
			}

			if (op.empty())
			{
				if (name == "always") out = true;
				else if (name == "our_turn") out = state.ourTurn;
				else if (name == "opponent_turn") out = !state.ourTurn;
				else if (name == "rated") out = state.rated;
				else if (name == "casual") out = !state.rated;
				else if (name == "scored") out = state.scored;
				else if (name == "has_mate") out = state.mate > 0;
				else if (name == "getting_mated") out = state.mate < 0;
				else
					return error = "unknown condition '" + name + "'", false;
				return true;
			}

			skipSpace();
			const size_t numberStart = position;
			if (position < input.size() && (input[position] == '-' || input[position] == '+'))
				++position;
			const size_t digitsStart = position;
			while (position < input.size() && std::isdigit(static_cast<unsigned char>(input[position])))
				++position;
			if (position == digitsStart)
				return error = "expected a number after '" + name + " " + op + "'", false;
			const long long number = std::strtoll(input.substr(numberStart, position - numberStart).c_str(), nullptr, 10);

			long long value = 0;
			if (name == "eval" || name == "cp")
			{
				if (!state.scored)
				{
					out = false;
					return true;
				}
				value = state.cp;
			}
			else if (name == "mate") value = state.mate;
			else if (name == "depth") value = state.depth;
			else if (name == "ply") value = state.ply;
			else if (name == "clock") value = state.ourClockSeconds;
			else
				return error = "unknown condition '" + name + "'", false;

			if (op == ">=") out = value >= number;
			else if (op == "<=") out = value <= number;
			else if (op == ">") out = value > number;
			else if (op == "<") out = value < number;
			else if (op == "==") out = value == number;
			else out = value != number;
			return true;
		}

		const std::string &input;
		size_t position = 0;
		const ChatState &state;
	};
}

void ChatState::set(std::string name, std::string value)
{
	for (auto &entry : values)
		if (entry.first == name)
		{
			entry.second = std::move(value);
			return;
		}
	values.emplace_back(std::move(name), std::move(value));
}

std::string ChatState::get(const std::string &name) const
{
	for (const auto &entry : values)
		if (entry.first == name)
			return entry.second;
	return {};
}

const std::vector<std::string> &chat::valueNames()
{
	static const std::vector<std::string> names = {
		"eval", "cp", "mate", "depth", "seldepth", "nodes", "nps", "hash", "time",
		"clock", "best", "turn", "ply", "last_move", "us", "opponent", "our_rating",
		"opponent_rating", "rated", "result", "report",
	};
	return names;
}

bool chat::parseCommand(std::string_view spec, ChatCommand &command, std::string &error)
{
	const std::vector<std::string> fields = splitFields(spec, 3);
	if (fields.size() < 2 || fields.front().empty() || fields.back().empty())
		return error = "chat_command needs '<trigger> : <condition> : <response>': " + std::string(spec), false;
	command.trigger = fields[0];
	command.condition = fields.size() == 3 ? fields[1] : std::string();
	command.response = fields.back();
	return true;
}

bool chat::triggerMatches(const ChatCommand &command, std::string_view text)
{
	if (command.trigger == "*")
		return true;
	return util::lower(util::trim(text)) == util::lower(command.trigger);
}

bool chat::test(const std::string &condition, const ChatState &state, std::string &error)
{
	if (util::trim(condition).empty())
		return true;
	ConditionParser parser(condition, state);
	bool result = false;
	if (!parser.run(result))
		return error = parser.error, false;
	return result;
}

std::vector<std::string> chat::alternatives(std::string_view response)
{
	std::vector<std::string> parts;
	size_t start = 0;
	while (start <= response.size())
	{
		const size_t bar = response.find('|', start);
		const std::string part = util::trim(response.substr(start, bar == std::string_view::npos ? std::string_view::npos : bar - start));
		if (!part.empty())
			parts.push_back(part);
		if (bar == std::string_view::npos)
			break;
		start = bar + 1;
	}
	return parts;
}

std::string chat::render(std::string_view response, const ChatState &state, std::vector<std::string> *missing)
{
	std::string out;
	out.reserve(response.size());
	for (size_t index = 0; index < response.size(); ++index)
	{
		const char c = response[index];
		if (c == '{' && index + 1 < response.size() && response[index + 1] == '{')
		{
			out += '{';
			++index;
			continue;
		}
		if (c == '}' && index + 1 < response.size() && response[index + 1] == '}')
		{
			out += '}';
			++index;
			continue;
		}
		if (c != '{')
		{
			out += c;
			continue;
		}
		const size_t end = response.find('}', index + 1);
		if (end == std::string_view::npos)
		{
			out += c;
			continue;
		}
		const std::string name(response.substr(index + 1, end - index - 1));
		if (!isValueName(name))
		{
			if (missing)
				missing->push_back(name);
			out += response.substr(index, end - index + 1);
		}
		else
			out += state.get(name);
		index = end;
	}
	return out;
}
