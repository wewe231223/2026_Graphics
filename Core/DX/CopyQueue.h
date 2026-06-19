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
            Interface::Future EnqueueCopyFuture(const Interface::CopyRequest& CopyRequest) override;
            Interface::Future EnqueueCopyFuture(std::span<const Interface::CopyRequest> CopyRequests) override;
            Interface::Future EnqueueTextureCopyFuture(const Interface::CopyQueueTextureCopyRequest& CopyRequest) override;
            Interface::Future EnqueueTextureCopyFuture(std::span<const Interface::CopyQueueTextureCopyRequest> CopyRequests) override;

            void DispatchCopies() override;
            bool IsFutureComplete(std::uint64_t CopyTicket) const override;
            void WaitFuture(std::uint64_t CopyTicket) const override;
            void QueueWaitFuture(ID3D12CommandQueue* WaitingQueue, std::uint64_t CopyTicket) const override;
            void QueueWaitFutures(ID3D12CommandQueue* WaitingQueue, std::span<const std::uint64_t> CopyTickets) const override;
            void Flush() override;

            std::uint64_t GetRequiredUploadBufferSize() const override;

        private:
            struct UploadAllocation final {
                Microsoft::WRL::ComPtr<ID3D12Resource> Resource{};
                std::byte* CpuAddress{};
                std::uint64_t Offset{};
                std::uint64_t Size{};
                std::size_t PageIndex{};
            };

            struct UploadPage final {
                Microsoft::WRL::ComPtr<ID3D12Resource> Resource{};
                std::byte* MappedAddress{};
                std::uint64_t Capacity{};
                std::uint64_t UsedSize{};
                std::uint32_t PendingUsageCount{};
                bool IsDedicated{};
            };

            struct PreparedCopyRequest final {
                bool IsTextureCopy{};
                Interface::CopyPriority Priority{ Interface::CopyPriority::Invalid };
                Microsoft::WRL::ComPtr<ID3D12Resource> DestinationDefaultResource{};
                Microsoft::WRL::ComPtr<ID3D12Resource> UploadResource{};
                std::uint64_t DestinationOffset{};
                std::uint64_t SourceOffset{};
                std::uint64_t CopySize{};
                std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> Layouts{};
            };

            struct CopyRequestBatch final {
                Interface::CopyPriority Priority{ Interface::CopyPriority::Invalid };
                std::vector<PreparedCopyRequest> CopyRequests{};
                std::vector<std::size_t> UploadPageIndices{};
                std::uint64_t CopySizeInBytes{};
                std::uint64_t CopyTicket{};
            };

            struct RetiredUploadPageUsage final {
                std::uint64_t SubmitFenceValue{};
                std::vector<std::size_t> UploadPageIndices{};
            };

            struct CopyTicketFenceState final {
                std::uint64_t LastSubmitFenceValue{};
                std::uint64_t PendingBatchCount{};
            };

        private:
            void WorkerLoop();
            void ExecuteRequestBatches(std::vector<CopyRequestBatch>& RequestBatches);
            void StopWorker();
            void WaitForQueueIdle();
            bool IsSubmitFenceComplete(std::uint64_t FenceValue) const;
            void WaitForSubmitFence(std::uint64_t FenceValue) const;
            bool EnqueuePreparedCopyRequests(std::uint64_t CopyTicket, Interface::CopyPriority Priority, std::vector<PreparedCopyRequest>& PreparedRequests, std::vector<std::size_t>& UploadPageIndices);
            std::uint64_t ResolveCopyTicketToFenceValue(std::uint64_t CopyTicket) const;
            std::uint64_t ResolveCopyTicketsToFenceValue(std::span<const std::uint64_t> CopyTickets) const;
            std::uint64_t GenerateCopyTicket();
            bool PrepareCopyRequests(std::span<const Interface::CopyRequest> CopyRequests, std::vector<PreparedCopyRequest>& OutPreparedRequests, std::vector<std::size_t>& OutUploadPageIndices, Interface::CopyPriority& OutPriority);
            bool PrepareTextureCopyRequests(std::span<const Interface::CopyQueueTextureCopyRequest> CopyRequests, std::vector<PreparedCopyRequest>& OutPreparedRequests, std::vector<std::size_t>& OutUploadPageIndices, Interface::CopyPriority& OutPriority);
            bool HasPendingRequestBatchesLocked() const;
            std::size_t GetPendingRequestBatchCountLocked() const;
            Interface::CopyPriority SelectSubmitPriorityLocked() const;
            bool AllocateUploadMemory(std::uint64_t Size, std::uint64_t Alignment, std::vector<std::size_t>& UploadPageIndices, UploadAllocation& OutAllocation);
            bool TryAllocateUploadMemoryLocked(std::uint64_t Size, std::uint64_t Alignment, bool IsDedicated, std::vector<std::size_t>& UploadPageIndices, UploadAllocation& OutAllocation);
            bool TryAllocateFromUploadPageLocked(std::size_t PageIndex, std::uint64_t Size, std::uint64_t Alignment, std::vector<std::size_t>& UploadPageIndices, UploadAllocation& OutAllocation);
            bool CreateUploadPageLocked(std::uint64_t Capacity, bool IsDedicated, std::size_t& OutPageIndex);
            void RegisterUploadPageUsageLocked(std::size_t PageIndex, std::vector<std::size_t>& UploadPageIndices);
            void ReleaseUploadPageUsages(std::span<const std::size_t> UploadPageIndices);
            void CollectCompletedUploads();
            void CollectCompletedUploadsLocked();
            void ReleaseUnusedDedicatedUploadPagesLocked();
            void ResetUploadPage(UploadPage& Page);
            void ResetUploadPages();
            static std::uint64_t AlignUp(std::uint64_t Value, std::uint64_t Alignment);
            static bool IsValidCopyPriority(Interface::CopyPriority Priority);
            static bool IsHigherCopyPriority(Interface::CopyPriority Left, Interface::CopyPriority Right);
            static std::size_t ResolveCopyPriorityIndex(Interface::CopyPriority Priority);
          
        private:
            static constexpr std::uint64_t UploadPageSizeInBytes{ 64ull * 1024ull * 1024ull };
            static constexpr std::uint64_t LargeUploadThresholdInBytes{ UploadPageSizeInBytes / 2ull };
            static constexpr std::uint64_t MaxUploadMemorySizeInBytes{ 1ull * 1024ull * 1024ull * 1024ull };
            static constexpr std::uint64_t BufferUploadAlignmentInBytes{ 256ull };
            static constexpr std::uint64_t TextureUploadAlignmentInBytes{ D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT };
            static constexpr std::uint64_t AutoSubmitByteThresholdInBytes{ 4ull * 1024ull * 1024ull };
            static constexpr std::size_t AutoSubmitBatchThreshold{ 16ull };
            static constexpr std::uint64_t AutoSubmitCoalesceTimeoutMicroseconds{ 250ull };
            static constexpr std::size_t CopyPriorityLaneCount{ static_cast<std::size_t>(Interface::CopyPriority::Count) - 1ull };
            static constexpr std::size_t InvalidUploadPageIndex{ static_cast<std::size_t>(-1) };

            Microsoft::WRL::ComPtr<ID3D12Device> mDevice{};
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCopyCommandQueue{};
            static constexpr std::uint32_t CopyAllocatorFlightCount{ 3 };

            std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, CopyAllocatorFlightCount> mCopyCommandAllocators{};
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCopyCommandList{};
            Microsoft::WRL::ComPtr<ID3D12Fence> mCopyFence{};

            std::uint32_t mCurrentAllocatorFlightIndex{};
            std::array<std::uint64_t, CopyAllocatorFlightCount> mAllocatorFlightFenceValues{};

            HANDLE mFenceEvent{};

            mutable std::mutex mQueueMutex{};
            mutable std::mutex mFenceMutex{};
            mutable std::mutex mUploadPageMutex{};
            std::condition_variable mQueueCondition{};
            mutable std::condition_variable mFenceCondition{};
            std::array<std::queue<CopyRequestBatch>, CopyPriorityLaneCount> mPendingRequestBatchQueues{};
            std::uint64_t mPendingCopySizeInBytes{};
            std::vector<UploadPage> mUploadPages{};
            std::deque<RetiredUploadPageUsage> mRetiredUploadPageUsages{};
            std::size_t mCurrentUploadPageIndex{};
            std::uint64_t mUploadPageCapacityInBytes{};
            bool mDispatchRequested{};

            std::thread mWorkerThread{};
            std::atomic_bool mIsRunning{};
            std::atomic<std::uint64_t> mSubmitFenceValueCounter{};
            std::atomic<std::uint64_t> mCopyTicketCounter{};
            std::unordered_map<std::uint64_t, CopyTicketFenceState> mCopyTicketFenceStates{};
        };
    }
}
