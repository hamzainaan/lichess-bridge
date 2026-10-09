#include "engine.h"

#include "config.h"
#include "util.h"

#include <charconv>
#include <cstdlib>
#include <cstring>

namespace
{
	uint64_t toUnsigned(std::string_view text)
	{
		uint64_t value = 0;
		std::from_chars(text.data(), text.data() + text.size(), value);
		return value;
	}

	int64_t toSigned(std::string_view text)
	{
		int64_t value = 0;
		std::from_chars(text.data(), text.data() + text.size(), value);
		return value;
	}

	void parseInfo(std::string_view line, SearchResult &result)
	{
		size_t position = 5;
		const auto next = [&](std::string_view &out) {
			while (position < line.size() && line[position] == ' ')
				++position;
			const size_t stop = line.find(' ', position);
			out = line.substr(position, stop == std::string_view::npos ? std::string_view::npos : stop - position);
			position = stop == std::string_view::npos ? line.size() : stop + 1;
		};

		std::string_view token;
		std::string_view value;
		while (position < line.size())
		{
			next(token);
			if (token == "depth")
			{
				next(value);
				result.depth = static_cast<int>(toUnsigned(value));
			}
			else if (token == "seldepth")
			{
				next(value);
				result.seldepth = static_cast<int>(toUnsigned(value));
			}
			else if (token == "nodes")
			{
				next(value);
				result.nodes = toUnsigned(value);
			}
			else if (token == "nps")
			{
				next(value);
				result.nps = toUnsigned(value);
			}
			else if (token == "hashfull")
			{
				next(value);
				result.hashfull = static_cast<int>(toUnsigned(value));
			}
			else if (token == "tbhits")
			{
				next(value);
				result.tbhits = toUnsigned(value);
			}
			else if (token == "time")
			{
				next(value);
				result.timeMs = toSigned(value);
			}
			else if (token == "score")
			{
				std::string_view kind;
				next(kind);
				next(value);
				const int score = static_cast<int>(toSigned(value));
				if (kind == "cp")
				{
					result.scored = true;
					result.cp = score;
					result.mate = 0;
				}
				else if (kind == "mate")
				{
					result.scored = true;
					result.mate = score;
				}
			}
			else if (token == "pv")
			{
				std::string_view move;
				next(move);
				result.line = std::string(move);
				position = line.size();
			}
		}
	}
}

Engine::~Engine()
{
	stop();
}

bool Engine::start(const std::string &path, std::string &error)
{
	stop();

#ifdef _WIN32
	SECURITY_ATTRIBUTES attributes{};
	attributes.nLength = sizeof(attributes);
	attributes.bInheritHandle = TRUE;

	HANDLE childInput = nullptr;
	HANDLE parentWrite = nullptr;
	HANDLE parentRead = nullptr;
	HANDLE childOutput = nullptr;
	if (!CreatePipe(&childInput, &parentWrite, &attributes, 0)
	    || !SetHandleInformation(parentWrite, HANDLE_FLAG_INHERIT, 0))
	{
		error = "pipe failed";
		return false;
	}
	if (!CreatePipe(&parentRead, &childOutput, &attributes, 0)
	    || !SetHandleInformation(parentRead, HANDLE_FLAG_INHERIT, 0))
	{
		CloseHandle(childInput);
		CloseHandle(parentWrite);
		error = "pipe failed";
		return false;
	}

	STARTUPINFOA startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = childInput;
	startup.hStdOutput = childOutput;
	startup.hStdError = childOutput;

	const std::string commandLine = "\"" + path + "\"";
	std::vector<char> command(commandLine.begin(), commandLine.end());
	command.push_back('\0');

	PROCESS_INFORMATION process{};
	if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
	{
		CloseHandle(childInput);
		CloseHandle(parentWrite);
		CloseHandle(parentRead);
		CloseHandle(childOutput);
		error = "fork failed";
		return false;
	}
	CloseHandle(childInput);
	CloseHandle(childOutput);
	CloseHandle(process.hThread);

	pid = process.hProcess;
	toEngine = parentWrite;
	fromEngine = parentRead;
#else
	int input[2] = { -1, -1 };
	int output[2] = { -1, -1 };
	if (pipe(input) != 0 || pipe(output) != 0)
	{
		error = "pipe failed";
		return false;
	}

	pid = fork();
	if (pid < 0)
	{
		error = "fork failed";
		return false;
	}
	if (pid == 0)
	{
		dup2(input[0], STDIN_FILENO);
		dup2(output[1], STDOUT_FILENO);
		close(input[0]);
		close(input[1]);
		close(output[0]);
		close(output[1]);
		execl(path.c_str(), path.c_str(), static_cast<char *>(nullptr));
		_exit(127);
	}

	close(input[0]);
	close(output[1]);
	toEngine = input[1];
	fromEngine = output[0];
#endif

	std::string line;
	if (!writeLine("uci", error) || !waitFor("uciok", 10000, line, error))
	{
		stop();
		error = "engine handshake failed: " + error;
		return false;
	}
	return true;
}

void Engine::stop()
{
#ifdef _WIN32
	if (pid != plat::invalidProcess)
	{
		if (toEngine != plat::invalidHandle)
		{
			std::string ignored;
			writeLine("quit", ignored);
		}
		bool exited = false;
		for (int attempt = 0; attempt < 20; ++attempt)
		{
			if (WaitForSingleObject(static_cast<HANDLE>(pid), 50) == WAIT_OBJECT_0)
			{
				exited = true;
				break;
			}
		}
		if (!exited)
		{
			TerminateProcess(static_cast<HANDLE>(pid), 1);
			WaitForSingleObject(static_cast<HANDLE>(pid), 1000);
		}
		CloseHandle(static_cast<HANDLE>(pid));
		pid = plat::invalidProcess;
	}
	if (toEngine != plat::invalidHandle)
	{
		CloseHandle(static_cast<HANDLE>(toEngine));
		toEngine = plat::invalidHandle;
	}
	if (fromEngine != plat::invalidHandle)
	{
		CloseHandle(static_cast<HANDLE>(fromEngine));
		fromEngine = plat::invalidHandle;
	}
#else
	if (pid > 0)
	{
		if (toEngine >= 0)
		{
			std::string ignored;
			writeLine("quit", ignored);
		}
		for (int attempt = 0; attempt < 20; ++attempt)
		{
			if (waitpid(pid, nullptr, WNOHANG) == pid)
			{
				pid = -1;
				break;
			}
			util::sleepMs(50);
		}
		if (pid > 0)
		{
			kill(pid, SIGKILL);
			waitpid(pid, nullptr, 0);
			pid = -1;
		}
	}
	if (toEngine >= 0)
	{
		close(toEngine);
		toEngine = -1;
	}
	if (fromEngine >= 0)
	{
		close(fromEngine);
		fromEngine = -1;
	}
#endif
	buffer.clear();
}

bool Engine::setOption(const std::string &name, const std::string &value, std::string &error)
{
	std::string line = "setoption name " + name;
	if (!value.empty())
		line += " value " + value;
	if (!writeLine(line, error))
		return false;
	if (!writeLine("isready", error))
		return false;

	std::string ignored;
	return waitFor("readyok", 10000, ignored, error);
}

bool Engine::newGame(std::string &error)
{
	if (!writeLine("ucinewgame", error))
		return false;
	if (!writeLine("isready", error))
		return false;

	std::string ignored;
	return waitFor("readyok", 10000, ignored, error);
}

bool Engine::position(const std::string &startFen, const std::vector<std::string> &moves, std::string &error)
{
	std::string line = startFen.empty() || startFen == "startpos" ? "position startpos" : "position fen " + startFen;
	if (!moves.empty())
	{
		line += " moves";
		for (const std::string &move : moves)
		{
			line += ' ';
			line += move;
		}
	}
	return writeLine(line, error);
}

bool Engine::search(const std::string &parameters, int timeoutMs, SearchResult &result,
                    const std::function<void(const SearchResult &)> &onProgress, std::string &error)
{
	result = {};
	if (!writeLine("go " + parameters, error))
		return false;

	const int64_t deadline = util::nowMs() + timeoutMs;
	std::string line;
	while (true)
	{
		const int64_t remaining = deadline - util::nowMs();
		if (remaining <= 0)
		{
			error = "engine timeout";
			stop();
			return false;
		}
		if (!readLine(line, static_cast<int>(remaining), error))
			return false;

		if (line.rfind("info ", 0) == 0)
		{
			parseInfo(line, result);
			if (onProgress)
				onProgress(result);
			continue;
		}
		if (line.rfind("bestmove", 0) != 0)
			continue;

		size_t position = 8;
		while (position < line.size() && line[position] == ' ')
			++position;
		size_t end = line.find(' ', position);
		result.bestMove = line.substr(position, end == std::string::npos ? std::string::npos : end - position);
		if (end != std::string::npos)
		{
			const size_t ponder = line.find("ponder", end);
			if (ponder != std::string::npos)
			{
				size_t start = ponder + 6;
				while (start < line.size() && line[start] == ' ')
					++start;
				const size_t stop = line.find(' ', start);
				result.ponderMove = line.substr(start, stop == std::string::npos ? std::string::npos : stop - start);
			}
		}
		if (result.bestMove == "(none)")
			result.bestMove.clear();
		return true;
	}
}

bool Engine::writeLine(const std::string &line, std::string &error)
{
	const std::string data = line + "\n";
	size_t sent = 0;
	while (sent < data.size())
	{
#ifdef _WIN32
		DWORD written = 0;
		if (WriteFile(static_cast<HANDLE>(toEngine), data.data() + sent, static_cast<DWORD>(data.size() - sent), &written, nullptr))
		{
			if (written == 0)
			{
				error = "engine write failed";
				return false;
			}
			sent += written;
			continue;
		}
		if (GetLastError() == ERROR_IO_PENDING)
		{
			util::sleepMs(1);
			continue;
		}
		error = "engine write failed";
		return false;
#else
		const ssize_t written = ::write(toEngine, data.data() + sent, data.size() - sent);
		if (written > 0)
		{
			sent += static_cast<size_t>(written);
			continue;
		}
		if (written < 0 && (errno == EINTR || errno == EAGAIN))
		{
			pollfd descriptor{ toEngine, POLLOUT, 0 };
			poll(&descriptor, 1, 1000);
			continue;
		}
		error = "engine write failed";
		return false;
#endif
	}
	return true;
}

bool Engine::readLine(std::string &line, int timeoutMs, std::string &error)
{
	const int64_t deadline = util::nowMs() + timeoutMs;
	while (true)
	{
		const size_t newline = buffer.find('\n');
		if (newline != std::string::npos)
		{
			line = buffer.substr(0, newline);
			buffer.erase(0, newline + 1);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			return true;
		}
		if (util::nowMs() > deadline)
		{
			error = "engine read timeout";
			return false;
		}

#ifdef _WIN32
		DWORD available = 0;
		if (!PeekNamedPipe(static_cast<HANDLE>(fromEngine), nullptr, 0, nullptr, &available, nullptr))
		{
			error = "engine read failed";
			stop();
			return false;
		}
		if (available == 0)
		{
			util::sleepMs(5);
			continue;
		}
		char chunk[4096];
		const DWORD want = available < sizeof(chunk) ? available : static_cast<DWORD>(sizeof(chunk));
		DWORD count = 0;
		if (!ReadFile(static_cast<HANDLE>(fromEngine), chunk, want, &count, nullptr) || count == 0)
		{
			error = "engine exited";
			stop();
			return false;
		}
		buffer.append(chunk, static_cast<size_t>(count));
#else
		pollfd descriptor{ fromEngine, POLLIN, 0 };
		const int ready = poll(&descriptor, 1, static_cast<int>(deadline - util::nowMs()));
		if (ready < 0)
		{
			if (errno == EINTR)
				continue;
			error = "engine poll failed";
			return false;
		}
		if (ready == 0)
		{
			error = "engine read timeout";
			return false;
		}

		char chunk[4096];
		const ssize_t count = ::read(fromEngine, chunk, sizeof(chunk));
		if (count > 0)
		{
			buffer.append(chunk, static_cast<size_t>(count));
			continue;
		}
		if (count == 0)
		{
			error = "engine exited";
			stop();
			return false;
		}
		if (errno == EINTR || errno == EAGAIN)
			continue;
		error = std::strerror(errno);
		return false;
#endif
	}
}

bool Engine::waitFor(std::string_view prefix, int timeoutMs, std::string &line, std::string &error)
{
	const int64_t deadline = util::nowMs() + timeoutMs;
	while (true)
	{
		const int64_t remaining = deadline - util::nowMs();
		if (remaining <= 0)
		{
			error = "engine timeout waiting for " + std::string(prefix);
			return false;
		}
		if (!readLine(line, static_cast<int>(remaining), error))
			return false;
		if (line.rfind(prefix, 0) == 0)
			return true;
	}
}

EngineSettings makeEngineSettings(const Config &config)
{
	EngineSettings settings;
	settings.path = config.enginePath;
	settings.net = config.engineNet;
	settings.hash = config.engineHash;
	settings.threads = config.engineThreads;
	settings.options = config.engineOptions;
	return settings;
}

bool startEngine(Engine &engine, const EngineSettings &settings, std::string &error)
{
	if (!engine.start(settings.path, error))
		return false;
	if (!engine.setOption("Hash", std::to_string(settings.hash), error)
	    || !engine.setOption("Threads", std::to_string(settings.threads), error)
	    || !engine.setOption("Ponder", "false", error))
		return false;
	if (!settings.net.empty() && !engine.setOption("EvalFile", settings.net, error))
		return false;
	for (const auto &[name, value] : settings.options)
		if (!engine.setOption(name, value, error))
			return false;
	return true;
}
