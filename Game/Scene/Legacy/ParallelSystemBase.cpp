#include "ParallelSystemBase.h"

namespace Game {
    ParallelSystemBase::ParallelSystemBase()
        : mThreadPool{} {
    }

    ParallelSystemBase::~ParallelSystemBase() {
    }

    void ParallelSystemBase::RunParallelBlocks(std::size_t ItemCount, const std::function<void(std::size_t, std::size_t)>& BlockFunction) {
        if (ItemCount == 0) {
            return;
        }

        BS::multi_future<void> BuildFutures{ mThreadPool.submit_blocks<std::size_t>(0, ItemCount, BlockFunction) };
        BuildFutures.wait();
    }

    std::size_t ParallelSystemBase::GetWorkerThreadCount() const {
        const std::size_t WorkerThreadCount{ mThreadPool.get_thread_count() };
        if (WorkerThreadCount == 0) {
            return 1;
        }

        return WorkerThreadCount;
    }

    std::size_t ParallelSystemBase::ResolveThreadLocalIndex(std::size_t ThreadLocalCount) const {
        if (ThreadLocalCount == 0) {
            return 0;
        }

        const std::optional<std::size_t> WorkerIndex{ BS::this_thread::get_index() };
        if (WorkerIndex.has_value() == false) {
            return 0;
        }

        if (WorkerIndex.value() >= ThreadLocalCount) {
            return 0;
        }

        return WorkerIndex.value();
    }
}
