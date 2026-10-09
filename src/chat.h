#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct ChatCommand
{
	std::string trigger;
	std::string condition;
	std::string response;
};

struct ChatState
{
	std::string text;             // incoming chat text, lowercased and trimmed
	bool ourTurn = false;
	bool rated = false;
	bool scored = false;
	int cp = 0;                   // evaluation from our side, centipawns
	int mate = 0;                 // >0 we mate, <0 we are mated
	int depth = 0;
	int ply = 0;                  // half moves already played
	int ourClockSeconds = 0;
	std::vector<std::pair<std::string, std::string>> values;

	void set(std::string name, std::string value);
	std::string get(const std::string &name) const;
};

namespace chat
{
	const std::vector<std::string> &valueNames();

	bool parseCommand(std::string_view spec, ChatCommand &command, std::string &error);

	bool triggerMatches(const ChatCommand &command, std::string_view text);

	bool test(const std::string &condition, const ChatState &state, std::string &error);

	std::vector<std::string> alternatives(std::string_view response);

	std::string render(std::string_view response, const ChatState &state,
	                   std::vector<std::string> *missing = nullptr);
}
