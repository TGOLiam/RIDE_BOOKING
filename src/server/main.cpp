#include "../common/socket_utils.hpp"
#include "../common/types.hpp"
#include "booking_system.hpp"
#include "tcp_server.hpp"

#include <cstdlib>
#include <ctime>
#include <iostream>

using namespace std;

int main() {
  srand((unsigned)time(nullptr));

  if (!socket_init()) {
    cerr << "socket init failed" << endl;
    return 1;
  }

  BookingSystem system;

  TcpServer server;
  if (!server.start(SERVER_PORT)) {
    socket_cleanup();
    return 1;
  }

  system.start_workers();
  server.run(system);

  socket_cleanup();
  return 0;
}
