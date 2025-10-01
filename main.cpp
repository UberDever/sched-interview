#include <cassert>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <set>
#include <atomic>

// clang++ -std=c++20 -stdlib=libc++ -fsanitize=thread,undefined,bounds main.cpp -o sched && ./sched

using TimePoint = std::chrono::steady_clock::time_point;

std::mutex cout_mutex;
std::unordered_map<size_t, TimePoint> expected;
std::unordered_map<size_t, TimePoint> got;
std::atomic_bool no_tasks_left = false;

template<typename Arg>
void print_message(const Arg& arg) {
  // std::cout << arg;
}

template<typename Arg, typename... Args>
void print_message(const Arg& arg, const Args&... rest) {
  // std::cout << arg;
  print_message(rest...);
}

template<typename... Args>
void print(const Args&... args) {
  std::lock_guard g{cout_mutex};
  print_message(args...);
}

template<typename Time>
class Scheduler {
public:
  using Fn = std::function<void()>;
  using Ms = std::chrono::milliseconds;

  struct Job {
    size_t id;
    Fn fn;
    TimePoint launch_at;
    bool canceled;

    bool operator<(const Job& rhs) const {
      return launch_at < rhs.launch_at;
    }
  };

  struct JobComparator {
    bool operator()(const std::shared_ptr<Job>& lhs, const std::shared_ptr<Job>& rhs) const {
      return *lhs < *rhs;
    }
  };

  Scheduler(Time& time) : time{time} {
    execution_thread = std::thread{&Scheduler::loop, this};
  }

  ~Scheduler() {
    if (execution_thread.joinable()) {
      execution_thread.join();
    }
  }

  std::weak_ptr<Job> schedule(size_t id, Fn&& fn, TimePoint at) {
    std::lock_guard g{jobs_mutex};
    auto ptr = std::make_shared<Job>(id, std::move(fn), at, false);
    jobs.insert(ptr);
    jobs_condvar.notify_one();
    return std::weak_ptr<Job>{ptr};
  }

  bool done() {
    std::lock_guard g{jobs_mutex};
    return jobs.empty();
  }

  void cancel(std::weak_ptr<Job>&& handle) {
    std::lock_guard g{jobs_mutex};
    if (handle.expired()) {
      return;
    }
    handle.lock()->canceled = true;
  }

private:
  void loop() {
    while (true) {
      if (done() && no_tasks_left) {
        break;
      }
      execute_pending(time.now());
    }
  }

  void execute_pending(const std::chrono::steady_clock::time_point& now) {
    std::unique_lock g{jobs_mutex};
    while (!jobs.empty()) {
      auto it = jobs.begin();
      auto ptr = it->get();
      if (ptr->canceled) {
        jobs.erase(it);
        continue;
      }
      if (ptr->launch_at > now) {
        jobs_condvar.wait_until(g, ptr->launch_at);
        return;
      }
      print("Executing ", ptr->id, " at ", now.time_since_epoch().count(), '\n');
      got[ptr->id] = now;
      ptr->fn();
      jobs.erase(it);
    }
  }

  std::mutex jobs_mutex;
  std::condition_variable jobs_condvar;
  std::set<std::shared_ptr<Job>, JobComparator> jobs;
  std::thread execution_thread;
  Time& time;
};

struct FakeTime {
  FakeTime() {
    now_ = std::chrono::steady_clock::now();
  }

  TimePoint now() {
    std::lock_guard g{now_mutex};
    return now_;
  };

  template<class T>
  void advance(const T& amount) {
    std::lock_guard g{now_mutex};
    now_ += amount;
  }

private:
  std::mutex now_mutex;
  TimePoint now_;
};

struct RealTime {
  TimePoint now() {
    return std::chrono::steady_clock::now();
  }

  template<class T>
  void advance(const T&) {}
};

struct Task {
  Scheduler<FakeTime>::Fn fn;
  Scheduler<FakeTime>::Ms wait_for;
};

int main() {
  auto time = RealTime{};

  auto handles = std::vector<std::weak_ptr<Scheduler<RealTime>::Job>>{};
  {
    constexpr size_t TASK_AMOUNT = 2048;
    auto s = Scheduler{time};

    for (size_t i = 0; i < TASK_AMOUNT; ++i) {
      const auto now = std::chrono::steady_clock::now();
      const auto wait_for = (rand() % 20) * std::chrono::milliseconds{500};
      const auto will_be_executed_at = now + wait_for;
      print(
          "Scheduled ",
          i,
          " to be executed at ",
          will_be_executed_at.time_since_epoch().count(),
          '\n');
      auto handle = s.schedule(i, []() {}, will_be_executed_at);
      if (rand() % 255 > 64) {
        expected[i] = will_be_executed_at;
        handles.emplace_back(std::move(handle));
      } else {
        s.cancel(std::move(handle));
      }
    }
    no_tasks_left = true;

    while (!s.done()) {
      print("advance to ?ms (if no input: 500ms):\n");
      std::string input = "q";
      // std::getline(std::cin, input);
      if (input == "q") {
        break;
      }
      if (input.empty()) {
        time.advance(std::chrono::milliseconds{500});
      } else {
        uint64_t ms;
        auto err = std::from_chars(input.data(), input.data() + input.size(), ms);
        assert(err.ec == std::error_code{});
        time.advance(std::chrono::milliseconds{ms});
      }
    }

    std::cout << std::endl;
  }
  if (expected.size() != got.size()) {
    std::cout << "sizes differ " << expected.size() << " " << got.size() << std::endl;
    return -1;
  }
  std::cout << "jobs actually executed: " << handles.size() << std::endl;

  const auto delta = std::chrono::microseconds{600};
  for (size_t i = 0; i < expected.size(); ++i) {
    const auto lhs = expected[i];
    const auto rhs = got[i];
    const auto got_delta = std::chrono::abs(lhs - rhs);
    if (got_delta >= delta) {
      std::cout << "very big delta " << i << " " << lhs.time_since_epoch().count() << " "
                << rhs.time_since_epoch().count() << " " << got_delta.count() << "ns" << std::endl;
      return -1;
    }
  }

  return 0;
}
