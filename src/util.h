#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace util
{
	std::string trim(std::string_view text);
	std::vector<std::string> split(std::string_view text, char separator);
	std::string lower(std::string_view text);
	bool iequals(std::string_view a, std::string_view b);
	std::string percentEncode(std::string_view text);
	std::string formEncode(const std::vector<std::pair<std::string, std::string>> &fields);
	bool readFile(const std::string &path, std::string &out);
	int64_t nowMs();
	void sleepMs(int64_t milliseconds);

	void info(std::string_view message);
	void warn(std::string_view message);
	void fail(std::string_view message);

	bool colorsEnabled();
	std::string dim(std::string_view text);
	std::string bold(std::string_view text);
	std::string green(std::string_view text);
	std::string red(std::string_view text);
	std::string yellow(std::string_view text);
	std::string cyan(std::string_view text);

	void statusSet(int slot, std::string text);
	void statusClear();
}
