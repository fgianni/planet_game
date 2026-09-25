#pragma once

#include <algorithm>
#include <cstddef>
#include <exception>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

namespace planetsim {

template <typename Block, typename Function>
void for_each_deterministic_block(std::span<const Block> blocks, std::size_t worker_count,
                                  Function function) {
    if (worker_count == 0U) {
        throw std::invalid_argument("worker count must be positive");
    }
    if (blocks.empty()) {
        return;
    }

    const std::size_t active_workers = std::min(worker_count, blocks.size());
    if (active_workers == 1U) {
        for (std::size_t block_index = 0; block_index < blocks.size(); ++block_index) {
            function(block_index, blocks[block_index]);
        }
        return;
    }

    std::vector<std::thread> workers;
    std::vector<std::exception_ptr> failures(active_workers);
    workers.reserve(active_workers);
    for (std::size_t worker = 0; worker < active_workers; ++worker) {
        workers.emplace_back([&, worker]() {
            try {
                for (std::size_t block_index = worker; block_index < blocks.size();
                     block_index += active_workers) {
                    function(block_index, blocks[block_index]);
                }
            } catch (...) {
                failures[worker] = std::current_exception();
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    for (const auto& failure : failures) {
        if (failure) {
            std::rethrow_exception(failure);
        }
    }
}

}  // namespace planetsim
