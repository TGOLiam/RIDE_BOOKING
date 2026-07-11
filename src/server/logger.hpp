#pragma once

#include <iostream>
#include <mutex>
#include <string>

using namespace std;

inline mutex log_mtx;

inline void log_event(const string &level, const string &event, int booking_id,
                      int driver_id, const string &message) {
  lock_guard<mutex> lock(log_mtx);
  cerr << "[" << level << "] " << event << " booking=" << booking_id
       << " driver=" << driver_id << " " << message << endl;
}
