#pragma once
#define WIN32_LEAN_AND_MEAN
#include <cstdint>
#include <mutex>
#include <thread>
#include <queue>
#include <atomic>
#include <vector>
#include <cstddef>
#include <span>
#include <array>
#include <condition_variable>
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
            struct CopyRequestBatch {
                std::vector<CopyQueueCopyRequest> CopyRequests;
                uint64_t FenceValue;
                size_t AllocatorIndex;
            };

            static constexpr size_t CopyAllocatorCount{ 3 };
            static constexpr UINT64 UploadHeapSize{ 32ull * 1024ull * 1024ull };
        public:
            CopyQueue(ID3D12Device* device);
            ~CopyQueue();
            CopyQueue(const CopyQueue& other) = delete;
            CopyQueue& operator=(const CopyQueue& other) = delete;
            CopyQueue(CopyQueue&& other) = delete;
            CopyQueue& operator=(CopyQueue&& other) = delete;

        public:
            uint64_t EnqueueCopy(const ComPtr<ID3D12Resource>& destinationDefaultResource, UINT64 destinationOffset, std::span<const std::byte> sourceData);
            uint64_t EnqueueCopy(std::span<const CopyQueueCopyRequest> copyRequests);
            void DispatchCopies(); 

            bool IsFenceComplete(uint64_t fenceValue) const;
            void WaitForFence(uint64_t fenceValue) const;
            void Flush();

        private:
            void WorkerLoop();
            void ExecuteRequestBatch(const CopyRequestBatch& requestBatch);
            void StopWorker();
            void WaitForQueueIdle() const;

        private:
            ComPtr<ID3D12CommandQueue> mCopyCommandQueue{};
            std::array<ComPtr<ID3D12CommandAllocator>, CopyAllocatorCount> mCopyCommandAllocators{};
            ComPtr<ID3D12GraphicsCommandList> mCopyCommandList{};
            ComPtr<ID3D12Fence> mCopyFence{};
            ComPtr<ID3D12Resource> mUploadHeapResource{};
            std::byte* mUploadHeapMappedData{};

            HANDLE mFenceEvent{};

            mutable std::mutex mQueueMutex{};
            mutable std::mutex mFenceMutex{};
            std::condition_variable mQueueCondition{};
            std::queue<CopyRequestBatch> mPendingRequestBatches{};
            bool mDispatchRequested{}; 

            std::thread mWorkerThread{};
            std::atomic_bool mIsRunning{};
            std::atomic<uint64_t> mFenceValueCounter{};
            std::atomic<size_t> mAllocatorCursor{};
            std::array<uint64_t, CopyAllocatorCount> mAllocatorFenceValues{};
        };
    }
}