#pragma once

#include "sim/core/scheduler/worker_pool.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <utility>
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

    // Worker w handles blocks w, w + W, w + 2W, ...; each block's result
    // depends only on the block, so the assignment never changes a result.
    run_on_worker_pool(active_workers, [&](std::size_t worker) {
        for (std::size_t block_index = worker; block_index < blocks.size();
             block_index += active_workers) {
            function(block_index, blocks[block_index]);
        }
    });
}

template <typename Partial, typename Block, typename BlockFunction, typename CombineFunction>
[[nodiscard]] Partial reduce_deterministic_blocks(std::span<const Block> blocks,
                                                  std::size_t worker_count,
                                                  Partial identity,
                                                  BlockFunction block_function,
                                                  CombineFunction combine) {
    std::vector<Partial> partials(blocks.size(), identity);
    for_each_deterministic_block(
        blocks, worker_count, [&](std::size_t block_index, const Block& block) {
            partials[block_index] = block_function(block_index, block);
        });
    for (const auto& partial : partials) {
        identity = combine(std::move(identity), partial);
    }
    return identity;
}

}  // namespace planetsim
