#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <set>

class Scheduler {
public:
  using Fn = std::function<void()>;
  using Ms = std::chrono::milliseconds;
  using TimePoint = std::chrono::steady_clock::time_point;

  struct Job {
    Fn fn;
    TimePoint launch_at;

    bool operator<(const Job& rhs) const {
      return launch_at < rhs.launch_at;
    }
  };

  // Scheduler() : cur_time{std::chrono::steady_clock::now()} {}

  void schedule(Fn&& fn, std::chrono::milliseconds wait_for) {
    std::lock_guard g{jobs_mutex};
    jobs.insert(Job{std::move(fn), std::chrono::steady_clock::now() + wait_for});
  }

  void execute_pending(const std::chrono::steady_clock::time_point& now) {
    std::lock_guard g{jobs_mutex};
    while (!jobs.empty()) {
      auto it = jobs.begin();
      if (it->launch_at > now) {
        return;
      }
      std::cout << "Executing " << reinterpret_cast<uintptr_t>(&it->fn) << " at "
                << now.time_since_epoch().count() << std::endl;
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
  // std::chrono::steady_clock::time_point cur_time;
};

struct Task {
  Scheduler::Fn fn;
  Scheduler::Ms wait_for;
};

int main() {
  auto s = Scheduler{};
  auto t1 = std::thread{[&s]() {
    std::this_thread::sleep_for(std::chrono::milliseconds{1500});
    while (!s.done()) {
      s.execute_pending(std::chrono::steady_clock::now());
    }
  }};

  std::cout << "Cur time is " << std::chrono::steady_clock::now().time_since_epoch().count()
            << std::endl;
  auto tasks = std::vector<Task>{};
  tasks.push_back({[]() {}, Scheduler::Ms{2000}});
  for (auto& j : tasks) {
    const auto now = std::chrono::steady_clock::now();
    std::cout << "Scheduled " << reinterpret_cast<uintptr_t>(&j.fn) << " to be executed at "
              << (now + j.wait_for).time_since_epoch().count() << std::endl;
    s.schedule(std::move(j.fn), j.wait_for);
  }

  t1.join();
  return 0;
}
