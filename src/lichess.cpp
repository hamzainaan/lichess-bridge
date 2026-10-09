#include "lichess.h"

#include "config.h"
#include "util.h"

namespace
{
	std::string describe(const http::Response &response)
	{
		std::string detail = response.error.empty() ? "http " + std::to_string(response.status) : response.error;
		if (!response.body.empty())
			detail += ": " + response.body.substr(0, 200);
		return detail;
	}

	bool drainLines(std::string &buffer, const Client::Handler &handler)
	{
		while (true)
		{
			const size_t newline = buffer.find('\n');
			if (newline == std::string::npos)
				return true;

			std::string line = buffer.substr(0, newline);
			buffer.erase(0, newline + 1);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line.empty())
				continue;

			json::Value value;
			if (!json::parse(line, value))
			{
				util::warn("skipping unparsable stream line");
				continue;
			}
			if (!handler(value))
				return false;
		}
	}
}

Client::Client(const Config &config) : base(config.baseUrl), token(config.token), agent("lichess-bridge/1.0")
{
	if (!base.empty() && base.back() == '/')
		base.pop_back();
}

std::string Client::url(std::string_view path) const
{
	return base + std::string(path);
}

std::vector<std::string> Client::headers(std::string_view accept) const
{
	return { "Authorization: Bearer " + token, "Accept: " + std::string(accept), "User-Agent: " + agent };
}

http::Response Client::call(std::string_view method, std::string_view target, std::string_view body, std::string_view contentType)
{
	std::vector<std::string> requestHeaders = headers("application/json");
	if (!contentType.empty())
		requestHeaders.push_back("Content-Type: " + std::string(contentType));

	std::lock_guard<std::mutex> lock(mutex);
	if (method == "POST")
		return http::post(target, requestHeaders, body, 20000, stop);
	return http::get(target, requestHeaders, 20000, stop);
}

bool Client::profile(std::string &username, std::string &error)
{
	const http::Response response = call("GET", url("/api/account"), "", "");
	if (!response.ok())
		return error = "account request failed: " + describe(response), false;

	json::Value value;
	if (!json::parse(response.body, value))
		return error = "malformed account response", false;
	username = std::string(json::string(value, "username"));
	if (username.empty())
		return error = "account response has no username", false;
	return true;
}

bool Client::upgrade(std::string &error)
{
	const http::Response response = call("POST", url("/api/bot/account/upgrade"), "", "");
	if (!response.ok() && response.status != 400)
		return error = "upgrade failed: " + describe(response), false;
	return true;
}

bool Client::acceptChallenge(std::string_view id, std::string &error)
{
	const http::Response response = call("POST", url("/api/challenge/" + std::string(id) + "/accept"), "", "");
	if (!response.ok())
		return error = "accept failed: " + describe(response), false;
	return true;
}

bool Client::declineChallenge(std::string_view id, std::string_view reason, std::string &error)
{
	const std::string body = "reason=" + util::percentEncode(reason);
	const http::Response response = call("POST", url("/api/challenge/" + std::string(id) + "/decline"), body, "application/x-www-form-urlencoded");
	if (!response.ok())
		return error = "decline failed: " + describe(response), false;
	return true;
}

bool Client::createChallenge(std::string_view username, std::string_view body, std::string &error)
{
	const http::Response response = call("POST", url("/api/challenge/" + std::string(username)), body, "application/x-www-form-urlencoded");
	if (!response.ok())
		return error = "challenge failed: " + describe(response), false;
	return true;
}

bool Client::onlineBots(std::vector<std::string> &names, int limit, std::string &error)
{
	names.clear();
	std::string buffer;
	const http::Sink sink = [&](std::string_view chunk) {
		buffer.append(chunk);
		while (true)
		{
			const size_t newline = buffer.find('\n');
			if (newline == std::string::npos)
				return true;

			std::string line = buffer.substr(0, newline);
			buffer.erase(0, newline + 1);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line.empty())
				continue;

			json::Value value;
			if (!json::parse(line, value))
				continue;
			const std::string_view name = json::string(value, "username");
			if (name.empty())
				continue;
			names.emplace_back(name);
			if (limit > 0 && static_cast<int>(names.size()) >= limit)
				return false;
		}
	};

	int status = 0;
	const std::atomic<bool> never{ false };
	const std::atomic<bool> &flag = stop ? *stop : never;
	const bool ok = http::stream(url("/api/bot/online?nb=" + std::to_string(limit)), headers("application/x-ndjson"), sink, status, error, flag, 30000);
	if (!ok && !error.empty())
		return false;
	if (status != 200)
		return error = "online bots request failed: http " + std::to_string(status), false;
	return true;
}

bool Client::makeMove(std::string_view gameId, std::string_view move, int &status, std::string &error)
{
	const http::Response response = call("POST", url("/api/bot/game/" + std::string(gameId) + "/move/" + std::string(move)), "", "");
	status = response.status;
	if (!response.error.empty())
		return error = response.error, false;
	if (!response.ok())
		return error = describe(response), false;
	return true;
}

bool Client::resign(std::string_view gameId, std::string &error)
{
	const http::Response response = call("POST", url("/api/bot/game/" + std::string(gameId) + "/resign"), "", "");
	if (!response.ok())
		return error = "resign failed: " + describe(response), false;
	return true;
}

bool Client::offerDraw(std::string_view gameId, std::string &error)
{
	const http::Response response = call("POST", url("/api/bot/game/" + std::string(gameId) + "/draw/yes"), "", "");
	if (!response.ok())
		return error = "draw failed: " + describe(response), false;
	return true;
}

bool Client::claimVictory(std::string_view gameId, std::string &error)
{
	const http::Response response = call("POST", url("/api/bot/game/" + std::string(gameId) + "/claim-victory"), "", "");
	if (!response.ok())
		return error = "claim victory failed: " + describe(response), false;
	return true;
}

bool Client::abort(std::string_view gameId, std::string &error)
{
	const http::Response response = call("POST", url("/api/bot/game/" + std::string(gameId) + "/abort"), "", "");
	if (!response.ok())
		return error = "abort failed: " + describe(response), false;
	return true;
}

bool Client::sendChat(std::string_view gameId, std::string_view room, std::string_view text, std::string &error)
{
	const std::string body = "room=" + util::percentEncode(room) + "&text=" + util::percentEncode(text);
	const http::Response response = call("POST", url("/api/bot/game/" + std::string(gameId) + "/chat"), body, "application/x-www-form-urlencoded");
	if (!response.ok())
		return error = "chat failed: " + describe(response), false;
	return true;
}

bool Client::eventStream(const Handler &handler, const std::atomic<bool> &stop, int &status, std::string &error)
{
	std::string buffer;
	const http::Sink sink = [&](std::string_view chunk) {
		buffer.append(chunk);
		return drainLines(buffer, handler);
	};
	return http::stream(url("/api/stream/event"), headers("application/x-ndjson"), sink, status, error, stop);
}

bool Client::gameStream(std::string_view gameId, const Handler &handler, const std::atomic<bool> &stop, int &status, std::string &error)
{
	std::string buffer;
	const http::Sink sink = [&](std::string_view chunk) {
		buffer.append(chunk);
		return drainLines(buffer, handler);
	};
	return http::stream(url("/api/bot/game/stream/" + std::string(gameId)), headers("application/x-ndjson"), sink, status, error, stop);
}
