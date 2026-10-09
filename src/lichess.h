#pragma once

#include "http.h"
#include "json.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

struct Config;

class Client
{
public:
	explicit Client(const Config &config);

	bool profile(std::string &username, std::string &error);
	bool upgrade(std::string &error);
	bool acceptChallenge(std::string_view id, std::string &error);
	bool declineChallenge(std::string_view id, std::string_view reason, std::string &error);
	bool createChallenge(std::string_view username, std::string_view body, std::string &error);
	bool onlineBots(std::vector<std::string> &names, int limit, std::string &error);
	bool makeMove(std::string_view gameId, std::string_view move, int &status, std::string &error);
	bool offerDraw(std::string_view gameId, std::string &error);
	bool claimVictory(std::string_view gameId, std::string &error);
	bool sendChat(std::string_view gameId, std::string_view room, std::string_view text, std::string &error);
	bool resign(std::string_view gameId, std::string &error);
	bool abort(std::string_view gameId, std::string &error);

	using Handler = std::function<bool(const json::Value &)>;
	bool eventStream(const Handler &handler, const std::atomic<bool> &stop, int &status, std::string &error);
	bool gameStream(std::string_view gameId, const Handler &handler, const std::atomic<bool> &stop, int &status, std::string &error);

	void setStop(const std::atomic<bool> &flag) { stop = &flag; }

private:
	std::string url(std::string_view path) const;
	std::vector<std::string> headers(std::string_view accept) const;
	http::Response call(std::string_view method, std::string_view target, std::string_view body, std::string_view contentType);

	std::string base;
	std::string token;
	std::string agent;
	std::mutex mutex;
	const std::atomic<bool> *stop = nullptr;
};
