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
#include "Utility/DirectXInclude.h"

namespace Core {
    namespace DX {
        struct CopyQueueCopyRequest {
            ComPtr<ID3D12Resource> DestinationDefaultResource;
            UINT64 DestinationOffset;
            std::vector<std::byte> SourceData;
        };

        class CopyQueue {
        public:
            CopyQueue(ID3D12Device* device);
            ~CopyQueue();
            CopyQueue(const CopyQueue& other) = delete;
            CopyQueue& operator=(const CopyQueue& other) = delete;
            CopyQueue(CopyQueue&& other) = delete;
            CopyQueue& operator=(CopyQueue&& other) = delete;

        public:
            bool Initialize(ID3D12Device* device);
            uint64_t EnqueueCopy(const CopyQueueCopyRequest& copyRequest);
            uint64_t EnqueueCopy(std::span<const CopyQueueCopyRequest> copyRequests);

            void DispatchCopies();
            bool IsFenceComplete(uint64_t fenceValue) const;
            void WaitForFence(uint64_t fenceValue) const;
            void Flush();

            static UINT64 GetRequiredUploadBufferSize();

        private:
            struct UploadHeapSlot {
                ComPtr<ID3D12Resource> UploadHeapResource;
                std::byte* UploadHeapMappedData;
                UINT64 WriteOffset;
                uint64_t SlotFenceValue;
            };

            struct CopyRequestBatch {
                std::vector<CopyQueueCopyRequest> CopyRequests;
                uint64_t RequestedFenceValue;
            };

            void WorkerLoop();
            void ExecuteRequestBatch(const CopyRequestBatch& requestBatch);
            void StopWorker();
            void WaitForQueueIdle();
            bool IsSubmitFenceComplete(uint64_t fenceValue) const;
            void WaitForSubmitFence(uint64_t fenceValue) const;
            uint64_t ResolveRequestedFenceValue(uint64_t fenceValue) const;
            uint64_t SubmitCurrentCommandList(size_t allocatorIndex, std::vector<bool>& requestTouchedMask, std::vector<uint64_t>& requestCompletedSubmitFenceValues);
            bool TrySwitchUploadHeapSlot(size_t& slotIndex, size_t& allocatorIndex, std::vector<bool>& requestTouchedMask, std::vector<uint64_t>& requestCompletedSubmitFenceValues);

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
