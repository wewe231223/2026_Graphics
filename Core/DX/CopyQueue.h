#pragma once
#define WIN32_LEAN_AND_MEAN
#include <cstdint>
#include <mutex>
#include <thread>
#include <queue>
#include <atomic>
#include <condition_variable>
#include <Windows.h>
#include "Utility/DirectXInclude.h"

namespace Core {
    namespace DX {
        class CopyQueue {
        public:
            CopyQueue(ID3D12Device* device);
            ~CopyQueue();
            CopyQueue(const CopyQueue& other) = delete;
            CopyQueue& operator=(const CopyQueue& other) = delete;
            CopyQueue(CopyQueue&& other) = delete;
            CopyQueue& operator=(CopyQueue&& other) = delete;

        public:
            uint64_t EnqueueCopy(ID3D12Resource* destinationDefaultResource, UINT64 destinationOffset, ID3D12Resource* sourceUploadResource, UINT64 sourceOffset, UINT64 copySize);
            bool IsFenceComplete(uint64_t fenceValue) const;
            void WaitForFence(uint64_t fenceValue) const;
            void Flush();

        private:
            struct CopyRequest;
            void WorkerLoop();
            void ExecuteRequest(const CopyRequest& request);
            void StopWorker();
            void WaitForQueueIdle() const;

        private:
            struct CopyRequest {
                ID3D12Resource* DestinationDefaultResource;
                UINT64 DestinationOffset;
                ID3D12Resource* SourceUploadResource;
                UINT64 SourceOffset;
                UINT64 CopySize;
                uint64_t FenceValue;
            };

            ComPtr<ID3D12CommandQueue> mCopyCommandQueue{};
            ComPtr<ID3D12CommandAllocator> mCopyCommandAllocator{};
            ComPtr<ID3D12GraphicsCommandList> mCopyCommandList{};
            ComPtr<ID3D12Fence> mCopyFence{};

            HANDLE mFenceEvent{};

            mutable std::mutex mQueueMutex{};
            mutable std::mutex mFenceMutex{};
            std::condition_variable mQueueCondition{};
            std::queue<CopyRequest> mPendingRequests{};

            std::thread mWorkerThread{};
            std::atomic_bool mIsRunning{};
            std::atomic<uint64_t> mFenceValueCounter{};
        };
    }
}