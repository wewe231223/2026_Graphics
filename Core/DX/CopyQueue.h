#pragma once
#define WIN32_LEAN_AND_MEAN
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>
#include <Windows.h>
#include "Core/Common.h"
#include "Core/DX/GraphicsAllocator.h"
#include "Utility/DirectXInclude.h"

#ifdef max
#undef max
#endif

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
            bool EnqueueCopy(std::uint64_t CopyId, const Interface::CopyQueueCopyRequest& CopyRequest) override;
            bool EnqueueCopy(std::uint64_t CopyId, std::span<const Interface::CopyQueueCopyRequest> CopyRequests) override;
            bool EnqueueTextureCopy(std::uint64_t CopyId, const Interface::CopyQueueTextureCopyRequest& CopyRequest) override;
            bool EnqueueTextureCopy(std::uint64_t CopyId, std::span<const Interface::CopyQueueTextureCopyRequest> CopyRequests) override;

            void DispatchCopies() override;
            bool IsFenceComplete(std::uint64_t CopyId) const override;
            void GuaranteeCopy(std::uint64_t CopyId) const override;
            void Flush() override;
            void WaitForExternalFence(ID3D12Fence* Fence, std::uint64_t FenceValue) override;

            std::uint64_t GetRequiredUploadBufferSize() const override;

        private:
            struct UploadAllocation final {
                std::unique_ptr<Interface::IAllocationHandle> AllocationHandle{};
            };

            struct PreparedCopyRequest final {
                bool IsTextureCopy{};
                Microsoft::WRL::ComPtr<ID3D12Resource> DestinationDefaultResource{};
                std::uint64_t DestinationOffset{};
                std::uint64_t CopySize{};
                std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> Layouts{};
                UploadAllocation Upload{};
            };

            struct CopyRequestBatch final {
                std::vector<PreparedCopyRequest> CopyRequests{};
                std::uint64_t CopyId{};
            };

            struct InFlightUpload final {
                std::uint64_t SubmitFenceValue{};
                std::vector<UploadAllocation> Uploads{};
            };

            struct CopyIdFenceState final {
                std::uint64_t LastSubmitFenceValue{};
                std::uint64_t PendingBatchCount{};
            };

        private:
            void WorkerLoop();
            void ExecuteRequestBatch(CopyRequestBatch& RequestBatch);
            void StopWorker();
            void WaitForQueueIdle();
            bool IsSubmitFenceComplete(std::uint64_t FenceValue) const;
            void WaitForSubmitFence(std::uint64_t FenceValue) const;
            std::uint64_t ResolveCopyIdToFenceValue(std::uint64_t CopyId) const;
            bool PrepareCopyRequests(std::span<const Interface::CopyQueueCopyRequest> CopyRequests, std::vector<PreparedCopyRequest>& OutPreparedRequests);
            bool PrepareTextureCopyRequests(std::span<const Interface::CopyQueueTextureCopyRequest> CopyRequests, std::vector<PreparedCopyRequest>& OutPreparedRequests);
            void CollectCompletedUploads();

        private:
            static constexpr std::uint64_t UploadAllocatorHeapSize{ 256ull * 1024ull * 1024ull };

            Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCopyCommandQueue{};
            static constexpr std::uint32_t CopyAllocatorFlightCount{ 3 };

            std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, CopyAllocatorFlightCount> mCopyCommandAllocators{};
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCopyCommandList{};
            Microsoft::WRL::ComPtr<ID3D12Fence> mCopyFence{};
            GraphicsAllocator mUploadAllocator{};

            std::uint32_t mCurrentAllocatorFlightIndex{};
            std::array<std::uint64_t, CopyAllocatorFlightCount> mAllocatorFlightFenceValues{};

            HANDLE mFenceEvent{};

            mutable std::mutex mQueueMutex{};
            mutable std::mutex mFenceMutex{};
            mutable std::mutex mUploadAllocatorMutex{};
            std::condition_variable mQueueCondition{};
            mutable std::condition_variable mFenceCondition{};
            std::queue<CopyRequestBatch> mPendingRequestBatches{};
            std::deque<InFlightUpload> mInFlightUploads{};
            bool mDispatchRequested{};

            std::thread mWorkerThread{};
            std::atomic_bool mIsRunning{};
            std::atomic<std::uint64_t> mSubmitFenceValueCounter{};
            std::unordered_map<std::uint64_t, CopyIdFenceState> mCopyIdFenceStates{};
        };
    }
}
