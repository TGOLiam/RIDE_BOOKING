#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace std;

const int DEFAULT_PASSENGERS = 50;
const int DEFAULT_POLL_MS = 100;
const int START_DELAY_MS = 2000;
const int RACE_WINDOW_MS = 250;
const int FINAL_PAUSE_MS = 1500;
const int DRIVER_ID = 1;

// Booking states for the display.
// This demo intentionally has no mutex around the driver availability
// check-and-assign sequence.
enum BookingState { QUEUED = 0, MATCHING, WAITING, FAILED };

struct BookingRecord {
  atomic<int> state;
  atomic<int> driver_id;

  BookingRecord() : state(QUEUED), driver_id(-1) {}
};

struct SharedDriver {
  atomic<bool> available;
  atomic<int> assigned_booking;

  SharedDriver() : available(true), assigned_booking(-1) {}
};

atomic<bool> start_work(false);
atomic<bool> running(true);
atomic<bool> double_booking_found(false);
string violation_message;

string state_name(int state) {
  switch (state) {
  case QUEUED:
    return "QUEUED";
  case MATCHING:
    return "MATCHING";
  case WAITING:
    return "WAITING";
  case FAILED:
    return "FAILED";
  default:
    return "UNKNOWN";
  }
}

void passenger_thread(int booking_id, SharedDriver &driver,
                      vector<BookingRecord> &bookings) {
  while (!start_work.load())
    this_thread::yield();

  if (!running.load())
    return;

  BookingRecord &booking = bookings[booking_id - 1];
  booking.state.store(MATCHING);

  // BUG: this is a check-then-assign race.
  // Several threads can read available == true before any one stores false.
  if (driver.available.load()) {
    // Slow the race window down so the CLI can visibly show threads inside
    // the vulnerable check-before-assign section.
    this_thread::sleep_for(chrono::milliseconds(RACE_WINDOW_MS));

    driver.available.store(false);
    driver.assigned_booking.store(booking_id);
    booking.driver_id.store(DRIVER_ID);
    booking.state.store(WAITING);
  } else {
    booking.state.store(FAILED);
  }
}

bool check_double_booking(const vector<BookingRecord> &bookings,
                          string &detail) {
  int first_booking = -1;

  for (int i = 0; i < static_cast<int>(bookings.size()); i++) {
    int state = bookings[i].state.load();
    int driver = bookings[i].driver_id.load();

    if (state == WAITING && driver == DRIVER_ID) {
      int booking_id = i + 1;
      if (first_booking == -1) {
        first_booking = booking_id;
      } else {
        detail = "driver=" + to_string(DRIVER_ID) + " bookings=" +
                 to_string(first_booking) + "," + to_string(booking_id);
        return true;
      }
    }
  }

  detail = "none";
  return false;
}

void render_dashboard(const vector<BookingRecord> &bookings, int poll_ms,
                      int elapsed_ms, const string &status,
                      const string &detail) {
  int queued = 0, matching = 0, waiting = 0, failed = 0, other = 0;

  for (const BookingRecord &b : bookings) {
    int state = b.state.load();
    if (state == QUEUED)
      queued++;
    else if (state == MATCHING)
      matching++;
    else if (state == WAITING)
      waiting++;
    else if (state == FAILED)
      failed++;
    else
      other++;
  }

  cout << "\033[H\033[J";
  cout << "Problem-Only Simulation: Ride Booking Race Condition\n\n";
  cout << "passengers=" << bookings.size() << "  shared_drivers=1  poll="
       << poll_ms << "ms  elapsed=" << elapsed_ms << "ms\n";
  cout << "mutex=DISABLED  status=" << status << "\n\n";

  cout << left << setw(12) << "STATE" << "COUNT\n";
  cout << "------------------\n";
  cout << left << setw(12) << "QUEUED" << queued << "\n";
  cout << left << setw(12) << "MATCHING" << matching << "\n";
  cout << left << setw(12) << "WAITING" << waiting << "\n";
  cout << left << setw(12) << "FAILED" << failed << "\n";
  cout << left << setw(12) << "OTHER" << other << "\n\n";

  if (status == "DOUBLE_BOOKING_DETECTED")
    cout << "DOUBLE BOOKING OCCURRED (" << detail << ")\n";
  cout.flush();
}

void monitor_thread(const vector<BookingRecord> &bookings, int poll_ms) {
  auto started = chrono::steady_clock::now();

  while (running.load()) {
    string detail;
    bool violation = check_double_booking(bookings, detail);

    int elapsed_ms = static_cast<int>(chrono::duration_cast<chrono::milliseconds>(
                        chrono::steady_clock::now() - started)
                                          .count());

    if (violation) {
      double_booking_found.store(true);
      violation_message = detail;
      render_dashboard(bookings, poll_ms, elapsed_ms, "DOUBLE_BOOKING_DETECTED",
                       detail);
      this_thread::sleep_for(chrono::milliseconds(FINAL_PAUSE_MS));
      running.store(false);
      return;
    }

    render_dashboard(bookings, poll_ms, elapsed_ms, "RUNNING", detail);
    this_thread::sleep_for(chrono::milliseconds(poll_ms));
  }
}

int main(int argc, char **argv) {
  int passenger_count = argc >= 2 ? atoi(argv[1]) : DEFAULT_PASSENGERS;
  int poll_ms = argc >= 3 ? atoi(argv[2]) : DEFAULT_POLL_MS;

  if (passenger_count < 2)
    passenger_count = 2;
  if (poll_ms < 25)
    poll_ms = 25;

  SharedDriver driver;
  vector<BookingRecord> bookings(passenger_count);
  vector<thread> passengers;

  thread monitor(monitor_thread, cref(bookings), poll_ms);

  for (int i = 1; i <= passenger_count; i++)
    passengers.emplace_back(passenger_thread, i, ref(driver), ref(bookings));

  // Let the dashboard show the initial QUEUED state before releasing all
  // passenger threads at the same time.
  this_thread::sleep_for(chrono::milliseconds(START_DELAY_MS));
  start_work.store(true);

  for (thread &t : passengers)
    t.join();

  if (!double_booking_found.load()) {
    string detail;
    if (check_double_booking(bookings, detail)) {
      double_booking_found.store(true);
      violation_message = detail;
      running.store(false);
    }
  }

  running.store(false);
  monitor.join();

  cout << "\nFinal result: ";
  if (double_booking_found.load()) {
    cout << "DOUBLE BOOKING OCCURRED (" << violation_message << ")\n";
    return 1;
  }

  cout << "no double booking observed; rerun with more passengers.\n";
  return 0;
}
