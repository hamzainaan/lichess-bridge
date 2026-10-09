#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace http
{
	struct Response
	{
		int status = 0;
		std::string body;
		std::string error;
		std::vector<std::pair<std::string, std::string>> headers;

		bool ok() const { return status >= 200 && status < 300; }
		std::string header(std::string_view name) const;
	};

	using Sink = std::function<bool(std::string_view)>;

	void setRateLimit(int intervalMs, int backoffMs);

	Response get(std::string_view url, const std::vector<std::string> &headers, int timeoutMs = 20000, const std::atomic<bool> *stop = nullptr);
	Response post(std::string_view url, const std::vector<std::string> &headers, std::string_view body, int timeoutMs = 20000, const std::atomic<bool> *stop = nullptr);
	bool stream(std::string_view url, const std::vector<std::string> &headers, const Sink &sink, int &status, std::string &error, const std::atomic<bool> &stop, int timeoutMs = 0);
}
