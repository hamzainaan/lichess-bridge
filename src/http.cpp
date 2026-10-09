#include "http.h"

#include "platform.h"
#include "util.h"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>

namespace
{
	struct Url
	{
		bool tls = true;
		std::string host;
		std::string port;
		std::string path;
	};

	bool parseUrl(std::string_view url, Url &out)
	{
		std::string_view rest = url;
		if (rest.substr(0, 8) == "https://")
		{
			out.tls = true;
			rest.remove_prefix(8);
		}
		else if (rest.substr(0, 7) == "http://")
		{
			out.tls = false;
			rest.remove_prefix(7);
		}
		else
			return false;

		const size_t slash = rest.find('/');
		const std::string_view authority = rest.substr(0, slash);
		out.path = slash == std::string_view::npos ? "/" : std::string(rest.substr(slash));
		const size_t colon = authority.rfind(':');
		if (colon != std::string_view::npos)
		{
			out.host = std::string(authority.substr(0, colon));
			out.port = std::string(authority.substr(colon + 1));
		}
		else
		{
			out.host = std::string(authority);
			out.port = out.tls ? "443" : "80";
		}
		return !out.host.empty();
	}

	std::string originKey(const Url &url)
	{
		return (url.tls ? "https://" : "http://") + url.host + ":" + url.port;
	}

	SSL_CTX *sslContext()
	{
		static std::once_flag once;
		static SSL_CTX *context = nullptr;
		std::call_once(once, [] {
			context = SSL_CTX_new(TLS_client_method());
			if (context)
			{
				SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION);
				SSL_CTX_set_default_verify_paths(context);
			}
		});
		return context;
	}

	plat::socket_t connectSocket(const Url &url, std::string &error, int timeoutMs)
	{
		plat::ensureNetwork();

		addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;

		addrinfo *result = nullptr;
		if (getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &result) != 0 || !result)
		{
			error = "cannot resolve " + url.host;
			return plat::invalidSocket;
		}

		plat::socket_t socket = plat::invalidSocket;
		for (addrinfo *entry = result; entry; entry = entry->ai_next)
		{
			socket = static_cast<plat::socket_t>(::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol));
			if (socket == plat::invalidSocket)
				continue;

			plat::setNonBlocking(socket, true);
			int rc = ::connect(socket, entry->ai_addr, entry->ai_addrlen);
			if (rc != 0 && plat::inProgress(plat::socketError()))
			{
				if (plat::pollWrite(socket, timeoutMs) > 0)
				{
					int code = 0;
					socklen_t length = sizeof(code);
					getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&code), &length);
					if (code == 0)
						rc = 0;
					else
					{
						error = plat::socketErrorText(code);
						rc = -1;
					}
				}
				else
				{
					error = "connect timeout";
					rc = -1;
				}
			}
			else if (rc != 0)
				error = plat::socketErrorText(plat::socketError());

			if (rc == 0)
				break;

			plat::closeSocket(socket);
		}
		freeaddrinfo(result);

		if (socket == plat::invalidSocket)
		{
			if (error.empty())
				error = "connect failed";
			return plat::invalidSocket;
		}

		plat::setNonBlocking(socket, false);
		int one = 1;
		setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&one), sizeof(one));
		setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char *>(&one), sizeof(one));
#ifdef TCP_KEEPIDLE
		int keepIdle = 15;
		setsockopt(socket, IPPROTO_TCP, TCP_KEEPIDLE, reinterpret_cast<const char *>(&keepIdle), sizeof(keepIdle));
#endif
#ifdef TCP_KEEPINTVL
		int keepInterval = 5;
		setsockopt(socket, IPPROTO_TCP, TCP_KEEPINTVL, reinterpret_cast<const char *>(&keepInterval), sizeof(keepInterval));
#endif
#ifdef TCP_KEEPCNT
		int keepCount = 3;
		setsockopt(socket, IPPROTO_TCP, TCP_KEEPCNT, reinterpret_cast<const char *>(&keepCount), sizeof(keepCount));
#endif
#ifdef _WIN32
		DWORD receiveTimeout = 10000;
#else
		timeval receiveTimeout{ 10, 0 };
#endif
		setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&receiveTimeout), sizeof(receiveTimeout));
		return socket;
	}

	enum class Io
	{
		Data,
		Idle,
		Closed,
		Error
	};

	class Connection
	{
	public:
		Connection() = default;
		Connection(const Connection &) = delete;
		Connection &operator=(const Connection &) = delete;

		~Connection()
		{
			close();
		}

		bool open(const Url &url, std::string &error)
		{
			socket = connectSocket(url, error, 10000);
			if (socket == plat::invalidSocket)
				return false;
			if (!url.tls)
				return true;

			SSL_CTX *context = sslContext();
			if (!context)
			{
				error = "tls context unavailable";
				return false;
			}
			ssl = SSL_new(context);
			if (!ssl)
			{
				error = "tls session unavailable";
				return false;
			}
			SSL_set_fd(ssl, static_cast<int>(socket));
			SSL_set_tlsext_host_name(ssl, url.host.c_str());
			SSL_set1_host(ssl, url.host.c_str());
			if (SSL_connect(ssl) != 1)
			{
				error = "tls handshake failed";
				return false;
			}
			if (SSL_get_verify_result(ssl) != X509_V_OK)
			{
				error = "tls certificate verification failed";
				return false;
			}
			return true;
		}

		void close()
		{
			if (ssl)
			{
				SSL_shutdown(ssl);
				SSL_free(ssl);
				ssl = nullptr;
			}
			plat::closeSocket(socket);
		}

		bool isOpen() const { return socket != plat::invalidSocket; }
		plat::socket_t fd() const { return socket; }

		bool write(std::string_view data, std::string &error)
		{
			size_t sent = 0;
			while (sent < data.size())
			{
				ssize_t written = 0;
				if (ssl)
					written = SSL_write(ssl, data.data() + sent, static_cast<int>(data.size() - sent));
				else
					written = ::send(socket, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);

				if (written > 0)
				{
					sent += static_cast<size_t>(written);
					continue;
				}
				if (!ssl && plat::wouldBlock(plat::socketError()))
				{
					if (plat::pollWrite(socket, 10000) > 0)
						continue;
				}
				error = "write failed";
				return false;
			}
			return true;
		}

		Io read(char *destination, size_t capacity, size_t &count, int timeoutMs, std::string &error)
		{
			count = 0;
			if (ssl && SSL_pending(ssl) > 0)
				return tlsRead(destination, capacity, count, error);

			const int ready = plat::pollRead(socket, timeoutMs);
			if (ready == 0)
				return Io::Idle;
			if (ready < 0)
			{
				error = plat::socketErrorText(plat::socketError());
				return Io::Error;
			}
			if (ssl)
				return tlsRead(destination, capacity, count, error);

			const ssize_t received = ::recv(socket, destination, capacity, 0);
			if (received > 0)
			{
				count = static_cast<size_t>(received);
				return Io::Data;
			}
			if (received == 0)
				return Io::Closed;
			if (plat::wouldBlock(plat::socketError()))
				return Io::Idle;
			error = plat::socketErrorText(plat::socketError());
			return Io::Error;
		}

	private:
		Io tlsRead(char *destination, size_t capacity, size_t &count, std::string &error)
		{
			const int received = SSL_read(ssl, destination, static_cast<int>(capacity));
			if (received > 0)
			{
				count = static_cast<size_t>(received);
				return Io::Data;
			}
			const int code = SSL_get_error(ssl, received);
			if (code == SSL_ERROR_ZERO_RETURN)
				return Io::Closed;
			if (code == SSL_ERROR_WANT_READ || code == SSL_ERROR_WANT_WRITE)
				return Io::Idle;
			error = "tls read failed";
			return Io::Error;
		}

		plat::socket_t socket = plat::invalidSocket;
		SSL *ssl = nullptr;
	};

	using ConnectionPtr = std::unique_ptr<Connection>;

	struct Head
	{
		int status = 0;
		bool http11 = false;
		bool chunked = false;
		bool keepAlive = false;
		bool delimited = false;
		int64_t length = -1;
		std::vector<std::pair<std::string, std::string>> headers;
	};

	class RateLimiter
	{
	public:
		void configure(int intervalMs, int backoffMs)
		{
			std::lock_guard<std::mutex> lock(mutex);
			interval = std::max(0, intervalMs);
			backoff = std::max(0, backoffMs);
		}

		bool acquire(const std::atomic<bool> *stop)
		{
			std::unique_lock<std::mutex> lock(mutex);
			for (;;)
			{
				if (stop && stop->load())
					return false;

				const int64_t now = util::nowMs();
				const int64_t target = std::max(nextAt, cooldownUntil);
				if (now >= target)
					break;

				const int64_t wait = target - now;
				lock.unlock();
				util::sleepMs(std::min<int64_t>(wait, 100));
				lock.lock();
			}
			nextAt = util::nowMs() + interval;
			return true;
		}

		void penalize(const std::vector<std::pair<std::string, std::string>> &headers)
		{
			int64_t wait = backoff;
			for (const auto &[name, value] : headers)
			{
				if (util::lower(name) != "retry-after")
					continue;
				const std::string trimmed = util::trim(value);
				int seconds = 0;
				const auto parsed = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), seconds);
				if (parsed.ec == std::errc() && parsed.ptr == trimmed.data() + trimmed.size() && seconds >= 0)
					wait = static_cast<int64_t>(seconds) * 1000;
				break;
			}
			if (wait <= 0)
				wait = 1000;

			{
				std::lock_guard<std::mutex> lock(mutex);
				const int64_t until = util::nowMs() + wait;
				if (until > cooldownUntil)
					cooldownUntil = until;
			}
			util::warn("rate limited by the server, pausing requests for " + std::to_string(wait / 1000) + "s");
		}

		int64_t cooldownRemainingMs() const
		{
			std::lock_guard<std::mutex> lock(mutex);
			return std::max<int64_t>(0, cooldownUntil - util::nowMs());
		}

	private:
		mutable std::mutex mutex;
		int64_t interval = 0;
		int64_t backoff = 60000;
		int64_t nextAt = 0;
		int64_t cooldownUntil = 0;
	};

	RateLimiter &limiter()
	{
		static RateLimiter instance;
		return instance;
	}

	class ConnectionPool
	{
	public:
		ConnectionPtr take(const Url &url)
		{
			const std::string key = originKey(url);
			std::lock_guard<std::mutex> lock(mutex);
			const auto entry = entries.find(key);
			if (entry == entries.end())
				return nullptr;

			ConnectionPtr connection = std::move(entry->second.connection);
			const int64_t idleSince = entry->second.idleSince;
			entries.erase(entry);
			if (!connection || util::nowMs() - idleSince > idleTimeoutMs || !alive(*connection))
			{
				if (connection)
					connection->close();
				return nullptr;
			}
			return connection;
		}

		void put(const Url &url, ConnectionPtr connection)
		{
			if (!connection || !connection->isOpen())
				return;

			const std::string key = originKey(url);
			std::lock_guard<std::mutex> lock(mutex);
			if (entries.size() >= maxIdle)
			{
				connection->close();
				return;
			}
			Entry &entry = entries[key];
			if (entry.connection)
				entry.connection->close();
			entry.connection = std::move(connection);
			entry.idleSince = util::nowMs();
		}

	private:
		struct Entry
		{
			ConnectionPtr connection;
			int64_t idleSince = 0;
		};

		static bool alive(Connection &connection)
		{
			const int ready = plat::pollRead(connection.fd(), 0);
			if (ready < 0)
				return false;
			return ready == 0;
		}

		static constexpr int64_t idleTimeoutMs = 10000;
		static constexpr size_t maxIdle = 8;

		std::mutex mutex;
		std::map<std::string, Entry> entries;
	};

	ConnectionPool &pool()
	{
		static ConnectionPool instance;
		return instance;
	}

	constexpr int64_t RETRY_COOLDOWN_LIMIT_MS = 10000;

	class Session
	{
	public:
		ConnectionPtr connection;
		std::string buffer;
		std::string error;
		bool closed = false;
		bool reusable = false;
		const std::atomic<bool> *stop = nullptr;

		bool aborted() const { return stop && stop->load(); }

		bool pump(int timeoutMs)
		{
			char chunk[16384];
			size_t count = 0;
			const Io result = connection->read(chunk, sizeof(chunk), count, timeoutMs, error);
			if (result == Io::Data)
			{
				buffer.append(chunk, count);
				return true;
			}
			if (result == Io::Closed)
				closed = true;
			return false;
		}

		size_t waitFor(std::string_view marker, bool streaming, int64_t deadline)
		{
			while (true)
			{
				const size_t index = buffer.find(marker);
				if (index != std::string::npos)
					return index;
				if (aborted())
				{
					error = "stopped";
					return std::string::npos;
				}
				if (!streaming && util::nowMs() > deadline)
				{
					error = "timeout";
					return std::string::npos;
				}
				pump(streaming ? 200 : 250);
				if (closed)
				{
					const size_t finalIndex = buffer.find(marker);
					if (finalIndex != std::string::npos)
						return finalIndex;
					if (error.empty())
						error = "connection closed";
					return std::string::npos;
				}
				if (!error.empty())
					return std::string::npos;
			}
		}

		bool pull(size_t count, const http::Sink &sink, bool streaming, int64_t deadline)
		{
			while (count > 0)
			{
				if (!buffer.empty())
				{
					const size_t take = std::min(count, buffer.size());
					if (sink && !sink(std::string_view(buffer.data(), take)))
					{
						error = "consumer aborted";
						return false;
					}
					buffer.erase(0, take);
					count -= take;
					continue;
				}
				if (aborted())
				{
					error = "stopped";
					return false;
				}
				if (!streaming && util::nowMs() > deadline)
				{
					error = "timeout";
					return false;
				}
				pump(streaming ? 200 : 250);
				if (closed && buffer.empty())
				{
					error = "connection closed";
					return false;
				}
				if (!error.empty())
					return false;
			}
			return true;
		}
	};

	bool parseHead(std::string_view head, Head &out)
	{
		const size_t lineEnd = head.find("\r\n");
		if (lineEnd == std::string_view::npos)
			return false;

		std::string_view statusLine = head.substr(0, lineEnd);
		const size_t firstSpace = statusLine.find(' ');
		if (firstSpace == std::string_view::npos)
			return false;
		out.http11 = statusLine.substr(0, firstSpace) == "HTTP/1.1";
		out.keepAlive = out.http11;
		statusLine.remove_prefix(firstSpace + 1);
		const auto parsed = std::from_chars(statusLine.data(), statusLine.data() + statusLine.size(), out.status);
		if (parsed.ec != std::errc())
			return false;

		size_t position = lineEnd + 2;
		while (position < head.size())
		{
			size_t end = head.find("\r\n", position);
			if (end == std::string_view::npos)
				end = head.size();
			const std::string_view line = head.substr(position, end - position);
			position = end + 2;

			const size_t colon = line.find(':');
			if (colon == std::string_view::npos)
				continue;
			const std::string name = util::lower(util::trim(line.substr(0, colon)));
			const std::string_view value = line.substr(colon + 1);
			out.headers.emplace_back(name, util::trim(value));

			if (name == "transfer-encoding")
			{
				out.chunked = util::lower(value).find("chunked") != std::string::npos;
				if (out.chunked)
					out.delimited = true;
			}
			else if (name == "content-length")
			{
				const std::string trimmed = util::trim(value);
				const auto result = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), out.length);
				if (result.ec != std::errc())
					out.length = -1;
				else
					out.delimited = true;
			}
			else if (name == "connection")
			{
				const std::string lowered = util::lower(value);
				if (lowered.find("close") != std::string::npos)
					out.keepAlive = false;
				else if (lowered.find("keep-alive") != std::string::npos)
					out.keepAlive = true;
			}
		}
		return true;
	}

	bool decodeChunked(Session &session, const http::Sink &sink, bool streaming, int64_t deadline, bool &abortedByConsumer)
	{
		abortedByConsumer = false;
		while (true)
		{
			const size_t lineEnd = session.waitFor("\r\n", streaming, deadline);
			if (lineEnd == std::string::npos)
				return false;

			std::string_view line(session.buffer.data(), lineEnd);
			const size_t semicolon = line.find(';');
			if (semicolon != std::string_view::npos)
				line = line.substr(0, semicolon);

			uint64_t size = 0;
			const auto parsed = std::from_chars(line.data(), line.data() + line.size(), size, 16);
			if (parsed.ec != std::errc())
			{
				session.error = "invalid chunk size";
				return false;
			}
			session.buffer.erase(0, lineEnd + 2);

			if (size == 0)
			{
				if (session.buffer.rfind("\r\n", 0) == 0)
					session.buffer.erase(0, 2);
				return true;
			}
			if (!session.pull(static_cast<size_t>(size), sink, streaming, deadline))
			{
				abortedByConsumer = session.error == "consumer aborted";
				return false;
			}
			if (!session.pull(2, {}, streaming, deadline))
				return false;
		}
	}

	bool decodeFixed(Session &session, int64_t length, const http::Sink &sink, int64_t deadline, bool &abortedByConsumer)
	{
		abortedByConsumer = false;
		if (!session.pull(static_cast<size_t>(length), sink, false, deadline))
		{
			abortedByConsumer = session.error == "consumer aborted";
			return false;
		}
		return true;
	}

	bool decodeUntilClose(Session &session, const http::Sink &sink, bool &abortedByConsumer, int64_t deadline)
	{
		abortedByConsumer = false;
		while (true)
		{
			if (!session.buffer.empty())
			{
				if (sink && !sink(session.buffer))
				{
					session.error = "consumer aborted";
					abortedByConsumer = true;
					return false;
				}
				session.buffer.clear();
			}
			if (session.closed)
				return true;
			if (session.aborted())
			{
				session.error = "stopped";
				return false;
			}
			if (deadline > 0 && util::nowMs() > deadline)
			{
				session.error = "timeout";
				return false;
			}
			session.pump(200);
			if (!session.error.empty())
				return false;
		}
	}

	std::string buildRequest(std::string_view method, const Url &url, const std::vector<std::string> &headers, std::string_view body, bool keepAlive)
	{
		std::string request;
		request.reserve(512 + body.size());
		request.append(method);
		request.append(" ");
		request.append(url.path);
		request.append(" HTTP/1.1\r\nHost: ");
		request.append(url.host);
		if ((url.tls && url.port != "443") || (!url.tls && url.port != "80"))
		{
			request.append(":");
			request.append(url.port);
		}
		request.append("\r\n");

		bool hasUserAgent = false;
		for (const std::string &header : headers)
		{
			if (util::lower(header).rfind("user-agent:", 0) == 0)
				hasUserAgent = true;
			request.append(header);
			request.append("\r\n");
		}
		if (!hasUserAgent)
			request.append("User-Agent: lichess-bridge/1.0\r\n");
		request.append("Accept-Encoding: identity\r\n");
		if (!body.empty() || method == "POST")
		{
			request.append("Content-Length: ");
			request.append(std::to_string(body.size()));
			request.append("\r\n");
		}
		request.append(keepAlive ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n");
		request.append(body);
		return request;
	}

	bool exchange(std::string_view method, const Url &url, const std::vector<std::string> &headers, std::string_view body,
	              Session &session, int timeoutMs, http::Response &response, bool &gotHead)
	{
		gotHead = false;
		const std::string request = buildRequest(method, url, headers, body, true);
		if (!session.connection->write(request, response.error))
			return false;

		const size_t headEnd = session.waitFor("\r\n\r\n", false, util::nowMs() + timeoutMs);
		if (headEnd == std::string::npos)
		{
			response.error = session.error.empty() ? "no response" : session.error;
			return false;
		}
		gotHead = true;

		Head head;
		if (!parseHead(std::string_view(session.buffer.data(), headEnd), head))
		{
			response.error = "malformed response";
			return false;
		}
		session.buffer.erase(0, headEnd + 4);
		response.status = head.status;
		response.headers = head.headers;
		if (head.status == 429)
			limiter().penalize(head.headers);

		const http::Sink sink = [&](std::string_view chunk) {
			response.body.append(chunk);
			return true;
		};

		const bool bodyless = head.status == 204 || head.status == 304
		                   || (head.status >= 100 && head.status < 200) || method == "HEAD";
		const int64_t deadline = util::nowMs() + timeoutMs;
		bool abortedByConsumer = false;
		bool ok = false;
		if (bodyless)
			ok = true;
		else if (head.chunked)
			ok = decodeChunked(session, sink, false, deadline, abortedByConsumer);
		else if (head.length >= 0)
			ok = decodeFixed(session, head.length, sink, deadline, abortedByConsumer);
		else
			ok = decodeUntilClose(session, sink, abortedByConsumer, deadline);

		if (!ok)
		{
			response.error = session.error.empty() ? "read failed" : session.error;
			return false;
		}
		session.reusable = head.keepAlive && (bodyless || head.delimited) && !session.closed;
		return true;
	}

	bool perform(std::string_view method, std::string_view url, const std::vector<std::string> &headers, std::string_view body,
	             const std::atomic<bool> *stop, int timeoutMs, http::Response &response)
	{
		Url parsed;
		if (!parseUrl(url, parsed))
			return response.error = "invalid url", false;

		if (!limiter().acquire(stop))
			return response.error = "stopped", false;

		for (int attempt = 0; attempt < 2; ++attempt)
		{
			Session session;
			session.connection = pool().take(parsed);
			const bool reused = session.connection != nullptr;
			if (!reused)
			{
				session.connection = std::make_unique<Connection>();
				if (!session.connection->open(parsed, response.error))
					return false;
			}

			bool gotHead = false;
			if (exchange(method, parsed, headers, body, session, timeoutMs, response, gotHead))
			{
				if (session.reusable)
					pool().put(parsed, std::move(session.connection));
				return true;
			}

			const bool stopped = stop && stop->load();
			if (reused && attempt == 0 && !gotHead && !stopped && session.error != "timeout")
			{
				response = http::Response{};
				continue;
			}
			return false;
		}
		return false;
	}
}

namespace http
{
	std::string Response::header(std::string_view name) const
	{
		const std::string lowered = util::lower(name);
		for (const auto &[key, value] : headers)
			if (util::lower(key) == lowered)
				return value;
		return {};
	}

	void setRateLimit(int intervalMs, int backoffMs)
	{
		limiter().configure(intervalMs, backoffMs);
	}

	namespace
	{
		Response request(std::string_view method, std::string_view url, const std::vector<std::string> &headers,
		                 std::string_view body, int timeoutMs, const std::atomic<bool> *stop)
		{
			Response response;
			for (int attempt = 0; attempt < 2; ++attempt)
			{
				response = Response{};
				if (!perform(method, url, headers, body, stop, timeoutMs, response))
					break;
				if (response.status != 429)
					break;
				if (limiter().cooldownRemainingMs() > RETRY_COOLDOWN_LIMIT_MS)
					break;
			}
			return response;
		}
	}

	Response get(std::string_view url, const std::vector<std::string> &headers, int timeoutMs, const std::atomic<bool> *stop)
	{
		return request("GET", url, headers, {}, timeoutMs, stop);
	}

	Response post(std::string_view url, const std::vector<std::string> &headers, std::string_view body, int timeoutMs, const std::atomic<bool> *stop)
	{
		return request("POST", url, headers, body, timeoutMs, stop);
	}

	bool stream(std::string_view url, const std::vector<std::string> &headers, const Sink &sink, int &status, std::string &error, const std::atomic<bool> &stop, int timeoutMs)
	{
		Url parsed;
		if (!parseUrl(url, parsed))
			return error = "invalid url", false;

		if (!limiter().acquire(&stop))
			return error = "stopped", false;

		Session session;
		session.stop = &stop;
		session.connection = std::make_unique<Connection>();
		if (!session.connection->open(parsed, error))
			return false;

		const std::string request = buildRequest("GET", parsed, headers, {}, false);
		if (!session.connection->write(request, error))
			return false;

		const bool bounded = timeoutMs > 0;
		const int64_t deadline = bounded ? util::nowMs() + timeoutMs : 0;

		const size_t headEnd = session.waitFor("\r\n\r\n", !bounded, deadline);
		if (headEnd == std::string::npos)
			return error = session.error.empty() ? "no response" : session.error, false;

		Head head;
		if (!parseHead(std::string_view(session.buffer.data(), headEnd), head))
			return error = "malformed response", false;
		session.buffer.erase(0, headEnd + 4);
		status = head.status;
		if (head.status == 429)
			limiter().penalize(head.headers);

		bool abortedByConsumer = false;
		bool ok = false;
		if (head.chunked)
			ok = decodeChunked(session, sink, !bounded, deadline, abortedByConsumer);
		else if (head.length >= 0)
		{
			const int64_t fixedDeadline = bounded ? deadline : util::nowMs() + 60000;
			ok = decodeFixed(session, head.length, sink, fixedDeadline, abortedByConsumer);
		}
		else
			ok = decodeUntilClose(session, sink, abortedByConsumer, deadline);

		if (!ok && !abortedByConsumer && error.empty())
			error = session.error;
		return ok;
	}
}
