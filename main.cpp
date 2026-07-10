#include <chrono>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <queue>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std;

// ─── Config
// ────────────────────────────────────────────────────────────────

const int MAP_W = 10;
const int MAP_H = 10;
const int DRIVER_CONFIRM_TIMEOUT_MS = 5000;
const int DISPATCHER_THREADS = 4;
const int MAX_BOOKING_RETRIES = 5;

// ─── Domain Models
// ─────────────────────────────────────────────────────────────────

struct Position {
  int x, y;
};

int dist(Position a, Position b) { return abs(a.x - b.x) + abs(a.y - b.y); }

enum class DriverState { AVAILABLE, RESERVED, BOOKED, OFFLINE };

string driver_state_name(DriverState s) {
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

struct Driver {
  int driver_id;
  DriverState state;
  int booking_id;
  chrono::steady_clock::time_point offer_start_time;

  Position pos;
  mutex mtx;
};

enum class BookingState {
  QUEUED,
  MATCHING,
  WAITING,
  BOOKED,
  COMPLETED,
  FAILED
};

string booking_state_name(BookingState s) {
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

struct Booking {
  int booking_id;
  BookingState state;
  int driver_id;
  int retry_count;

  Position pickup;
  Position destination;
  mutex mtx;
};

// ─── Booking Queue Actor
// ─────────────────────────────────────────────────────────────

struct BookingQueue {
  mutex mtx;
  queue<int> q;

  void push(int id) {
    lock_guard<mutex> lock(mtx);
    q.push(id);
  }

  bool pop(int *id) {
    lock_guard<mutex> lock(mtx);
    if (q.empty())
      return false;
    *id = q.front();
    q.pop();
    return true;
  }
};

// ─── Booking Registry
// ────────────────────────────────────────────────────────── Pure container:
// only add and get. State transitions are inline in callers.

struct BookingRegistry {
  mutex mtx;
  map<int, unique_ptr<Booking>> bookings;

  bool add(unique_ptr<Booking> b) {
    lock_guard<mutex> lock(mtx);
    bookings[b->booking_id] = move(b);
    return true;
  }

  Booking *get(int id) {
    lock_guard<mutex> lock(mtx);
    auto it = bookings.find(id);
    return it == bookings.end() ? nullptr : it->second.get();
  }
};

// ─── Driver Registry
// ─────────────────────────────────────────────────────────── Pure container +
// query (find_nearest is read-only on state).

struct DriverRegistry {
  mutex mtx;
  map<int, unique_ptr<Driver>> drivers;

  bool add(unique_ptr<Driver> d) {
    lock_guard<mutex> lock(mtx);
    drivers[d->driver_id] = move(d);
    return true;
  }

  Driver *get(int id) {
    lock_guard<mutex> lock(mtx);
    auto it = drivers.find(id);
    return it == drivers.end() ? nullptr : it->second.get();
  }

  int find_nearest(Position pickup_pos) {
    int best_id = -1;
    int best_d = INT_MAX;

    for (int pass = 0; pass < 3; pass++) {
      for (auto &entry : drivers) {
        Driver &d = *entry.second;
        if (!d.mtx.try_lock())
          continue;
        if (d.state == DriverState::AVAILABLE) {
          int ddist = dist(d.pos, pickup_pos);
          if (ddist < best_d) {
            best_d = ddist;
            best_id = d.driver_id;
          }
        }
        d.mtx.unlock();
      }
      if (best_id != -1)
        break;
    }
    return best_id;
  }
};

// ─── Server State
// ──────────────────────────────────────────────────────────────────

BookingQueue booking_q;
BookingRegistry booking_reg;
DriverRegistry driver_reg;
mutex next_id_mtx;
mutex log_mtx;
int next_booking_id = 1;

// ─── Logger
// ─────────────────────────────────────────────────────────────

void log_event(const string &level, const string &event, int booking_id,
               int driver_id, const string &message) {
  lock_guard<mutex> lock(log_mtx);
  cerr << "[" << level << "] " << event << " booking=" << booking_id
       << " driver=" << driver_id << " " << message << endl;
}

// ─── Driver Setup Flow
// ─────────────────────────────────────────────────────────────

void add_driver(int id, Position pos) {
  auto d = make_unique<Driver>();
  d->driver_id = id;
  d->state = DriverState::AVAILABLE;
  d->booking_id = -1;
  d->pos = pos;
  driver_reg.add(move(d));
}

// ─── Passenger Booking Flow
// ─────────────────────────────────────────────────────────────

int create_booking(Position pickup, Position destination) {
  int id;
  {
    lock_guard<mutex> lock(next_id_mtx);
    id = next_booking_id++;
  }

  auto b = make_unique<Booking>();
  b->booking_id = id;
  b->state = BookingState::QUEUED;
  b->driver_id = -1;
  b->retry_count = 0;
  b->pickup = pickup;
  b->destination = destination;

  booking_reg.add(move(b));
  booking_q.push(id);
  log_event("INFO", "BOOKING_CREATED", id, -1, "queued");
  return id;
}

// Shared requeue-or-fail logic used by dispatcher, watcher, and reject.
bool requeue_or_fail(int booking_id, bool *requeue) {
  *requeue = false;
  Booking *b = booking_reg.get(booking_id);
  if (!b)
    return false;

  lock_guard<mutex> bl(b->mtx);
  if (b->state == BookingState::BOOKED || b->state == BookingState::COMPLETED ||
      b->state == BookingState::FAILED)
    return false;

  b->retry_count++;
  b->driver_id = -1;

  if (b->retry_count >= MAX_BOOKING_RETRIES) {
    b->state = BookingState::FAILED;
    log_event("INFO", "BOOKING_FAILED", booking_id, -1, "retry limit reached");
    return true;
  }

  b->state = BookingState::QUEUED;
  *requeue = true;
  log_event("INFO", "BOOKING_REQUEUED", booking_id, -1,
            "retry=" + to_string(b->retry_count));
  return true;
}

// ─── Driver Action Flow
// ─────────────────────────────────────────────────────────────

bool accept_booking(int driver_id, int booking_id) {
  Driver *d = driver_reg.get(driver_id);
  if (!d)
    return false;

  {
    lock_guard<mutex> dl(d->mtx);
    if (d->state != DriverState::RESERVED || d->booking_id != booking_id)
      return false;
    d->state = DriverState::BOOKED;
  }

  Booking *b = booking_reg.get(booking_id);
  if (!b)
    return false;

  {
    lock_guard<mutex> bl(b->mtx);
    if (b->state != BookingState::WAITING)
      return false;
    b->state = BookingState::BOOKED;
  }

  log_event("INFO", "DRIVER_ACCEPTED", booking_id, driver_id, "ride booked");
  return true;
}

bool reject_booking(int driver_id, int booking_id) {
  Driver *d = driver_reg.get(driver_id);
  if (!d)
    return false;

  {
    lock_guard<mutex> dl(d->mtx);
    if (d->state != DriverState::RESERVED || d->booking_id != booking_id)
      return false;
    d->state = DriverState::AVAILABLE;
    d->booking_id = -1;
  }

  bool requeue = false;
  if (!requeue_or_fail(booking_id, &requeue))
    return false;
  if (requeue)
    booking_q.push(booking_id);
  log_event("INFO", "DRIVER_REJECTED", booking_id, driver_id, "offer rejected");
  return true;
}

bool finish_booking(int driver_id, int booking_id) {
  Driver *d = driver_reg.get(driver_id);
  if (!d)
    return false;

  Position dest;

  {
    lock_guard<mutex> dl(d->mtx);
    if (d->state != DriverState::BOOKED || d->booking_id != booking_id)
      return false;

    Booking *b = booking_reg.get(booking_id);
    if (!b)
      return false;

    lock_guard<mutex> bl(b->mtx);
    dest = b->destination;
  }

  {
    lock_guard<mutex> dl(d->mtx);
    d->pos = dest;
    d->state = DriverState::AVAILABLE;
    d->booking_id = -1;
  }

  Booking *b = booking_reg.get(booking_id);
  if (!b)
    return false;

  {
    lock_guard<mutex> bl(b->mtx);
    if (b->state != BookingState::BOOKED)
      return false;
    b->state = BookingState::COMPLETED;
  }

  log_event("INFO", "BOOKING_COMPLETED", booking_id, driver_id,
            "ride finished");
  return true;
}

// ─── Dispatcher Actor
// ──────────────────────────────────────────────────────────────────

void dispatcher() {
  while (true) {
    int id;
    if (!booking_q.pop(&id)) {
      this_thread::sleep_for(chrono::milliseconds(50));
      continue;
    }

    Booking *b = booking_reg.get(id);
    if (!b)
      continue;

    Position pickup;

    // QUEUED → MATCHING (inline state transition)
    {
      lock_guard<mutex> bl(b->mtx);
      if (b->state != BookingState::QUEUED)
        continue;
      b->state = BookingState::MATCHING;
      pickup = b->pickup;
    }
    log_event("INFO", "BOOKING_MATCHING", id, -1,
              "dispatcher started matching");

    while (true) {
      int driver_id = driver_reg.find_nearest(pickup);

      if (driver_id < 0) {
        // No driver available at all — wait a meaningful amount of time
        // so drivers have a chance to finish their 5-second confirmation
        // window before burning through retries.
        this_thread::sleep_for(chrono::milliseconds(2000));
        bool requeue = false;
        if (requeue_or_fail(id, &requeue) && requeue)
          booking_q.push(id);
        break;
      }

      Driver *d = driver_reg.get(driver_id);
      if (!d)
        continue;

      // Match found: reserve driver and update booking (inline transition)
      {
        lock_guard<mutex> dl(d->mtx);
        if (d->state != DriverState::AVAILABLE) {
          // Driver was taken by another dispatcher between find_nearest
          // and this lock — yield briefly then retry.
          this_thread::sleep_for(chrono::milliseconds(100));
          continue;
        }

        Booking *b2 = booking_reg.get(id);
        if (!b2)
          continue;

        lock_guard<mutex> bl2(b2->mtx);
        if (b2->state != BookingState::MATCHING)
          continue;

        b2->state = BookingState::WAITING;
        b2->driver_id = d->driver_id;
        d->state = DriverState::RESERVED;
        d->booking_id = id;
        d->offer_start_time = chrono::steady_clock::now();
      }
      log_event("INFO", "DRIVER_RESERVED", id, driver_id,
                "dispatcher reserved driver");
      break;
    }
  }
}

// ─── Monitor Actor
// ──────────────────────────────────────────────────────────────────

void expire_timed_out_reservations() {}

void monitor_actor() {
  while (true) {
    vector<int> expired;

    {
      lock_guard<mutex> rl(driver_reg.mtx);
      for (auto &entry : driver_reg.drivers) {
        Driver &d = *entry.second;
        int bid = -1;

        if (!d.mtx.try_lock())
          continue;
        if (d.state == DriverState::RESERVED) {
          auto elapsed = chrono::duration_cast<chrono::milliseconds>(
                             chrono::steady_clock::now() - d.offer_start_time)
                             .count();
          if (elapsed >= DRIVER_CONFIRM_TIMEOUT_MS) {
            bid = d.booking_id;
            d.state = DriverState::AVAILABLE;
            d.booking_id = -1;
          }
        }
        d.mtx.unlock();
        if (bid != -1)
          expired.push_back(bid);
      }
    }

    for (int id : expired) {
      bool requeue = false;
      if (requeue_or_fail(id, &requeue) && requeue)
        booking_q.push(id);
      log_event("INFO", "DRIVER_TIMEOUT", id, -1, "reservation expired");
    }

    this_thread::sleep_for(chrono::milliseconds(50));
  }
}

// ─── Socket Protocol
// ──────────────────────────────────────────────────────────────────

string dispatch(const string &line) {
  istringstream iss(line);
  string cmd;
  iss >> cmd;
  if (cmd.empty())
    return "ERR EMPTY";

  // BOOK px py dx dy
  if (cmd == "BOOK") {
    int px, py, dx, dy;
    if (!(iss >> px >> py >> dx >> dy))
      return "ERR ARGS";
    return "OK " + to_string(create_booking({px, py}, {dx, dy}));
  }

  // BOOKING id
  if (cmd == "BOOKING") {
    int id;
    if (!(iss >> id))
      return "ERR ARGS";
    Booking *b = booking_reg.get(id);
    if (!b)
      return "ERR NOT_FOUND";
    lock_guard<mutex> bl(b->mtx);
    return booking_state_name(b->state) + " " + to_string(b->driver_id) + " " +
           to_string(b->retry_count);
  }

  // BOOKINGS
  if (cmd == "BOOKINGS") {
    vector<string> lines;
    {
      lock_guard<mutex> rl(booking_reg.mtx);
      for (auto &entry : booking_reg.bookings) {
        Booking &b = *entry.second;
        lock_guard<mutex> bl(b.mtx);
        lines.push_back(
            to_string(b.booking_id) + " " + booking_state_name(b.state) + " " +
            to_string(b.driver_id) + " " + to_string(b.retry_count));
      }
    }
    string resp = "OK " + to_string(lines.size());
    for (auto &l : lines)
      resp += " " + l;
    return resp;
  }

  // DRIVER id
  if (cmd == "DRIVER") {
    int id;
    if (!(iss >> id))
      return "ERR ARGS";
    Driver *d = driver_reg.get(id);
    if (!d)
      return "ERR NOT_FOUND";
    lock_guard<mutex> dl(d->mtx);
    return driver_state_name(d->state) + " " + to_string(d->booking_id) + " " +
           to_string(d->pos.x) + " " + to_string(d->pos.y);
  }

  // DRIVERS
  if (cmd == "DRIVERS") {
    vector<string> lines;
    {
      lock_guard<mutex> rl(driver_reg.mtx);
      for (auto &entry : driver_reg.drivers) {
        Driver &d = *entry.second;
        lock_guard<mutex> dl(d.mtx);
        lines.push_back(to_string(d.driver_id) + " " +
                        driver_state_name(d.state) + " " +
                        to_string(d.booking_id) + " " + to_string(d.pos.x) +
                        " " + to_string(d.pos.y));
      }
    }
    string resp = "OK " + to_string(lines.size());
    for (auto &l : lines)
      resp += " " + l;
    return resp;
  }

  // ACCEPT driver_id booking_id
  // REJECT driver_id booking_id
  // FINISH driver_id booking_id
  if (cmd == "ACCEPT" || cmd == "REJECT" || cmd == "FINISH") {
    int did, bid;
    if (!(iss >> did >> bid))
      return "ERR ARGS";

    if (cmd == "ACCEPT")
      return accept_booking(did, bid) ? "OK" : "ERR";
    if (cmd == "REJECT")
      return reject_booking(did, bid) ? "OK" : "ERR";
    return finish_booking(did, bid) ? "OK" : "ERR";
  }

  return "ERR UNKNOWN";
}

// ─── Socket Server
// ──────────────────────────────────────────────────────────────────

void handler() {
  signal(SIGPIPE, SIG_IGN);

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    cerr << "socket() failed" << endl;
    exit(1);
  }

  int yes = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(8000);

  if (bind(server_fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
    cerr << "bind() failed" << endl;
    exit(1);
  }

  if (listen(server_fd, 10) < 0) {
    cerr << "listen() failed" << endl;
    exit(1);
  }

  while (true) {
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (sockaddr *)&client_addr, &client_len);
    if (client_fd < 0)
      continue;

    char buf[4096];
    int n = recv(client_fd, buf, sizeof(buf) - 1, 0);
    string resp;

    if (n > 0) {
      buf[n] = '\0';
      char *nl = strchr(buf, '\n');
      if (nl)
        *nl = '\0';
      resp = dispatch(string(buf)) + "\n";
      send(client_fd, resp.c_str(), resp.size(), 0);
    }

    close(client_fd);
  }
}

// ─── Main
// ─────────────────────────────────────────────────────────────────────

int main() {
  srand(time(nullptr));
  int num_drivers = 50;

  for (int i = 1; i <= num_drivers; i++) {
    Position p = {rand() % MAP_W, rand() % MAP_H};
    add_driver(i, p);
  }
  log_event("INFO", "SERVER_STARTED", -1, -1,
            "drivers=" + to_string(num_drivers));

  vector<thread> dispatchers;
  for (int i = 0; i < DISPATCHER_THREADS; i++)
    dispatchers.emplace_back(dispatcher);
  thread monitor_th(monitor_actor);

  for (auto &t : dispatchers)
    t.detach();
  monitor_th.detach();

  handler();
  return 0;
}
