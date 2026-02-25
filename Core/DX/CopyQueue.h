#pragma once
#define WIN32_LEAN_AND_MEAN
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <unordered_map>
#include <mutex>
#include <queue>
#include <span>
#include <thread>
#include <vector>
#include <Windows.h>
#include "Core/Common.h"
#include "Utility/DirectXInclude.h"

namespace Core {
    namespace DX {
        class CopyQueue final : public Interface::ICopyQueue {
        public:
            CopyQueue(ID3D12Device* Device);
            ~CopyQueue();
            CopyQueue(const CopyQueue& Other) = delete;
            CopyQueue& operator=(const CopyQueue& Other) = delete;
            CopyQueue(CopyQueue&& Other) = delete;
            CopyQueue& operator=(CopyQueue&& Other) = delete;

        public:
            bool Initialize(ID3D12Device* Device) override;
            uint64_t EnqueueCopy(const Interface::CopyQueueCopyRequest& CopyRequest) override;
            uint64_t EnqueueCopy(std::span<const Interface::CopyQueueCopyRequest> CopyRequests) override;

            void DispatchCopies() override;
            bool IsFenceComplete(uint64_t FenceValue) const override;
            void WaitForFence(uint64_t FenceValue) const override;
            void Flush() override;

            uint64_t GetRequiredUploadBufferSize() const override;

        private:
            struct UploadHeapSlot {
                ComPtr<ID3D12Resource> UploadHeapResource{};
                std::byte* UploadHeapMappedData{};
                UINT64 WriteOffset{};
                uint64_t SlotFenceValue{};
            };

            struct CopyRequestBatch {
                std::vector<Interface::CopyQueueCopyRequest> CopyRequests{};
                uint64_t RequestedFenceValue{};
            };

        private:
            void WorkerLoop();
            void ExecuteRequestBatch(const CopyRequestBatch& RequestBatch);
            void StopWorker();
            void WaitForQueueIdle();
            bool IsSubmitFenceComplete(uint64_t FenceValue) const;
            void WaitForSubmitFence(uint64_t FenceValue) const;
            uint64_t ResolveRequestedFenceValue(uint64_t FenceValue) const;
            uint64_t SubmitCurrentCommandList(size_t AllocatorIndex, std::vector<bool>& RequestTouchedMask, std::vector<uint64_t>& RequestCompletedSubmitFenceValues);
            bool TrySwitchUploadHeapSlot(size_t& SlotIndex, size_t& AllocatorIndex, std::vector<bool>& RequestTouchedMask, std::vector<uint64_t>& RequestCompletedSubmitFenceValues);

        private:
            static constexpr size_t CopyAllocatorCount{ 3 };
            static constexpr size_t UploadHeapSlotCount{ 2 };
            static constexpr UINT64 DefaultUploadBufferSize{ 32ull * 1024ull * 1024ull };

            ComPtr<ID3D12CommandQueue> mCopyCommandQueue{};
            std::array<ComPtr<ID3D12CommandAllocator>, CopyAllocatorCount> mCopyCommandAllocators{};
            ComPtr<ID3D12GraphicsCommandList> mCopyCommandList{};
            ComPtr<ID3D12Fence> mCopyFence{};
            std::array<UploadHeapSlot, UploadHeapSlotCount> mUploadHeapSlots{};
            UINT64 mUploadBufferSize{};
            size_t mCurrentUploadHeapSlotIndex{};

            HANDLE mFenceEvent{};

            mutable std::mutex mQueueMutex{};
            mutable std::mutex mFenceMutex{};
            std::condition_variable mQueueCondition{};
            std::queue<CopyRequestBatch> mPendingRequestBatches{};
            bool mDispatchRequested{};

            std::thread mWorkerThread{};
            std::atomic_bool mIsRunning{};
            std::atomic<uint64_t> mRequestedFenceValueCounter{};
            std::atomic<uint64_t> mSubmitFenceValueCounter{};
            std::array<uint64_t, CopyAllocatorCount> mAllocatorFenceValues{};
            std::unordered_map<uint64_t, uint64_t> mRequestedFenceToCompletedSubmitFence{};
        };
    }
}
