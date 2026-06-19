#include "CopyQueue.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <vector>
#include "Utility/ErrorHandler.h"
#include "Utility/StdOutput.h"

using namespace Core::DX;

Interface::CopyRequest::CopyRequest(Interface::CopyPriority PriorityValue)
    : Priority{ PriorityValue },
    DestinationDefaultResource{},
    DestinationOffset{},
    SourceData{} {
}

Interface::CopyQueueTextureCopyRequest::CopyQueueTextureCopyRequest(Interface::CopyPriority PriorityValue)
    : Priority{ PriorityValue },
    DestinationTextureResource{},
    SourceLayouts{},
    SourceData{} {
}

CopyQueue::CopyQueue(ID3D12Device* Device)
    : mDevice{},
    mCopyCommandQueue{},
    mCopyCommandAllocators{},
    mCopyCommandList{},
    mCopyFence{},
    mCurrentAllocatorFlightIndex{ 0 },
    mAllocatorFlightFenceValues{},
    mFenceEvent{},
    mQueueMutex{},
    mFenceMutex{},
    mUploadPageMutex{},
    mQueueCondition{},
    mFenceCondition{},
    mPendingRequestBatchQueues{},
    mPendingCopySizeInBytes{},
    mUploadPages{},
    mRetiredUploadPageUsages{},
    mCurrentUploadPageIndex{ InvalidUploadPageIndex },
    mUploadPageCapacityInBytes{},
    mDispatchRequested{ false },
    mWorkerThread{},
    mIsRunning{ true },
    mSubmitFenceValueCounter{ 0 },
    mCopyTicketCounter{ 0 },
    mCopyTicketFenceStates{} {
    bool InitializeResult{ Initialize(Device) };
    ErrorHandler::report(InitializeResult == false, "CopyQueue", "Failed to initialize copy queue.", ErrorHandler::Level::Critical);
}

CopyQueue::~CopyQueue() {
    StopWorker();
    ResetUploadPages();

    if (mFenceEvent != nullptr) {
        CloseHandle(mFenceEvent);
        mFenceEvent = nullptr;
    }
}

bool CopyQueue::Initialize(ID3D12Device* Device) {
    if (Device == nullptr) {
        return false;
    }

    mDevice = Device;

    D3D12_COMMAND_QUEUE_DESC QueueDescription{};
    QueueDescription.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    QueueDescription.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    QueueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    QueueDescription.NodeMask = 0;
    ErrorHandler::report(Device->CreateCommandQueue(&QueueDescription, IID_PPV_ARGS(mCopyCommandQueue.GetAddressOf())), "CopyQueue", "Failed to create copy command queue.", ErrorHandler::Level::Critical);

    for (Microsoft::WRL::ComPtr<ID3D12CommandAllocator>& CopyCommandAllocator : mCopyCommandAllocators) {
        ErrorHandler::report(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(CopyCommandAllocator.GetAddressOf())), "CopyQueue", "Failed to create copy command allocator.", ErrorHandler::Level::Critical);
    }

    ErrorHandler::report(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, mCopyCommandAllocators[mCurrentAllocatorFlightIndex].Get(), nullptr, IID_PPV_ARGS(mCopyCommandList.GetAddressOf())), "CopyQueue", "Failed to create copy command list.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mCopyCommandList->Close(), "CopyQueue", "Failed to close initial copy command list.", ErrorHandler::Level::Critical);
    ErrorHandler::report(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(mCopyFence.GetAddressOf())), "CopyQueue", "Failed to create copy queue fence.", ErrorHandler::Level::Critical);

    mFenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    ErrorHandler::report(mFenceEvent == nullptr, "CopyQueue", "Failed to create copy queue fence event.", ErrorHandler::Level::Critical);

    mWorkerThread = std::thread(&CopyQueue::WorkerLoop, this);
    return true;
}


Interface::Future CopyQueue::EnqueueCopyFuture(const Interface::CopyRequest& CopyRequest) {
    std::span<const Interface::CopyRequest> CopyRequests{ &CopyRequest, 1 };
    return EnqueueCopyFuture(CopyRequests);
}

Interface::Future CopyQueue::EnqueueCopyFuture(std::span<const Interface::CopyRequest> CopyRequests) {
    std::vector<PreparedCopyRequest> PreparedRequests{};
    std::vector<std::size_t> UploadPageIndices{};
    Interface::CopyPriority Priority{ Interface::CopyPriority::Invalid };
    bool IsPrepared{ PrepareCopyRequests(CopyRequests, PreparedRequests, UploadPageIndices, Priority) };
    if (IsPrepared == false) {
        return Interface::Future{};
    }

    if (Priority == Interface::CopyPriority::Invalid) {
        Priority = Interface::CopyPriority::Normal;
    }

    std::uint64_t CopyTicket{ GenerateCopyTicket() };
    bool IsEnqueued{ EnqueuePreparedCopyRequests(CopyTicket, Priority, PreparedRequests, UploadPageIndices) };
    if (IsEnqueued == false) {
        ReleaseUploadPageUsages(UploadPageIndices);
        return Interface::Future{};
    }

    return Interface::Future{ this, CopyTicket };
}

Interface::Future CopyQueue::EnqueueTextureCopyFuture(const Interface::CopyQueueTextureCopyRequest& CopyRequest) {
    std::span<const Interface::CopyQueueTextureCopyRequest> CopyRequests{ &CopyRequest, 1 };
    return EnqueueTextureCopyFuture(CopyRequests);
}

Interface::Future CopyQueue::EnqueueTextureCopyFuture(std::span<const Interface::CopyQueueTextureCopyRequest> CopyRequests) {
    std::vector<PreparedCopyRequest> PreparedRequests{};
    std::vector<std::size_t> UploadPageIndices{};
    Interface::CopyPriority Priority{ Interface::CopyPriority::Invalid };
    bool IsPrepared{ PrepareTextureCopyRequests(CopyRequests, PreparedRequests, UploadPageIndices, Priority) };
    if (IsPrepared == false) {
        return Interface::Future{};
    }

    if (Priority == Interface::CopyPriority::Invalid) {
        Priority = Interface::CopyPriority::Normal;
    }

    std::uint64_t CopyTicket{ GenerateCopyTicket() };
    bool IsEnqueued{ EnqueuePreparedCopyRequests(CopyTicket, Priority, PreparedRequests, UploadPageIndices) };
    if (IsEnqueued == false) {
        ReleaseUploadPageUsages(UploadPageIndices);
        return Interface::Future{};
    }

    return Interface::Future{ this, CopyTicket };
}

void CopyQueue::DispatchCopies() {
    {
        std::lock_guard<std::mutex> QueueGuard{ mQueueMutex };
        mDispatchRequested = true;
    }

    mQueueCondition.notify_one();
}

bool CopyQueue::IsFutureComplete(std::uint64_t CopyTicket) const {
    std::uint64_t SubmitFenceValue{ ResolveCopyTicketToFenceValue(CopyTicket) };
    return IsSubmitFenceComplete(SubmitFenceValue);
}

void CopyQueue::WaitFuture(std::uint64_t CopyTicket) const {
    std::uint64_t SubmitFenceValue{ ResolveCopyTicketToFenceValue(CopyTicket) };
    WaitForSubmitFence(SubmitFenceValue);
}

void CopyQueue::QueueWaitFuture(ID3D12CommandQueue* WaitingQueue, std::uint64_t CopyTicket) const {
    std::array<std::uint64_t, 1> CopyTickets{ CopyTicket };
    QueueWaitFutures(WaitingQueue, CopyTickets);
}

void CopyQueue::QueueWaitFutures(ID3D12CommandQueue* WaitingQueue, std::span<const std::uint64_t> CopyTickets) const {
    if (WaitingQueue == nullptr or CopyTickets.empty() == true) {
        return;
    }

    std::uint64_t SubmitFenceValue{ ResolveCopyTicketsToFenceValue(CopyTickets) };
    if (SubmitFenceValue == 0) {
        return;
    }

    ErrorHandler::report(WaitingQueue->Wait(mCopyFence.Get(), SubmitFenceValue), "CopyQueue", "Failed to queue wait for copy queue fence.", ErrorHandler::Level::Critical);
}
void CopyQueue::Flush() {
    DispatchCopies();
    WaitForQueueIdle();
}

std::uint64_t CopyQueue::GetRequiredUploadBufferSize() const {
    return MaxUploadMemorySizeInBytes;
}

void CopyQueue::WorkerLoop() {
    while (mIsRunning.load() == true) {
        std::vector<CopyRequestBatch> RequestBatches{};

        {
            std::unique_lock<std::mutex> QueueLock{ mQueueMutex };
            mQueueCondition.wait(QueueLock, [this] { return mIsRunning.load() == false or mDispatchRequested == true or HasPendingRequestBatchesLocked() == true; });

            if (mIsRunning.load() == false and HasPendingRequestBatchesLocked() == false) {
                return;
            }

            if (HasPendingRequestBatchesLocked() == false) {
                mDispatchRequested = false;
                continue;
            }

            const bool HasHighPriorityRequestBatches{ mPendingRequestBatchQueues[ResolveCopyPriorityIndex(Interface::CopyPriority::High)].empty() == false };
            const bool ShouldCoalesce{ mDispatchRequested == false and HasHighPriorityRequestBatches == false and GetPendingRequestBatchCountLocked() < AutoSubmitBatchThreshold and mPendingCopySizeInBytes < AutoSubmitByteThresholdInBytes };
            if (ShouldCoalesce == true) {
                mQueueCondition.wait_for(QueueLock, std::chrono::microseconds{ AutoSubmitCoalesceTimeoutMicroseconds }, [this] { return mIsRunning.load() == false or mDispatchRequested == true or mPendingRequestBatchQueues[ResolveCopyPriorityIndex(Interface::CopyPriority::High)].empty() == false or GetPendingRequestBatchCountLocked() >= AutoSubmitBatchThreshold or mPendingCopySizeInBytes >= AutoSubmitByteThresholdInBytes; });
            }

            if (mIsRunning.load() == false and HasPendingRequestBatchesLocked() == false) {
                return;
            }

            Interface::CopyPriority SubmitPriority{ SelectSubmitPriorityLocked() };
            if (SubmitPriority == Interface::CopyPriority::Invalid) {
                mDispatchRequested = false;
                continue;
            }

            std::queue<CopyRequestBatch>& RequestBatchQueue{ mPendingRequestBatchQueues[ResolveCopyPriorityIndex(SubmitPriority)] };
            RequestBatches.reserve(RequestBatchQueue.size());
            while (RequestBatchQueue.empty() == false) {
                mPendingCopySizeInBytes -= RequestBatchQueue.front().CopySizeInBytes;
                RequestBatches.push_back(std::move(RequestBatchQueue.front()));
                RequestBatchQueue.pop();
            }

            if (HasPendingRequestBatchesLocked() == false) {
                mDispatchRequested = false;
            }
        }

        ExecuteRequestBatches(RequestBatches);
    }
}

void CopyQueue::ExecuteRequestBatches(std::vector<CopyRequestBatch>& RequestBatches) {
    CollectCompletedUploads();

    if (RequestBatches.empty() == true) {
        return;
    }

    bool HasCopyRequest{};
    for (const CopyRequestBatch& RequestBatch : RequestBatches) {
        if (RequestBatch.CopyRequests.empty() == false) {
            HasCopyRequest = true;
            break;
        }
    }

    if (HasCopyRequest == false) {
        const std::uint64_t SubmitFenceValue{ mSubmitFenceValueCounter.load() };
        {
            std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
            for (const CopyRequestBatch& RequestBatch : RequestBatches) {
                CopyTicketFenceState& FenceState{ mCopyTicketFenceStates[RequestBatch.CopyTicket] };
                FenceState.LastSubmitFenceValue = SubmitFenceValue;
                if (FenceState.PendingBatchCount > 0) {
                    FenceState.PendingBatchCount -= 1;
                }
            }
        }

        mFenceCondition.notify_all();
        return;
    }

    std::uint32_t AllocatorFlightIndex{ mCurrentAllocatorFlightIndex };
    std::uint64_t AllocatorFenceValue{ mAllocatorFlightFenceValues[AllocatorFlightIndex] };
    if (AllocatorFenceValue > 0 and IsSubmitFenceComplete(AllocatorFenceValue) == false) {
        WaitForSubmitFence(AllocatorFenceValue);
    }

    ErrorHandler::report(mCopyCommandAllocators[AllocatorFlightIndex]->Reset(), "CopyQueue", "Failed to reset copy command allocator.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mCopyCommandList->Reset(mCopyCommandAllocators[AllocatorFlightIndex].Get(), nullptr), "CopyQueue", "Failed to reset copy command list.", ErrorHandler::Level::Critical);

    for (CopyRequestBatch& RequestBatch : RequestBatches) {
        for (PreparedCopyRequest& PreparedCopyRequest : RequestBatch.CopyRequests) {
            ID3D12Resource* UploadResource{ PreparedCopyRequest.UploadResource.Get() };

            if (PreparedCopyRequest.IsTextureCopy == true) {
                UINT SubresourceCount{ static_cast<UINT>(PreparedCopyRequest.Layouts.size()) };
                for (UINT SubresourceIndex = 0; SubresourceIndex < SubresourceCount; ++SubresourceIndex) {
                    D3D12_TEXTURE_COPY_LOCATION DestinationLocation{};
                    DestinationLocation.pResource = PreparedCopyRequest.DestinationDefaultResource.Get();
                    DestinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    DestinationLocation.SubresourceIndex = SubresourceIndex;

                    D3D12_TEXTURE_COPY_LOCATION SourceLocation{};
                    SourceLocation.pResource = UploadResource;
                    SourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    SourceLocation.PlacedFootprint = PreparedCopyRequest.Layouts[SubresourceIndex];

                    mCopyCommandList->CopyTextureRegion(&DestinationLocation, 0, 0, 0, &SourceLocation, nullptr);
                }
            }
            else {
                mCopyCommandList->CopyBufferRegion(PreparedCopyRequest.DestinationDefaultResource.Get(), PreparedCopyRequest.DestinationOffset, UploadResource, PreparedCopyRequest.SourceOffset, PreparedCopyRequest.CopySize);
            }
        }
    }

    ErrorHandler::report(mCopyCommandList->Close(), "CopyQueue", "Failed to close copy command list.", ErrorHandler::Level::Critical);

    ID3D12CommandList* CommandLists[]{ mCopyCommandList.Get() };
    mCopyCommandQueue->ExecuteCommandLists(1, CommandLists);

    std::uint64_t SubmitFenceValue{ mSubmitFenceValueCounter.fetch_add(1) + 1 };
    ErrorHandler::report(mCopyCommandQueue->Signal(mCopyFence.Get(), SubmitFenceValue), "CopyQueue", "Failed to signal copy queue fence.", ErrorHandler::Level::Critical);
    mAllocatorFlightFenceValues[AllocatorFlightIndex] = SubmitFenceValue;
    mCurrentAllocatorFlightIndex = (mCurrentAllocatorFlightIndex + 1) % CopyAllocatorFlightCount;

    {
        std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
        for (const CopyRequestBatch& RequestBatch : RequestBatches) {
            CopyTicketFenceState& FenceState{ mCopyTicketFenceStates[RequestBatch.CopyTicket] };
            if (FenceState.LastSubmitFenceValue < SubmitFenceValue) {
                FenceState.LastSubmitFenceValue = SubmitFenceValue;
            }

            if (FenceState.PendingBatchCount > 0) {
                FenceState.PendingBatchCount -= 1;
            }
        }
    }

    RetiredUploadPageUsage RetiredUploadPageUsageValue{};
    RetiredUploadPageUsageValue.SubmitFenceValue = SubmitFenceValue;
    for (CopyRequestBatch& RequestBatch : RequestBatches) {
        RetiredUploadPageUsageValue.UploadPageIndices.insert(RetiredUploadPageUsageValue.UploadPageIndices.end(), RequestBatch.UploadPageIndices.begin(), RequestBatch.UploadPageIndices.end());
    }

    if (RetiredUploadPageUsageValue.UploadPageIndices.empty() == false) {
        std::lock_guard<std::mutex> UploadPageGuard{ mUploadPageMutex };
        mRetiredUploadPageUsages.push_back(std::move(RetiredUploadPageUsageValue));
    }

    mFenceCondition.notify_all();
    CollectCompletedUploads();
}

void CopyQueue::StopWorker() {
    if (mIsRunning.load() == true) {
        Flush();
        mIsRunning.store(false);
        mQueueCondition.notify_all();
        if (mWorkerThread.joinable() == true) {
            mWorkerThread.join();
        }
    }

    CollectCompletedUploads();
}

void CopyQueue::WaitForQueueIdle() {
    std::uint64_t IdleCopyTicket{ GenerateCopyTicket() };
    const std::array<Interface::CopyPriority, CopyPriorityLaneCount> Priorities{ Interface::CopyPriority::High, Interface::CopyPriority::Normal, Interface::CopyPriority::Background };
    for (Interface::CopyPriority Priority : Priorities) {
        std::vector<PreparedCopyRequest> PreparedRequests{};
        std::vector<std::size_t> UploadPageIndices{};
        bool EnqueueResult{ EnqueuePreparedCopyRequests(IdleCopyTicket, Priority, PreparedRequests, UploadPageIndices) };
        ErrorHandler::report(EnqueueResult == false, "CopyQueue", "Failed to enqueue idle marker.", ErrorHandler::Level::Critical);
    }

    DispatchCopies();
    WaitFuture(IdleCopyTicket);
}

bool CopyQueue::IsSubmitFenceComplete(std::uint64_t FenceValue) const {
    return mCopyFence->GetCompletedValue() >= FenceValue;
}

void CopyQueue::WaitForSubmitFence(std::uint64_t FenceValue) const {
    if (IsSubmitFenceComplete(FenceValue) == true) {
        return;
    }

	StdOutput::PrintErrorLine("Waiting for copy queue fence value {} to be completed.", FenceValue);

    std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };

    ErrorHandler::report(mCopyFence->SetEventOnCompletion(FenceValue, mFenceEvent), "CopyQueue", "Failed to set copy queue fence completion event.", ErrorHandler::Level::Critical);
    WaitForSingleObjectEx(mFenceEvent, INFINITE, FALSE);
}

std::uint64_t CopyQueue::GenerateCopyTicket() {
    return mCopyTicketCounter.fetch_add(1) + 1;
}

bool CopyQueue::EnqueuePreparedCopyRequests(std::uint64_t CopyTicket, Interface::CopyPriority Priority, std::vector<PreparedCopyRequest>& PreparedRequests, std::vector<std::size_t>& UploadPageIndices) {
    if (IsValidCopyPriority(Priority) == false) {
        return false;
    }

    CopyRequestBatch RequestBatch{};
    RequestBatch.Priority = Priority;
    RequestBatch.CopyTicket = CopyTicket;
    for (const PreparedCopyRequest& PreparedRequest : PreparedRequests) {
        RequestBatch.CopySizeInBytes += PreparedRequest.CopySize;
    }

    RequestBatch.CopyRequests = std::move(PreparedRequests);
    RequestBatch.UploadPageIndices = std::move(UploadPageIndices);

    {
        std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
        CopyTicketFenceState& FenceState{ mCopyTicketFenceStates[CopyTicket] };
        FenceState.PendingBatchCount += 1;
    }

    {
        std::lock_guard<std::mutex> QueueGuard{ mQueueMutex };
        mPendingCopySizeInBytes += RequestBatch.CopySizeInBytes;
        mPendingRequestBatchQueues[ResolveCopyPriorityIndex(Priority)].push(std::move(RequestBatch));
    }

    mQueueCondition.notify_one();
    return true;
}

std::uint64_t CopyQueue::ResolveCopyTicketToFenceValue(std::uint64_t CopyTicket) const {
    std::array<std::uint64_t, 1> CopyTickets{ CopyTicket };
    return ResolveCopyTicketsToFenceValue(CopyTickets);
}

std::uint64_t CopyQueue::ResolveCopyTicketsToFenceValue(std::span<const std::uint64_t> CopyTickets) const {
    std::unique_lock<std::mutex> FenceGuard{ mFenceMutex };
    mFenceCondition.wait(FenceGuard, [this, CopyTickets] {
        for (std::uint64_t CopyTicket : CopyTickets) {
            std::unordered_map<std::uint64_t, CopyTicketFenceState>::const_iterator CurrentFence{ mCopyTicketFenceStates.find(CopyTicket) };
            if (CurrentFence != mCopyTicketFenceStates.end() and CurrentFence->second.PendingBatchCount > 0) {
                return false;
            }
        }

        return true;
    });

    std::uint64_t SubmitFenceValue{};
    for (std::uint64_t CopyTicket : CopyTickets) {
        std::unordered_map<std::uint64_t, CopyTicketFenceState>::const_iterator FoundFence{ mCopyTicketFenceStates.find(CopyTicket) };
        if (FoundFence == mCopyTicketFenceStates.end()) {
            SubmitFenceValue = std::max(SubmitFenceValue, CopyTicket);
            continue;
        }

        SubmitFenceValue = std::max(SubmitFenceValue, FoundFence->second.LastSubmitFenceValue);
    }

    return SubmitFenceValue;
}
bool CopyQueue::HasPendingRequestBatchesLocked() const {
    for (const std::queue<CopyRequestBatch>& RequestBatchQueue : mPendingRequestBatchQueues) {
        if (RequestBatchQueue.empty() == false) {
            return true;
        }
    }

    return false;
}

std::size_t CopyQueue::GetPendingRequestBatchCountLocked() const {
    std::size_t PendingRequestBatchCount{};
    for (const std::queue<CopyRequestBatch>& RequestBatchQueue : mPendingRequestBatchQueues) {
        PendingRequestBatchCount += RequestBatchQueue.size();
    }

    return PendingRequestBatchCount;
}

Interface::CopyPriority CopyQueue::SelectSubmitPriorityLocked() const {
    for (std::size_t PriorityIndex{ 0 }; PriorityIndex < CopyPriorityLaneCount; ++PriorityIndex) {
        if (mPendingRequestBatchQueues[PriorityIndex].empty() == false) {
            return static_cast<Interface::CopyPriority>(PriorityIndex + 1ull);
        }
    }

    return Interface::CopyPriority::Invalid;
}

bool CopyQueue::PrepareCopyRequests(std::span<const Interface::CopyRequest> CopyRequests, std::vector<PreparedCopyRequest>& OutPreparedRequests, std::vector<std::size_t>& OutUploadPageIndices, Interface::CopyPriority& OutPriority) {
    std::vector<PreparedCopyRequest> PreparedRequests{};
    PreparedRequests.reserve(CopyRequests.size());
    std::vector<std::size_t> UploadPageIndices{};
    Interface::CopyPriority Priority{ Interface::CopyPriority::Invalid };

    for (const Interface::CopyRequest& CopyRequest : CopyRequests) {
        if (IsValidCopyPriority(CopyRequest.Priority) == false) {
            ReleaseUploadPageUsages(UploadPageIndices);
            return false;
        }

        if (Priority == Interface::CopyPriority::Invalid or IsHigherCopyPriority(CopyRequest.Priority, Priority) == true) {
            Priority = CopyRequest.Priority;
        }

        if (CopyRequest.SourceData.empty() == true) {
            continue;
        }

        if (CopyRequest.DestinationDefaultResource == nullptr) {
            ReleaseUploadPageUsages(UploadPageIndices);
            return false;
        }

        UploadAllocation NewUploadAllocation{};
        bool AllocationResult{ AllocateUploadMemory(static_cast<std::uint64_t>(CopyRequest.SourceData.size()), BufferUploadAlignmentInBytes, UploadPageIndices, NewUploadAllocation) };
        if (AllocationResult == false) {
            ReleaseUploadPageUsages(UploadPageIndices);
            return false;
        }

        std::memcpy(NewUploadAllocation.CpuAddress, CopyRequest.SourceData.data(), CopyRequest.SourceData.size());

        PreparedCopyRequest NewPreparedCopyRequest{};
        NewPreparedCopyRequest.IsTextureCopy = false;
        NewPreparedCopyRequest.Priority = CopyRequest.Priority;
        NewPreparedCopyRequest.DestinationDefaultResource = CopyRequest.DestinationDefaultResource;
        NewPreparedCopyRequest.UploadResource = NewUploadAllocation.Resource;
        NewPreparedCopyRequest.DestinationOffset = CopyRequest.DestinationOffset;
        NewPreparedCopyRequest.SourceOffset = NewUploadAllocation.Offset;
        NewPreparedCopyRequest.CopySize = static_cast<std::uint64_t>(CopyRequest.SourceData.size());
        PreparedRequests.push_back(std::move(NewPreparedCopyRequest));
    }

    OutPreparedRequests = std::move(PreparedRequests);
    OutUploadPageIndices = std::move(UploadPageIndices);
    OutPriority = Priority;
    return true;
}

bool CopyQueue::PrepareTextureCopyRequests(std::span<const Interface::CopyQueueTextureCopyRequest> CopyRequests, std::vector<PreparedCopyRequest>& OutPreparedRequests, std::vector<std::size_t>& OutUploadPageIndices, Interface::CopyPriority& OutPriority) {
    std::vector<PreparedCopyRequest> PreparedRequests{};
    PreparedRequests.reserve(CopyRequests.size());
    std::vector<std::size_t> UploadPageIndices{};
    Interface::CopyPriority Priority{ Interface::CopyPriority::Invalid };

    for (const Interface::CopyQueueTextureCopyRequest& CopyRequest : CopyRequests) {
        if (IsValidCopyPriority(CopyRequest.Priority) == false) {
            ReleaseUploadPageUsages(UploadPageIndices);
            return false;
        }

        if (Priority == Interface::CopyPriority::Invalid or IsHigherCopyPriority(CopyRequest.Priority, Priority) == true) {
            Priority = CopyRequest.Priority;
        }

        if (CopyRequest.DestinationTextureResource == nullptr) {
            ReleaseUploadPageUsages(UploadPageIndices);
            return false;
        }

        if (CopyRequest.SourceLayouts.empty() == true or CopyRequest.SourceData.empty() == true) {
            continue;
        }

        UploadAllocation NewUploadAllocation{};
        bool AllocationResult{ AllocateUploadMemory(static_cast<std::uint64_t>(CopyRequest.SourceData.size()), TextureUploadAlignmentInBytes, UploadPageIndices, NewUploadAllocation) };
        if (AllocationResult == false) {
            ReleaseUploadPageUsages(UploadPageIndices);
            return false;
        }

        std::memcpy(NewUploadAllocation.CpuAddress, CopyRequest.SourceData.data(), CopyRequest.SourceData.size());

        PreparedCopyRequest NewPreparedCopyRequest{};
        NewPreparedCopyRequest.IsTextureCopy = true;
        NewPreparedCopyRequest.Priority = CopyRequest.Priority;
        NewPreparedCopyRequest.DestinationDefaultResource = CopyRequest.DestinationTextureResource;
        NewPreparedCopyRequest.UploadResource = NewUploadAllocation.Resource;
        NewPreparedCopyRequest.SourceOffset = NewUploadAllocation.Offset;
        NewPreparedCopyRequest.CopySize = static_cast<std::uint64_t>(CopyRequest.SourceData.size());
        NewPreparedCopyRequest.Layouts = CopyRequest.SourceLayouts;
        for (D3D12_PLACED_SUBRESOURCE_FOOTPRINT& Layout : NewPreparedCopyRequest.Layouts) {
            Layout.Offset += NewUploadAllocation.Offset;
        }
        PreparedRequests.push_back(std::move(NewPreparedCopyRequest));
    }

    OutPreparedRequests = std::move(PreparedRequests);
    OutUploadPageIndices = std::move(UploadPageIndices);
    OutPriority = Priority;
    return true;
}

bool CopyQueue::AllocateUploadMemory(std::uint64_t Size, std::uint64_t Alignment, std::vector<std::size_t>& UploadPageIndices, UploadAllocation& OutAllocation) {
    if (Size == 0 or Size > MaxUploadMemorySizeInBytes) {
        return false;
    }

    const bool IsDedicated{ Size > LargeUploadThresholdInBytes };

    for (std::uint32_t AttemptIndex{ 0 }; AttemptIndex < 2; ++AttemptIndex) {
        CollectCompletedUploads();

        {
            std::lock_guard<std::mutex> UploadPageGuard{ mUploadPageMutex };
            bool AllocationResult{ TryAllocateUploadMemoryLocked(Size, Alignment, IsDedicated, UploadPageIndices, OutAllocation) };
            if (AllocationResult == true) {
                return true;
            }
        }

        Flush();
    }

    CollectCompletedUploads();

    {
        std::lock_guard<std::mutex> UploadPageGuard{ mUploadPageMutex };
        return TryAllocateUploadMemoryLocked(Size, Alignment, IsDedicated, UploadPageIndices, OutAllocation);
    }
}

bool CopyQueue::TryAllocateUploadMemoryLocked(std::uint64_t Size, std::uint64_t Alignment, bool IsDedicated, std::vector<std::size_t>& UploadPageIndices, UploadAllocation& OutAllocation) {
    if (IsDedicated == false and mCurrentUploadPageIndex != InvalidUploadPageIndex) {
        bool AllocationResult{ TryAllocateFromUploadPageLocked(mCurrentUploadPageIndex, Size, Alignment, UploadPageIndices, OutAllocation) };
        if (AllocationResult == true) {
            return true;
        }
    }

    for (std::size_t PageIndex{ 0 }; PageIndex < mUploadPages.size(); ++PageIndex) {
        UploadPage& Page{ mUploadPages[PageIndex] };
        if (Page.Resource == nullptr or Page.IsDedicated != IsDedicated) {
            continue;
        }

        bool AllocationResult{ TryAllocateFromUploadPageLocked(PageIndex, Size, Alignment, UploadPageIndices, OutAllocation) };
        if (AllocationResult == true) {
            if (IsDedicated == false) {
                mCurrentUploadPageIndex = PageIndex;
            }

            return true;
        }
    }

    ReleaseUnusedDedicatedUploadPagesLocked();

    std::uint64_t RequiredCapacity{ IsDedicated == true ? AlignUp(Size, UploadPageSizeInBytes) : UploadPageSizeInBytes };
    if (mUploadPageCapacityInBytes + RequiredCapacity > MaxUploadMemorySizeInBytes) {
        return false;
    }

    std::size_t NewPageIndex{ InvalidUploadPageIndex };
    bool CreateResult{ CreateUploadPageLocked(RequiredCapacity, IsDedicated, NewPageIndex) };
    if (CreateResult == false) {
        return false;
    }

    if (IsDedicated == false) {
        mCurrentUploadPageIndex = NewPageIndex;
    }

    return TryAllocateFromUploadPageLocked(NewPageIndex, Size, Alignment, UploadPageIndices, OutAllocation);
}

bool CopyQueue::TryAllocateFromUploadPageLocked(std::size_t PageIndex, std::uint64_t Size, std::uint64_t Alignment, std::vector<std::size_t>& UploadPageIndices, UploadAllocation& OutAllocation) {
    if (PageIndex >= mUploadPages.size()) {
        return false;
    }

    UploadPage& Page{ mUploadPages[PageIndex] };
    if (Page.Resource == nullptr or Page.MappedAddress == nullptr) {
        return false;
    }

    std::uint64_t AlignedOffset{ AlignUp(Page.UsedSize, Alignment) };
    if (AlignedOffset > Page.Capacity or Size > Page.Capacity - AlignedOffset) {
        return false;
    }

    OutAllocation.Resource = Page.Resource;
    OutAllocation.CpuAddress = Page.MappedAddress + AlignedOffset;
    OutAllocation.Offset = AlignedOffset;
    OutAllocation.Size = Size;
    OutAllocation.PageIndex = PageIndex;
    Page.UsedSize = AlignedOffset + Size;
    RegisterUploadPageUsageLocked(PageIndex, UploadPageIndices);
    return true;
}

bool CopyQueue::CreateUploadPageLocked(std::uint64_t Capacity, bool IsDedicated, std::size_t& OutPageIndex) {
    if (mDevice == nullptr or Capacity == 0) {
        return false;
    }

    D3D12_HEAP_PROPERTIES UploadHeapProperties{};
    UploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    UploadHeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    UploadHeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    UploadHeapProperties.CreationNodeMask = 1;
    UploadHeapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC ResourceDescription{};
    ResourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    ResourceDescription.Alignment = 0;
    ResourceDescription.Width = Capacity;
    ResourceDescription.Height = 1;
    ResourceDescription.DepthOrArraySize = 1;
    ResourceDescription.MipLevels = 1;
    ResourceDescription.Format = DXGI_FORMAT_UNKNOWN;
    ResourceDescription.SampleDesc.Count = 1;
    ResourceDescription.SampleDesc.Quality = 0;
    ResourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ResourceDescription.Flags = D3D12_RESOURCE_FLAG_NONE;

    UploadPage NewPage{};
    HRESULT CreateResult{ mDevice->CreateCommittedResource(&UploadHeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDescription, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(NewPage.Resource.GetAddressOf())) };
    if (FAILED(CreateResult)) {
        ErrorHandler::report(CreateResult, "CopyQueue", "Failed to create upload page.", ErrorHandler::Level::Critical);
        return false;
    }

    const wchar_t* ResourceName{ IsDedicated == true ? L"CopyQueue.DedicatedUploadPage" : L"CopyQueue.UploadPage" };
    NewPage.Resource->SetName(ResourceName);

    void* MappedAddress{};
    D3D12_RANGE ReadRange{};
    HRESULT MapResult{ NewPage.Resource->Map(0, &ReadRange, &MappedAddress) };
    if (FAILED(MapResult)) {
        ErrorHandler::report(MapResult, "CopyQueue", "Failed to map upload page.", ErrorHandler::Level::Critical);
        NewPage.Resource.Reset();
        return false;
    }

    NewPage.MappedAddress = static_cast<std::byte*>(MappedAddress);
    NewPage.Capacity = Capacity;
    NewPage.UsedSize = 0;
    NewPage.PendingUsageCount = 0;
    NewPage.IsDedicated = IsDedicated;

    for (std::size_t PageIndex{ 0 }; PageIndex < mUploadPages.size(); ++PageIndex) {
        if (mUploadPages[PageIndex].Resource != nullptr) {
            continue;
        }

        mUploadPages[PageIndex] = std::move(NewPage);
        mUploadPageCapacityInBytes += Capacity;
        OutPageIndex = PageIndex;
        return true;
    }

    mUploadPages.push_back(std::move(NewPage));
    mUploadPageCapacityInBytes += Capacity;
    OutPageIndex = mUploadPages.size() - 1;
    return true;
}

void CopyQueue::RegisterUploadPageUsageLocked(std::size_t PageIndex, std::vector<std::size_t>& UploadPageIndices) {
    std::vector<std::size_t>::iterator FoundPage{ std::find(UploadPageIndices.begin(), UploadPageIndices.end(), PageIndex) };
    if (FoundPage != UploadPageIndices.end()) {
        return;
    }

    if (PageIndex >= mUploadPages.size()) {
        return;
    }

    mUploadPages[PageIndex].PendingUsageCount += 1;
    UploadPageIndices.push_back(PageIndex);
}

void CopyQueue::ReleaseUploadPageUsages(std::span<const std::size_t> UploadPageIndices) {
    std::lock_guard<std::mutex> UploadPageGuard{ mUploadPageMutex };

    for (std::size_t PageIndex : UploadPageIndices) {
        if (PageIndex >= mUploadPages.size()) {
            continue;
        }

        UploadPage& Page{ mUploadPages[PageIndex] };
        if (Page.PendingUsageCount > 0) {
            Page.PendingUsageCount -= 1;
        }

        if (Page.PendingUsageCount == 0) {
            Page.UsedSize = 0;
        }
    }

    ReleaseUnusedDedicatedUploadPagesLocked();
}

void CopyQueue::CollectCompletedUploads() {
    std::lock_guard<std::mutex> UploadPageGuard{ mUploadPageMutex };
    CollectCompletedUploadsLocked();
}

void CopyQueue::CollectCompletedUploadsLocked() {
    while (mRetiredUploadPageUsages.empty() == false) {
        const RetiredUploadPageUsage& RetiredUsage{ mRetiredUploadPageUsages.front() };
        if (IsSubmitFenceComplete(RetiredUsage.SubmitFenceValue) == false) {
            break;
        }

        for (std::size_t PageIndex : RetiredUsage.UploadPageIndices) {
            if (PageIndex >= mUploadPages.size()) {
                continue;
            }

            UploadPage& Page{ mUploadPages[PageIndex] };
            if (Page.PendingUsageCount > 0) {
                Page.PendingUsageCount -= 1;
            }

            if (Page.PendingUsageCount == 0) {
                Page.UsedSize = 0;
            }
        }

        mRetiredUploadPageUsages.pop_front();
    }

    ReleaseUnusedDedicatedUploadPagesLocked();
}

void CopyQueue::ReleaseUnusedDedicatedUploadPagesLocked() {
    for (std::size_t PageIndex{ 0 }; PageIndex < mUploadPages.size(); ++PageIndex) {
        UploadPage& Page{ mUploadPages[PageIndex] };
        if (Page.Resource == nullptr or Page.IsDedicated == false or Page.PendingUsageCount > 0) {
            continue;
        }

        if (mCurrentUploadPageIndex == PageIndex) {
            mCurrentUploadPageIndex = InvalidUploadPageIndex;
        }

        mUploadPageCapacityInBytes -= Page.Capacity;
        ResetUploadPage(Page);
    }
}

void CopyQueue::ResetUploadPage(UploadPage& Page) {
    if (Page.Resource != nullptr and Page.MappedAddress != nullptr) {
        D3D12_RANGE WrittenRange{};
        Page.Resource->Unmap(0, &WrittenRange);
    }

    Page.Resource.Reset();
    Page.MappedAddress = nullptr;
    Page.Capacity = 0;
    Page.UsedSize = 0;
    Page.PendingUsageCount = 0;
    Page.IsDedicated = false;
}

void CopyQueue::ResetUploadPages() {
    std::lock_guard<std::mutex> UploadPageGuard{ mUploadPageMutex };

    for (UploadPage& Page : mUploadPages) {
        ResetUploadPage(Page);
    }

    mUploadPages.clear();
    mRetiredUploadPageUsages.clear();
    mCurrentUploadPageIndex = InvalidUploadPageIndex;
    mUploadPageCapacityInBytes = 0;
}

std::uint64_t CopyQueue::AlignUp(std::uint64_t Value, std::uint64_t Alignment) {
    if (Alignment == 0) {
        return Value;
    }

    return ((Value + Alignment - 1) / Alignment) * Alignment;
}

bool CopyQueue::IsValidCopyPriority(Interface::CopyPriority Priority) {
    return Priority > Interface::CopyPriority::Invalid and Priority < Interface::CopyPriority::Count;
}

bool CopyQueue::IsHigherCopyPriority(Interface::CopyPriority Left, Interface::CopyPriority Right) {
    return static_cast<std::uint8_t>(Left) < static_cast<std::uint8_t>(Right);
}

std::size_t CopyQueue::ResolveCopyPriorityIndex(Interface::CopyPriority Priority) {
    return static_cast<std::size_t>(Priority) - 1ull;
}
