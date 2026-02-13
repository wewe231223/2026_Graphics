#pragma once
#define WIN32_LEAN_AND_MEAN
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
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
            struct CopyRequestBatch {
                std::vector<CopyQueueCopyRequest> CopyRequests;
                uint64_t FenceValue;
                size_t AllocatorIndex;
            };

            void WorkerLoop();
            void ExecuteRequestBatch(const CopyRequestBatch& requestBatch);
            void StopWorker();
            void WaitForQueueIdle() const;

        private:
            static constexpr size_t CopyAllocatorCount{ 3 };
            static constexpr UINT64 DefaultUploadBufferSize{ 32ull * 1024ull * 1024ull };

            ComPtr<ID3D12CommandQueue> mCopyCommandQueue{};
            std::array<ComPtr<ID3D12CommandAllocator>, CopyAllocatorCount> mCopyCommandAllocators{};
            ComPtr<ID3D12GraphicsCommandList> mCopyCommandList{};
            ComPtr<ID3D12Fence> mCopyFence{};
            ComPtr<ID3D12Resource> mUploadHeapResource{};
            std::byte* mUploadHeapMappedData{};
            UINT64 mUploadBufferSize{};

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
