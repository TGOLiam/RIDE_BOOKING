#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std;

const int DEFAULT_PASSENGERS = 50;
const int DRIVER_ID = 1;

struct Assignment {
  int booking_id = -1;
  int driver_id = -1;
};

struct SharedDriverUnsafe {
  bool available = true;
  int assigned_booking = -1;
};

struct SharedDriverSafe {
  bool available = true;
  int assigned_booking = -1;
  mutex mtx;
};

void unsafe_booking_worker(int booking_id, SharedDriverUnsafe &driver,
                           vector<Assignment> &assignments) {
  // Broken check-then-assign sequence:
  // many threads can observe available == true before any one marks it false.
  if (driver.available) {
    this_thread::sleep_for(chrono::milliseconds(2));
    driver.available = false;
    driver.assigned_booking = booking_id;
    assignments[booking_id - 1] = {booking_id, DRIVER_ID};
  }
}

void safe_booking_worker(int booking_id, SharedDriverSafe &driver,
                         vector<Assignment> &assignments) {
  lock_guard<mutex> lock(driver.mtx);
  if (driver.available) {
    this_thread::sleep_for(chrono::milliseconds(2));
    driver.available = false;
    driver.assigned_booking = booking_id;
    assignments[booking_id - 1] = {booking_id, DRIVER_ID};
  }
}

int count_assignments(const vector<Assignment> &assignments) {
  int count = 0;
  for (const Assignment &a : assignments) {
    if (a.driver_id == DRIVER_ID)
      count++;
  }
  return count;
}

void print_result(const string &mode, const vector<Assignment> &assignments) {
  int assigned = count_assignments(assignments);

  cout << "Mode: " << mode << "\n";
  cout << "Driver " << DRIVER_ID << " assigned to " << assigned
       << " booking(s).\n\n";

  for (const Assignment &a : assignments) {
    if (a.driver_id == DRIVER_ID)
      cout << "booking " << a.booking_id << " got driver " << a.driver_id
           << "\n";
  }

  cout << "\n";
  if (assigned > 1) {
    cout << "DOUBLE BOOKING DETECTED: the same driver was assigned to "
         << assigned << " bookings.\n";
  } else {
    cout << "No double booking detected.\n";
  }
}

int run_unsafe(int passengers) {
  SharedDriverUnsafe driver;
  vector<Assignment> assignments(passengers);
  vector<thread> threads;

  for (int i = 1; i <= passengers; i++)
    threads.emplace_back(unsafe_booking_worker, i, ref(driver),
                         ref(assignments));

  for (thread &t : threads)
    t.join();

  print_result("unsafe/no mutex", assignments);
  return count_assignments(assignments) > 1 ? 1 : 0;
}

int run_safe(int passengers) {
  SharedDriverSafe driver;
  vector<Assignment> assignments(passengers);
  vector<thread> threads;

  for (int i = 1; i <= passengers; i++)
    threads.emplace_back(safe_booking_worker, i, ref(driver), ref(assignments));

  for (thread &t : threads)
    t.join();

  print_result("safe/mutex", assignments);
  return count_assignments(assignments) > 1 ? 1 : 0;
}

int main(int argc, char **argv) {
  string mode = argc >= 2 ? argv[1] : "unsafe";
  int passengers = argc >= 3 ? atoi(argv[2]) : DEFAULT_PASSENGERS;
  if (passengers < 2)
    passengers = 2;

  cout << "Race condition demo: check availability + assign driver\n";
  cout << "passenger threads=" << passengers << " shared drivers=1\n\n";

  if (mode == "unsafe")
    return run_unsafe(passengers);
  if (mode == "safe")
    return run_safe(passengers);

  cerr << "usage: " << argv[0] << " [unsafe|safe] [passenger_threads]\n";
  return 2;
}
