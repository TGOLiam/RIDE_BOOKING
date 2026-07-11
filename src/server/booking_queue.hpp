#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

using namespace std;

class BookingQueue {
private:
  queue<int> q;
  mutex mtx;
  condition_variable cv;

public:
  void push(int id) {
    {
      lock_guard<mutex> lock(mtx);
      q.push(id);
    }
    cv.notify_one();
  }

  int wait_pop() {
    unique_lock<mutex> lock(mtx);
    cv.wait(lock, [&] { return !q.empty(); });
    int id = q.front();
    q.pop();
    return id;
  }
};
