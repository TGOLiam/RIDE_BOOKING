#pragma once

#include <csignal>
#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using Socket = SOCKET;
const Socket INVALID_SOCKET_FD = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
const Socket INVALID_SOCKET_FD = -1;
#endif

inline bool socket_init() {
#ifdef _WIN32
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
  signal(SIGPIPE, SIG_IGN);
  return true;
#endif
}

inline void socket_cleanup() {
#ifdef _WIN32
  WSACleanup();
#endif
}

inline void socket_close(Socket s) {
#ifdef _WIN32
  closesocket(s);
#else
  close(s);
#endif
}
