class thread_pool {
  typedef function_wrapper task_type;
  std::atomic_bool done;
  thread_safe_queue<task_type> pool_work_queue;
  std::vector<std::unique_ptr<work_stealing_queue>> queues; // 1
  std::vector<std::thread> threads;
  join_threads joiner;
  static thread_local work_stealing_queue *local_work_queue; // 2
  static thread_local unsigned my_index;
  void worker_thread(unsigned my_index_) {
    my_index = my_index_;
    local_work_queue = queues[my_index].get(); // 3
    while (!done) {
      run_pending_task();
    }
  }
  bool pop_task_from_local_queue(task_type &task) {
    return local_work_queue && local_work_queue->try_pop(task);
  }
  bool pop_task_from_pool_queue(task_type &task) {
    return pool_work_queue.try_pop(task);
  }
  bool pop_task_from_other_thread_queue(task_type &task) // 4
  {
    for (unsigned i = 0; i < queues.size(); ++i) {
      unsigned const index = (my_index + i + 1) % queues.size(); // 5
      if (queues[index]->try_steal(task)) {
        return true;
      }
    }
    return false;
  }

public:
  thread_pool() : done(false), joiner(threads) {
    unsigned const thread_count = std::thread::hardware_concurrency();
    try {
      for (unsigned i = 0; i < thread_count; ++i) {
        queues.push_back(std::unique_ptr<work_stealing_queue>( // 6
            new work_stealing_queue));
        threads.push_back(std::thread(&thread_pool::worker_thread, this, i));
      }
    } catch (...) {
      done = true;
      throw;
    }
  }
  ~thread_pool() { done = true; }
  template <typename FunctionType>
  std::future<typename std::result_of<FunctionType()>::type>
  submit(FunctionType f) {
    typedef typename std::result_of<FunctionType()>::type result_type;
    std::packaged_task<result_type()> task(f);
    std::future<result_type> res(task.get_future());
    if (local_work_queue) {
      local_work_queue->push(std::move(task));
    } else {
      pool_work_queue.push(std::move(task));
    }
    return res;
  }
  void run_pending_task() {
    task_type task;
    if (pop_task_from_local_queue(task) ||        // 7
        pop_task_from_pool_queue(task) ||         // 8
        pop_task_from_other_thread_queue(task)) { // 9
      task();
    } else {
      std::this_thread::yield();
    }
  }
};