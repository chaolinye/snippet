class function_wrapper {
  struct impl_base {
    virtual void call() = 0;
    virtual ~impl_base() {}
  };
  std::unique_ptr<impl_base> impl;
  template <typename F> struct impl_type : impl_base {
    F f;
    impl_type(F &&f_) : f(std::move(f_)) {}
    void call() { f(); }
  };

public:
  template <typename F>
  function_wrapper(F &&f) : impl(new impl_type<F>(std::move(f))) {}
  void operator()() { impl->call(); }
  function_wrapper() = default;
  function_wrapper(function_wrapper &&other) : impl(std::move(other.impl)) {}
  function_wrapper &operator=(function_wrapper &&other) {
    impl = std::move(other.impl);
    return *this;
  }
  function_wrapper(const function_wrapper &) = delete;
  function_wrapper(function_wrapper &) = delete;
  function_wrapper &operator=(const function_wrapper &) = delete;
};
class thread_pool {
  std::atomic_bool done;
  thread_safe_queue<function_wrapper> work_queue; // 使用function_wrapper，而非 std::function
  std::vector<std::thread> threads; // 2
  join_threads joiner;              // 3

  void worker_thread() {
    while (!done) {
      function_wrapper task; // 使用 function_wrapper，而非 std::function
      if (work_queue.try_pop(task)) {
        task();
      } else {
        std::this_thread::yield();
      }
    }
  }

public:
  thread_pool() : done(false), joiner(threads) {
    unsigned const thread_count = std::thread::hardware_concurrency();
    / / 8 try {
      for (unsigned i = 0; i < thread_count; ++i) {
        threads.push_back(std::thread(&thread_pool::worker_thread, this)); // 9
      }
    } catch (...) {
      done = true; // 10
      throw;
    }
  }
  ~thread_pool() {
    done = true; // 11
  }
  template <typename FunctionType>
  std::future<typename std::result_of<FunctionType()>::type> // 1
  submit(FunctionType f) {
    typedef typename std::result_of<FunctionType()>::type result_type; // 2
    std::packaged_task<result_type()> task(std::move(f));              // 3
    std::future<result_type> res(task.get_future());                   // 4
    work_queue.push(std::move(task));                                  // 5
    return res;                                                        // 6
  }
};