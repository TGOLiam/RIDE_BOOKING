#pragma once

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>

using namespace std;

const int MAP_W = 100;
const int MAP_H = 100;
const int DISPATCHER_THREADS = 4;
const int DRIVER_CONFIRM_TIMEOUT_MS = 5000;
const int MAX_BOOKING_RETRIES = 5;
const int SERVER_PORT = 8000;

struct Position {
  int x;
  int y;
};

inline int grid_distance(Position a, Position b) {
  return abs(a.x - b.x) + abs(a.y - b.y);
}

enum class DriverState { AVAILABLE, RESERVED, BOOKED, OFFLINE };

enum class BookingState {
  QUEUED,
  MATCHING,
  WAITING,
  BOOKED,
  COMPLETED,
  FAILED
};

inline string driver_state_name(DriverState s) {
  switch (s) {
  case DriverState::AVAILABLE:
    return "AVAILABLE";
  case DriverState::RESERVED:
    return "RESERVED";
  case DriverState::BOOKED:
    return "BOOKED";
  case DriverState::OFFLINE:
    return "OFFLINE";
  }
  return "UNKNOWN";
}

inline string booking_state_name(BookingState s) {
  switch (s) {
  case BookingState::QUEUED:
    return "QUEUED";
  case BookingState::MATCHING:
    return "MATCHING";
  case BookingState::WAITING:
    return "WAITING";
  case BookingState::BOOKED:
    return "BOOKED";
  case BookingState::COMPLETED:
    return "COMPLETED";
  case BookingState::FAILED:
    return "FAILED";
  }
  return "UNKNOWN";
}

struct BookingView {
  int id;
  string state;
  int driver_id;
  int retry_count;
};

struct DriverView {
  int id;
  string state;
  int booking_id;
  int x;
  int y;
};
