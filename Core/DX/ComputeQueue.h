#pragma once
#define WIN32_LEAN_AND_MEAN
#include <array>
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>
#include <Windows.h>
#include "Core/Common.h"
#include "Utility/DirectXInclude.h"

#ifdef max
#undef max
#endif

namespace Core {
    namespace DX {
        class ComputeQueue final : public Interface::IComputeQueue {
        public:
            ComputeQueue(ID3D12Device* Device);
            ~ComputeQueue();
            ComputeQueue(const ComputeQueue& Other) = delete;
            ComputeQueue& operator=(const ComputeQueue& Other) = delete;
            ComputeQueue(ComputeQueue&& Other) = delete;
            ComputeQueue& operator=(ComputeQueue&& Other) = delete;

        public:
            bool Initialize(ID3D12Device* Device) override;
            Interface::Future EnqueueComputeFuture(const Interface::ComputeQueueDispatchRequest& DispatchRequest) override;
            Interface::Future EnqueueComputeFuture(std::span<const Interface::ComputeQueueDispatchRequest> DispatchRequests) override;

            void DispatchComputes() override;
            bool IsFutureComplete(std::uint64_t ComputeTicket) const override;
            void WaitFuture(std::uint64_t ComputeTicket) const override;
            void QueueWaitFuture(ID3D12CommandQueue* WaitingQueue, std::uint64_t ComputeTicket) const override;
            void Flush() override;
            ID3D12CommandQueue* GetCommandQueue() const override;

        private:
            struct ComputeRequestBatch final {
                std::vector<Interface::ComputeQueueDispatchRequest> DispatchRequests{};
                std::uint64_t ComputeTicket{};
            };

            struct ComputeTicketFenceState final {
                std::uint64_t LastSubmitFenceValue{};
                std::uint64_t PendingBatchCount{};
            };

        private:
            void WorkerLoop();
            void ExecuteRequestBatch(ComputeRequestBatch& RequestBatch);
            void RecordDispatchRequest(const Interface::ComputeQueueDispatchRequest& DispatchRequest);
            void StopWorker();
            void WaitForQueueIdle();
            void RequestDispatch() const;
            bool IsSubmitFenceComplete(std::uint64_t FenceValue) const;
            void WaitForSubmitFence(std::uint64_t FenceValue) const;
            bool EnqueuePreparedComputeRequests(std::uint64_t ComputeTicket, std::vector<Interface::ComputeQueueDispatchRequest>& DispatchRequests);
            std::uint64_t ResolveComputeTicketToFenceValue(std::uint64_t ComputeTicket) const;
            std::uint64_t GenerateComputeTicket();
            void CompleteComputeTicket(std::uint64_t ComputeTicket, std::uint64_t SubmitFenceValue);

        private:
            static constexpr std::uint32_t ComputeAllocatorFlightCount{ 3 };

            Microsoft::WRL::ComPtr<ID3D12CommandQueue> mComputeCommandQueue{};
            std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, ComputeAllocatorFlightCount> mComputeCommandAllocators{};
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mComputeCommandList{};
            Microsoft::WRL::ComPtr<ID3D12Fence> mComputeFence{};

            std::uint32_t mCurrentAllocatorFlightIndex{};
            std::array<std::uint64_t, ComputeAllocatorFlightCount> mAllocatorFlightFenceValues{};

            HANDLE mFenceEvent{};

            mutable std::mutex mQueueMutex{};
            mutable std::mutex mFenceMutex{};
            mutable std::condition_variable mQueueCondition{};
            mutable std::condition_variable mFenceCondition{};
            std::queue<ComputeRequestBatch> mPendingRequestBatches{};
            mutable bool mDispatchRequested{};

            std::thread mWorkerThread{};
            std::atomic_bool mIsRunning{};
            std::atomic<std::uint64_t> mSubmitFenceValueCounter{};
            std::atomic<std::uint64_t> mComputeTicketCounter{};
            std::unordered_map<std::uint64_t, ComputeTicketFenceState> mComputeTicketFenceStates{};
        };
    }
}
