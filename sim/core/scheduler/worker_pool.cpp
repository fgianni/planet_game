#include "sim/core/scheduler/worker_pool.hpp"

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace planetsim {
namespace {

thread_local bool inside_pool_task = false;

class WorkerPool {
  public:
    WorkerPool() = default;
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    ~WorkerPool() {
        {
            const std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        for (auto& thread : threads_) {
            thread.join();
        }
    }

    // False if the pool is busy (another caller holds it).
    bool try_run(std::size_t count, const std::function<void(std::size_t)>& task) {
        std::unique_lock busy(busy_, std::try_to_lock);
        if (!busy.owns_lock()) {
            return false;
        }
        std::vector<std::exception_ptr> failures(count);
        {
            std::unique_lock lock(mutex_);
            while (threads_.size() + 1U < count) {
                const std::size_t index = threads_.size() + 1U;
                threads_.emplace_back([this, index] { work(index); });
            }
            task_ = &task;
            failures_ = &failures;
            count_ = count;
            remaining_ = count - 1U;
            ++generation_;
        }
        wake_.notify_all();
        run_task(task, 0U, failures);
        {
            std::unique_lock lock(mutex_);
            done_.wait(lock, [this] { return remaining_ == 0U; });
            task_ = nullptr;
            failures_ = nullptr;
        }
        for (const auto& failure : failures) {
            if (failure) {
                std::rethrow_exception(failure);
            }
        }
        return true;
    }

  private:
    static void run_task(const std::function<void(std::size_t)>& task, std::size_t index,
                         std::vector<std::exception_ptr>& failures) {
        inside_pool_task = true;
        try {
            task(index);
        } catch (...) {
            failures[index] = std::current_exception();
        }
        inside_pool_task = false;
    }

    void work(std::size_t index) {
        std::uint64_t seen = 0;
        for (;;) {
            const std::function<void(std::size_t)>* task = nullptr;
            std::vector<std::exception_ptr>* failures = nullptr;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return stopping_ || generation_ != seen; });
                if (stopping_) {
                    return;
                }
                seen = generation_;
                if (index >= count_) {
                    continue;   // not needed this round
                }
                task = task_;
                failures = failures_;
            }
            run_task(*task, index, *failures);
            {
                const std::lock_guard lock(mutex_);
                --remaining_;
            }
            done_.notify_one();
        }
    }

    std::mutex busy_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable done_;
    std::vector<std::thread> threads_;
    const std::function<void(std::size_t)>* task_ = nullptr;
    std::vector<std::exception_ptr>* failures_ = nullptr;
    std::size_t count_ = 0;
    std::size_t remaining_ = 0;
    std::uint64_t generation_ = 0;
    bool stopping_ = false;
};

WorkerPool& pool() {
    static WorkerPool instance;
    return instance;
}

}  // namespace

void run_on_worker_pool(std::size_t count, const std::function<void(std::size_t)>& task) {
    if (count == 0U) {
        return;
    }
    if (count > 1U && !inside_pool_task && pool().try_run(count, task)) {
        return;
    }
    std::vector<std::exception_ptr> failures(count);
    for (std::size_t index = 0; index < count; ++index) {
        try {
            task(index);
        } catch (...) {
            failures[index] = std::current_exception();
        }
    }
    for (const auto& failure : failures) {
        if (failure) {
            std::rethrow_exception(failure);
        }
    }
}

}  // namespace planetsim
