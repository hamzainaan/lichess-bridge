#pragma once


#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <io.h>

#else

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace plat
{
#ifdef _WIN32
	using socket_t = intptr_t;
	using process_t = void *;
	using handle_t = void *;

	inline constexpr socket_t invalidSocket = -1;
	inline constexpr process_t invalidProcess = nullptr;
	inline constexpr handle_t invalidHandle = nullptr;

	inline int socketError() { return WSAGetLastError(); }
	inline bool wouldBlock(int error) { return error == WSAEWOULDBLOCK; }
	inline bool inProgress(int error) { return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEALREADY; }
	inline bool interrupted(int error) { return error == WSAEINTR; }
	std::string socketErrorText(int error);
#else
	using socket_t = int;
	using process_t = int;
	using handle_t = int;

	inline constexpr socket_t invalidSocket = -1;
	inline constexpr process_t invalidProcess = -1;
	inline constexpr handle_t invalidHandle = -1;

	inline int socketError() { return errno; }
	inline bool wouldBlock(int error) { return error == EAGAIN || error == EWOULDBLOCK; }
	inline bool inProgress(int error) { return error == EINPROGRESS; }
	inline bool interrupted(int error) { return error == EINTR; }
	inline std::string socketErrorText(int error) { return std::strerror(error); }
#endif

	void init();
	void ensureNetwork();

	void closeSocket(socket_t &socket);
	bool setNonBlocking(socket_t socket, bool enabled);
	int pollRead(socket_t socket, int timeoutMs);
	int pollWrite(socket_t socket, int timeoutMs);

	void localTime(std::time_t seconds, std::tm &out);
	bool stderrIsTty();
}
