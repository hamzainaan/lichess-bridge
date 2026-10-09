#include "util.h"

#include "platform.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <mutex>
#include <thread>

namespace
{
	std::mutex logMutex;
	std::vector<std::string> statusSlots(2);
	int statusHeight = 0;
	const bool ttyOutput = plat::stderrIsTty();
	const bool coloredOutput = ttyOutput && std::getenv("NO_COLOR") == nullptr;

	std::string wrap(const char *code, std::string_view text)
	{
		if (!coloredOutput)
			return std::string(text);
		return std::string("\033[") + code + "m" + std::string(text) + "\033[0m";
	}

	std::string blockText()
	{
		std::string out;
		statusHeight = 0;
		for (const std::string &slot : statusSlots)
		{
			if (slot.empty())
				continue;
			out += slot;
			out += '\n';
			++statusHeight;
		}
		return out;
	}

	void eraseBlock()
	{
		if (statusHeight > 0)
			std::fprintf(stderr, "\033[%dA", statusHeight);
		std::fprintf(stderr, "\r\033[J");
		statusHeight = 0;
	}

	void drawBlock()
	{
		const std::string text = blockText();
		if (!text.empty())
			std::fwrite(text.data(), 1, text.size(), stderr);
		std::fflush(stderr);
	}

	void write(const char *level, const char *color, std::string_view message)
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
		std::tm parts{};
		plat::localTime(seconds, parts);

		char stamp[16];
		std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d", parts.tm_hour, parts.tm_min, parts.tm_sec);

		std::lock_guard<std::mutex> lock(logMutex);
		if (ttyOutput)
			eraseBlock();
		std::fprintf(stderr, "%s  %s  %.*s\n", wrap("2", stamp).c_str(), wrap(color, level).c_str(),
		             static_cast<int>(message.size()), message.data());
		if (ttyOutput)
			drawBlock();
		std::fflush(stderr);
	}
}

namespace util
{
	bool colorsEnabled() { return coloredOutput; }
	std::string dim(std::string_view text) { return wrap("2", text); }
	std::string bold(std::string_view text) { return wrap("1", text); }
	std::string green(std::string_view text) { return wrap("32", text); }
	std::string red(std::string_view text) { return wrap("31", text); }
	std::string yellow(std::string_view text) { return wrap("33", text); }
	std::string cyan(std::string_view text) { return wrap("36", text); }

	void statusSet(int slot, std::string text)
	{
		if (!ttyOutput || slot < 0 || slot >= static_cast<int>(statusSlots.size()))
			return;
		std::lock_guard<std::mutex> lock(logMutex);
		if (statusSlots[slot] == text)
			return;
		statusSlots[slot] = std::move(text);
		eraseBlock();
		drawBlock();
	}

	void statusClear()
	{
		if (!ttyOutput)
			return;
		std::lock_guard<std::mutex> lock(logMutex);
		for (std::string &slot : statusSlots)
			slot.clear();
		eraseBlock();
		std::fflush(stderr);
	}

	std::string trim(std::string_view text)
	{
		size_t begin = 0;
		size_t end = text.size();
		while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
			++begin;
		while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
			--end;
		return std::string(text.substr(begin, end - begin));
	}

	std::vector<std::string> split(std::string_view text, char separator)
	{
		std::vector<std::string> parts;
		size_t start = 0;
		while (start <= text.size())
		{
			const size_t end = text.find(separator, start);
			std::string part = trim(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
			if (!part.empty())
				parts.push_back(std::move(part));
			if (end == std::string_view::npos)
				break;
			start = end + 1;
		}
		return parts;
	}

	std::string lower(std::string_view text)
	{
		std::string out(text);
		for (char &c : out)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return out;
	}

	bool iequals(std::string_view a, std::string_view b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i)
			if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
				return false;
		return true;
	}

	std::string percentEncode(std::string_view text)
	{
		static constexpr char HEX[] = "0123456789ABCDEF";
		std::string out;
		out.reserve(text.size());
		for (const char c : text)
		{
			const unsigned char byte = static_cast<unsigned char>(c);
			if (std::isalnum(byte) || c == '-' || c == '_' || c == '.' || c == '~')
				out.push_back(c);
			else
			{
				out.push_back('%');
				out.push_back(HEX[byte >> 4]);
				out.push_back(HEX[byte & 15]);
			}
		}
		return out;
	}

	std::string formEncode(const std::vector<std::pair<std::string, std::string>> &fields)
	{
		std::string body;
		for (const auto &[name, value] : fields)
		{
			if (!body.empty())
				body += '&';
			body += percentEncode(name);
			body += '=';
			body += percentEncode(value);
		}
		return body;
	}

	bool readFile(const std::string &path, std::string &out)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream)
			return false;
		out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
		return stream.good() || stream.eof();
	}

	int64_t nowMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	void sleepMs(int64_t milliseconds)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
	}

	void info(std::string_view message) { write("INFO", "1;36", message); }
	void warn(std::string_view message) { write("WARN", "1;33", message); }
	void fail(std::string_view message) { write("FAIL", "1;31", message); }
}
