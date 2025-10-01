#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <set>
#include <atomic>

// clang++ -std=c++20 -stdlib=libc++ -fsanitize=thread,undefined,bounds main.cpp -o sched && ./sched

using TimePoint = std::chrono::steady_clock::time_point;

std::mutex cout_mutex;
std::unordered_map<size_t, TimePoint> expected;
std::unordered_map<size_t, TimePoint> got;

template<typename Arg>
void print_message(const Arg& arg) {
  std::cout << arg;
}

template<typename Arg, typename... Args>
void print_message(const Arg& arg, const Args&... rest) {
  std::cout << arg;
  print_message(rest...);
}

template<typename... Args>
void print(const Args&... args) {
  std::lock_guard g{cout_mutex};
  print_message(args...);
}

class Scheduler {
public:
  using Fn = std::function<void()>;
  using Ms = std::chrono::milliseconds;

  struct Job {
    size_t id;
    Fn fn;
    TimePoint launch_at;

    bool operator<(const Job& rhs) const {
      return launch_at < rhs.launch_at;
    }
  };

  void schedule(size_t id, Fn&& fn, std::chrono::milliseconds wait_for) {
    std::lock_guard g{jobs_mutex};
    jobs.insert(Job{id, std::move(fn), std::chrono::steady_clock::now() + wait_for});
  }

  void execute_pending(const std::chrono::steady_clock::time_point& now) {
    std::lock_guard g{jobs_mutex};
    while (!jobs.empty()) {
      auto it = jobs.begin();
      if (it->launch_at > now) {
        return;
      }
      print("Executing ", it->id, " at ", now.time_since_epoch().count(), '\n');
      got[it->id] = now;
      it->fn();
      jobs.erase(it);
    }
  }

  bool done() {
    std::lock_guard g{jobs_mutex};
    return jobs.empty();
  }

private:
  std::mutex jobs_mutex;
  std::set<Job> jobs;
};

struct Task {
  Scheduler::Fn fn;
  Scheduler::Ms wait_for;
};

int main() {
  auto s = Scheduler{};
  std::atomic_bool no_tasks_left = false;
  auto t1 = std::thread{[&s, &no_tasks_left]() {
    while (true) {
      if (no_tasks_left && s.done()) {
        break;
      }
      s.execute_pending(std::chrono::steady_clock::now());
    }
  }};

  print("Cur time is ", std::chrono::steady_clock::now().time_since_epoch().count(), '\n');
  for (size_t i = 0; i < 64; ++i) {
    const auto now = std::chrono::steady_clock::now();
    const auto wait_for = (rand() % 20) * std::chrono::milliseconds{500};
    const auto will_be_executed_at = now + wait_for;
    print(
        "Scheduled ",
        i,
        " to be executed at ",
        will_be_executed_at.time_since_epoch().count(),
        '\n');
    expected[i] = will_be_executed_at;
    s.schedule(i, []() {}, wait_for);
  }
  no_tasks_left = true;

  t1.join();
  std::cout << std::endl;

  if (expected.size() != got.size()) {
    std::cout << "sizes differ " << expected.size() << " " << got.size() << std::endl;
    return -1;
  }
  const auto delta = std::chrono::microseconds{100};
  for (size_t i = 0; i < expected.size(); ++i) {
    const auto lhs = expected[i];
    const auto rhs = got[i];
    const auto got_delta = std::chrono::abs(lhs - rhs);
    if (got_delta >= delta) {
      std::cout << "very big delta " << i << " " << lhs.time_since_epoch().count() << " "
                << rhs.time_since_epoch().count() << " " << got_delta.count() << std::endl;
      return -1;
    }
  }

  return 0;
}
