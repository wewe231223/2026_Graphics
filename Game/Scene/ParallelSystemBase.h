#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include "External/Include/BS_thread_pool.hpp"
#include "Game/Scene/System.h"

namespace Game {
    class ParallelSystemBase abstract : public ISystem {
    public:
        ParallelSystemBase();
        ~ParallelSystemBase() override;
        ParallelSystemBase(const ParallelSystemBase& Other) = delete;
        ParallelSystemBase& operator=(const ParallelSystemBase& Other) = delete;
        ParallelSystemBase(ParallelSystemBase&& Other) noexcept = delete;
        ParallelSystemBase& operator=(ParallelSystemBase&& Other) noexcept = delete;

    protected:
        void RunParallelBlocks(std::size_t ItemCount, const std::function<void(std::size_t, std::size_t)>& BlockFunction);
        std::size_t ResolveThreadLocalIndex(std::size_t ThreadLocalCount) const;
        std::size_t GetWorkerThreadCount() const;

    private:
        BS::thread_pool<BS::tp::none> mThreadPool;
    };
}
