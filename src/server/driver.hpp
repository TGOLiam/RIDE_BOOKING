#pragma once

#include "../common/types.hpp"

#include <chrono>
#include <mutex>

using namespace std;

struct Driver {
  int id;
  DriverState state;
  int booking_id;
  Position pos;
  chrono::steady_clock::time_point offer_start_time;
  mutex mtx;
};
