#pragma once

#include "../common/types.hpp"

#include <mutex>

using namespace std;

struct Booking {
  int id;
  BookingState state;
  int driver_id;
  int retry_count;
  Position pickup;
  Position destination;
  mutex mtx;
};
