#pragma once

#include "platform.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct Config;

struct SearchResult
{
	std::string bestMove;
	std::string ponderMove;
	std::string line;
	int depth = 0;
	int seldepth = 0;
	bool scored = false;
	int cp = 0;
	int mate = 0;
	uint64_t nodes = 0;
	uint64_t nps = 0;
	int hashfull = 0;
	uint64_t tbhits = 0;
	int64_t timeMs = 0;
};

class Engine
{
public:
	~Engine();

	bool start(const std::string &path, std::string &error);
	void stop();
	bool running() const { return pid != plat::invalidProcess; }
	bool setOption(const std::string &name, const std::string &value, std::string &error);
	bool newGame(std::string &error);
	bool position(const std::string &startFen, const std::vector<std::string> &moves, std::string &error);
	bool search(const std::string &parameters, int timeoutMs, SearchResult &result,
	            const std::function<void(const SearchResult &)> &onProgress, std::string &error);

private:
	bool writeLine(const std::string &line, std::string &error);
	bool readLine(std::string &line, int timeoutMs, std::string &error);
	bool waitFor(std::string_view prefix, int timeoutMs, std::string &line, std::string &error);

	plat::process_t pid = plat::invalidProcess;
	plat::handle_t toEngine = plat::invalidHandle;
	plat::handle_t fromEngine = plat::invalidHandle;
	std::string buffer;
};

struct EngineSettings
{
	std::string path;
	std::string net;
	int hash = 64;
	int threads = 1;
	std::vector<std::pair<std::string, std::string>> options;
};

EngineSettings makeEngineSettings(const Config &config);
bool startEngine(Engine &engine, const EngineSettings &settings, std::string &error);
