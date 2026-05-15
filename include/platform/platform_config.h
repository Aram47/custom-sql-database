#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#define DB_PLATFORM_WIN32 1
#define DB_PLATFORM_POSIX 0

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

using socket_handle_t = SOCKET;
inline constexpr socket_handle_t kInvalidSocket = INVALID_SOCKET;

#else
#define DB_PLATFORM_WIN32 0
#define DB_PLATFORM_POSIX 1

#include <sys/socket.h>
#include <unistd.h>

using socket_handle_t = int;
inline constexpr socket_handle_t kInvalidSocket = -1;

#endif
