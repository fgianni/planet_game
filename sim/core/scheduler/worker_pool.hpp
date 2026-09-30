#pragma once

#include <cstddef>
#include <functional>

namespace planetsim {

// A process-wide pool of persistent worker threads for the deterministic
// executor (ADR-0002 §4.6). run(count, task) calls task(worker) once for
// every worker index in [0, count) concurrently, the caller taking index 0,
// and returns when all have finished. Which blocks a worker index handles
// is the caller's affair, so results never depend on the pool.
//
// Threads are created on first use and kept until exit. A call made from
// inside a pool task, or while another thread is using the pool, runs its
// tasks serially on the calling thread instead of waiting, so nesting and
// concurrent callers cannot deadlock. The first exception, by worker
// index, is rethrown after every task has finished.
void run_on_worker_pool(std::size_t count, const std::function<void(std::size_t)>& task);

}  // namespace planetsim
