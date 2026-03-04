#pragma once
#define WIN32_LEAN_AND_MEAN
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <limits>
#include <unordered_map>
#include <mutex>
#include <queue>
#include <span>
#include <thread>
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
            uint64_t EnqueueCopy(const Interface::CopyRequest& CopyRequest) override;
            uint64_t EnqueueCopy(std::span<const Interface::CopyRequest> CopyRequests) override;
            uint64_t EnqueueCopy(const Interface::CopyRequest& CopyRequest, std::int32_t Tag) override;
            uint64_t EnqueueCopy(std::span<const Interface::CopyRequest> CopyRequests, std::int32_t Tag) override;

            void DispatchCopies() override;
            bool IsFenceComplete(uint64_t FenceValue) const override;
            void WaitForFence(uint64_t FenceValue) const override;
            bool IsTagComplete(std::int32_t Tag) const override;
            void WaitForTag(std::int32_t Tag) const override;
            void Flush() override;

            uint64_t GetRequiredUploadBufferSize() const override;
            uint64_t GetRequiredTextureUploadBufferSize() const override;

        private:
            struct UploadHeapSlot {
                ComPtr<ID3D12Resource> UploadHeapResource{};
                std::byte* UploadHeapMappedData{};
                UINT64 WriteOffset{};
                uint64_t SlotFenceValue{};
            };

            struct UploadHeapCollection {
                std::array<UploadHeapSlot, 2> Slots{};
                UINT64 BufferSize{};
                size_t CurrentSlotIndex{};
            };

            struct CopyRequestBatch {
                std::vector<Interface::CopyRequest> CopyRequests{};
                uint64_t RequestedFenceValue{};
            };

        private:
            void WorkerLoop();
            void ExecuteRequestBatch(const CopyRequestBatch& RequestBatch);
            bool ProcessBufferCopyRequest(const Interface::CopyRequest& CopyRequest, size_t RequestIndex, size_t& AllocatorIndex, std::vector<bool>& RequestTouchedMask, std::vector<uint64_t>& RequestCompletedSubmitFenceValues);
            bool ProcessTextureCopyRequest(const Interface::CopyRequest& CopyRequest, size_t RequestIndex, size_t& AllocatorIndex, std::vector<bool>& RequestTouchedMask, std::vector<uint64_t>& RequestCompletedSubmitFenceValues);
            bool WriteUploadBytes(UploadHeapCollection& Collection, ID3D12Resource* DestinationResource, uint64_t DestinationOffset, const std::byte* SourceData, uint64_t Size, size_t RequestIndex, size_t& AllocatorIndex, std::vector<bool>& RequestTouchedMask, std::vector<uint64_t>& RequestCompletedSubmitFenceValues);

            void StopWorker();
            void WaitForQueueIdle();

            bool IsSubmitFenceComplete(uint64_t FenceValue) const;
            void WaitForSubmitFence(uint64_t FenceValue) const;
            uint64_t ResolveRequestedFenceValue(uint64_t FenceValue) const;
            void PrepareAllocatorForRecording(size_t AllocatorIndex);
            uint64_t SubmitCurrentCommandList(size_t AllocatorIndex, std::vector<bool>& RequestTouchedMask, std::vector<uint64_t>& RequestCompletedSubmitFenceValues);
            bool TrySwitchUploadHeapSlot(UploadHeapCollection& Collection, size_t& AllocatorIndex, std::vector<bool>& RequestTouchedMask, std::vector<uint64_t>& RequestCompletedSubmitFenceValues);
            bool InitializeUploadHeapCollection(ID3D12Device* Device, UploadHeapCollection& Collection, UINT64 BufferSize);
            void ResetUploadHeapCollection(UploadHeapCollection& Collection);

        private:
            static constexpr size_t CopyAllocatorCount{ 3 };
            static constexpr UINT64 DefaultUploadBufferSize{ 32ull * 1024ull * 1024ull };
            static constexpr UINT64 DefaultTextureUploadBufferSize{ 64ull * 1024ull * 1024ull };

            ComPtr<ID3D12CommandQueue> mCopyCommandQueue{};
            std::array<ComPtr<ID3D12CommandAllocator>, CopyAllocatorCount> mCopyCommandAllocators{};
            ComPtr<ID3D12GraphicsCommandList> mCopyCommandList{};
            ComPtr<ID3D12Fence> mCopyFence{};
            UploadHeapCollection mBufferUploadHeapCollection{};
            UploadHeapCollection mTextureUploadHeapCollection{};

            HANDLE mFenceEvent{};

            mutable std::mutex mQueueMutex{};
            mutable std::mutex mFenceMutex{};
            std::condition_variable mQueueCondition{};
            mutable std::condition_variable mFenceCondition{};
            std::queue<CopyRequestBatch> mPendingRequestBatches{};
            bool mDispatchRequested{};

            std::thread mWorkerThread{};
            std::atomic_bool mIsRunning{};
            std::atomic<uint64_t> mRequestedFenceValueCounter{};
            std::atomic<uint64_t> mSubmitFenceValueCounter{};
            std::array<uint64_t, CopyAllocatorCount> mAllocatorFenceValues{};
            std::unordered_map<uint64_t, uint64_t> mRequestedFenceToCompletedSubmitFence{};
            std::unordered_map<std::int32_t, uint64_t> mTagToLatestRequestedFenceValue{};

            static constexpr uint64_t PendingSubmitFenceValue{ std::numeric_limits<uint64_t>::max() };
        };
    }
}
