#include "platform.h"

#include <mutex>

#ifdef _WIN32

namespace
{
	std::once_flag networkOnce;
}

namespace plat
{
	std::string socketErrorText(int error)
	{
		return "socket error " + std::to_string(error);
	}

	void init()
	{
		SetConsoleOutputCP(CP_UTF8);
		SetConsoleCP(CP_UTF8);
		HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
		DWORD mode = 0;
		if (output != INVALID_HANDLE_VALUE && GetConsoleMode(output, &mode))
			SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
	}

	void ensureNetwork()
	{
		std::call_once(networkOnce, [] {
			WSADATA data{};
			WSAStartup(MAKEWORD(2, 2), &data);
		});
	}

	void closeSocket(socket_t &socket)
	{
		if (socket != invalidSocket)
		{
			closesocket(static_cast<SOCKET>(socket));
			socket = invalidSocket;
		}
	}

	bool setNonBlocking(socket_t socket, bool enabled)
	{
		u_long mode = enabled ? 1 : 0;
		return ioctlsocket(static_cast<SOCKET>(socket), FIONBIO, &mode) == 0;
	}

	int pollRead(socket_t socket, int timeoutMs)
	{
		WSAPOLLFD descriptor{};
		descriptor.fd = static_cast<SOCKET>(socket);
		descriptor.events = POLLIN;
		const int ready = WSAPoll(&descriptor, 1, timeoutMs);
		if (ready <= 0)
			return ready;
		if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))
			return 1;
		return (descriptor.revents & POLLIN) ? 1 : 0;
	}

	int pollWrite(socket_t socket, int timeoutMs)
	{
		WSAPOLLFD descriptor{};
		descriptor.fd = static_cast<SOCKET>(socket);
		descriptor.events = POLLOUT;
		const int ready = WSAPoll(&descriptor, 1, timeoutMs);
		if (ready <= 0)
			return ready;
		if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))
			return 1;
		return (descriptor.revents & POLLOUT) ? 1 : 0;
	}

	void localTime(std::time_t seconds, std::tm &out)
	{
		localtime_s(&out, &seconds);
	}

	bool stderrIsTty()
	{
		return _isatty(_fileno(stderr)) == 1;
	}
}

#else

namespace plat
{
	void init() {}
	void ensureNetwork() {}

	void closeSocket(socket_t &socket)
	{
		if (socket != invalidSocket)
		{
			::close(socket);
			socket = invalidSocket;
		}
	}

	bool setNonBlocking(socket_t socket, bool enabled)
	{
		const int flags = fcntl(socket, F_GETFL, 0);
		if (flags < 0)
			return false;
		const int updated = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
		return fcntl(socket, F_SETFL, updated) == 0;
	}

	int pollRead(socket_t socket, int timeoutMs)
	{
		pollfd descriptor{ socket, POLLIN, 0 };
		return poll(&descriptor, 1, timeoutMs);
	}

	int pollWrite(socket_t socket, int timeoutMs)
	{
		pollfd descriptor{ socket, POLLOUT, 0 };
		return poll(&descriptor, 1, timeoutMs);
	}

	void localTime(std::time_t seconds, std::tm &out)
	{
		localtime_r(&seconds, &out);
	}

	bool stderrIsTty()
	{
		return isatty(STDERR_FILENO) == 1;
	}
}

#endif
