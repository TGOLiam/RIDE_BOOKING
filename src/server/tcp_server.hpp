#pragma once

#include "../common/socket_utils.hpp"
#include "booking_system.hpp"
#include "protocol.hpp"

#include <cstring>
#include <iostream>
#include <string>

using namespace std;

class TcpServer {
private:
  Socket server_fd;

public:
  TcpServer() : server_fd(INVALID_SOCKET_FD) {}

  ~TcpServer() {
    if (server_fd != INVALID_SOCKET_FD)
      socket_close(server_fd);
  }

  bool start(int port) {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET_FD) {
      cerr << "socket() failed" << endl;
      return false;
    }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes,
               sizeof(yes));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);

#ifdef _WIN32
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
#else
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#endif

    if (bind(server_fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
      cerr << "bind() failed" << endl;
      return false;
    }

    if (listen(server_fd, 10) < 0) {
      cerr << "listen() failed" << endl;
      return false;
    }

    return true;
  }

  void run(BookingSystem &system) {
    while (true) {
      sockaddr_in client_addr;
      memset(&client_addr, 0, sizeof(client_addr));
#ifdef _WIN32
      int client_len = sizeof(client_addr);
#else
      socklen_t client_len = sizeof(client_addr);
#endif

      Socket client_fd =
          accept(server_fd, (sockaddr *)&client_addr, &client_len);
      if (client_fd == INVALID_SOCKET_FD)
        continue;

      char buffer[4096];
      int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

      if (n > 0) {
        buffer[n] = '\0';
        char *nl = strchr(buffer, '\n');
        if (nl)
          *nl = '\0';

        string response = handle_command(system, string(buffer)) + "\n";
        send(client_fd, response.c_str(), (int)response.size(), 0);
      }

      socket_close(client_fd);
    }
  }
};
