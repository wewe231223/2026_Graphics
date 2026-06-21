#include "ComputeQueue.h"
#include "Utility/ErrorHandler.h"
#include "Utility/StdOutput.h"

using namespace Core::DX;

ComputeQueue::ComputeQueue(ID3D12Device* Device)
    : mComputeCommandQueue{},
    mComputeCommandAllocators{},
    mComputeCommandList{},
    mComputeFence{},
    mCurrentAllocatorFlightIndex{ 0 },
    mAllocatorFlightFenceValues{},
    mFenceEvent{},
    mQueueMutex{},
    mFenceMutex{},
    mQueueCondition{},
    mFenceCondition{},
    mPendingRequestBatches{},
    mDispatchRequested{ false },
    mWorkerThread{},
    mIsRunning{ true },
    mSubmitFenceValueCounter{ 0 },
    mComputeTicketCounter{ 0 },
    mComputeTicketFenceStates{} {
    bool InitializeResult{ Initialize(Device) };
    ErrorHandler::report(InitializeResult == false, "ComputeQueue", "Failed to initialize compute queue.", ErrorHandler::Level::Critical);
}

ComputeQueue::~ComputeQueue() {
    StopWorker();

    if (mFenceEvent != nullptr) {
        CloseHandle(mFenceEvent);
        mFenceEvent = nullptr;
    }
}

bool ComputeQueue::Initialize(ID3D12Device* Device) {
    if (Device == nullptr) {
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC QueueDescription{};
    QueueDescription.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    QueueDescription.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    QueueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    QueueDescription.NodeMask = 0;
    ErrorHandler::report(Device->CreateCommandQueue(&QueueDescription, IID_PPV_ARGS(mComputeCommandQueue.GetAddressOf())), "ComputeQueue", "Failed to create compute command queue.", ErrorHandler::Level::Critical);

    for (Microsoft::WRL::ComPtr<ID3D12CommandAllocator>& ComputeCommandAllocator : mComputeCommandAllocators) {
        ErrorHandler::report(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(ComputeCommandAllocator.GetAddressOf())), "ComputeQueue", "Failed to create compute command allocator.", ErrorHandler::Level::Critical);
    }

    ErrorHandler::report(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, mComputeCommandAllocators[mCurrentAllocatorFlightIndex].Get(), nullptr, IID_PPV_ARGS(mComputeCommandList.GetAddressOf())), "ComputeQueue", "Failed to create compute command list.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mComputeCommandList->Close(), "ComputeQueue", "Failed to close initial compute command list.", ErrorHandler::Level::Critical);
    ErrorHandler::report(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(mComputeFence.GetAddressOf())), "ComputeQueue", "Failed to create compute queue fence.", ErrorHandler::Level::Critical);

    mFenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    ErrorHandler::report(mFenceEvent == nullptr, "ComputeQueue", "Failed to create compute queue fence event.", ErrorHandler::Level::Critical);

    mWorkerThread = std::thread(&ComputeQueue::WorkerLoop, this);
    return true;
}

RenderContract::Future ComputeQueue::EnqueueComputeFuture(const Interface::ComputeQueueDispatchRequest& DispatchRequest) {
    std::span<const Interface::ComputeQueueDispatchRequest> DispatchRequests{ &DispatchRequest, 1 };
    return EnqueueComputeFuture(DispatchRequests);
}

RenderContract::Future ComputeQueue::EnqueueComputeFuture(std::span<const Interface::ComputeQueueDispatchRequest> DispatchRequests) {
    std::vector<Interface::ComputeQueueDispatchRequest> PreparedRequests{};
    PreparedRequests.reserve(DispatchRequests.size());

    for (const Interface::ComputeQueueDispatchRequest& DispatchRequest : DispatchRequests) {
        PreparedRequests.push_back(DispatchRequest);
    }

    std::uint64_t ComputeTicket{ GenerateComputeTicket() };
    bool IsEnqueued{ EnqueuePreparedComputeRequests(ComputeTicket, PreparedRequests) };
    if (IsEnqueued == false) {
        return RenderContract::Future{};
    }

    return RenderContract::Future{ this, ComputeTicket };
}

void ComputeQueue::DispatchComputes() {
    RequestDispatch();
}

bool ComputeQueue::IsFutureComplete(std::uint64_t ComputeTicket) const {
    if (ComputeTicket == 0) {
        return true;
    }

    std::uint64_t SubmitFenceValue{};
    {
        std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
        std::unordered_map<std::uint64_t, ComputeTicketFenceState>::const_iterator FoundFence{ mComputeTicketFenceStates.find(ComputeTicket) };
        if (FoundFence == mComputeTicketFenceStates.end()) {
            return true;
        }

        if (FoundFence->second.PendingBatchCount > 0) {
            return false;
        }

        SubmitFenceValue = FoundFence->second.LastSubmitFenceValue;
    }

    return IsSubmitFenceComplete(SubmitFenceValue);
}

void ComputeQueue::WaitFuture(std::uint64_t ComputeTicket) const {
    RequestDispatch();
    std::uint64_t SubmitFenceValue{ ResolveComputeTicketToFenceValue(ComputeTicket) };
    WaitForSubmitFence(SubmitFenceValue);
}

void ComputeQueue::QueueWaitFuture(ID3D12CommandQueue* WaitingQueue, std::uint64_t ComputeTicket) const {
    if (WaitingQueue == nullptr) {
        return;
    }

    RequestDispatch();
    std::uint64_t SubmitFenceValue{ ResolveComputeTicketToFenceValue(ComputeTicket) };
    if (SubmitFenceValue == 0) {
        return;
    }

    ErrorHandler::report(WaitingQueue->Wait(mComputeFence.Get(), SubmitFenceValue), "ComputeQueue", "Failed to queue wait for compute queue fence.", ErrorHandler::Level::Critical);
}

void ComputeQueue::Flush() {
    DispatchComputes();
    WaitForQueueIdle();
}

ID3D12CommandQueue* ComputeQueue::GetCommandQueue() const {
    return mComputeCommandQueue.Get();
}

void ComputeQueue::WorkerLoop() {
    while (mIsRunning.load() == true) {
        ComputeRequestBatch RequestBatch{};

        {
            std::unique_lock<std::mutex> QueueLock{ mQueueMutex };
            mQueueCondition.wait(QueueLock, [this] { return mIsRunning.load() == false or (mDispatchRequested == true and mPendingRequestBatches.empty() == false); });

            if (mIsRunning.load() == false and mPendingRequestBatches.empty() == true) {
                return;
            }

            RequestBatch = std::move(mPendingRequestBatches.front());
            mPendingRequestBatches.pop();

            if (mPendingRequestBatches.empty() == true) {
                mDispatchRequested = false;
            }
        }

        ExecuteRequestBatch(RequestBatch);
    }
}

void ComputeQueue::ExecuteRequestBatch(ComputeRequestBatch& RequestBatch) {
    if (RequestBatch.DispatchRequests.empty() == true) {
        CompleteComputeTicket(RequestBatch.ComputeTicket, mSubmitFenceValueCounter.load());
        return;
    }

    for (const Interface::ComputeQueueDispatchRequest& DispatchRequest : RequestBatch.DispatchRequests) {
        if (DispatchRequest.WaitFuture.IsValid() == true) {
            DispatchRequest.WaitFuture.QueueWait(mComputeCommandQueue.Get());
        }
    }

    std::uint32_t AllocatorFlightIndex{ mCurrentAllocatorFlightIndex };
    std::uint64_t AllocatorFenceValue{ mAllocatorFlightFenceValues[AllocatorFlightIndex] };
    if (AllocatorFenceValue > 0 and IsSubmitFenceComplete(AllocatorFenceValue) == false) {
        WaitForSubmitFence(AllocatorFenceValue);
    }

    ErrorHandler::report(mComputeCommandAllocators[AllocatorFlightIndex]->Reset(), "ComputeQueue", "Failed to reset compute command allocator.", ErrorHandler::Level::Critical);
    ErrorHandler::report(mComputeCommandList->Reset(mComputeCommandAllocators[AllocatorFlightIndex].Get(), nullptr), "ComputeQueue", "Failed to reset compute command list.", ErrorHandler::Level::Critical);

    for (const Interface::ComputeQueueDispatchRequest& DispatchRequest : RequestBatch.DispatchRequests) {
        RecordDispatchRequest(DispatchRequest);
    }

    ErrorHandler::report(mComputeCommandList->Close(), "ComputeQueue", "Failed to close compute command list.", ErrorHandler::Level::Critical);

    ID3D12CommandList* CommandLists[]{ mComputeCommandList.Get() };
    mComputeCommandQueue->ExecuteCommandLists(1, CommandLists);

    std::uint64_t SubmitFenceValue{ mSubmitFenceValueCounter.fetch_add(1) + 1 };
    ErrorHandler::report(mComputeCommandQueue->Signal(mComputeFence.Get(), SubmitFenceValue), "ComputeQueue", "Failed to signal compute queue fence.", ErrorHandler::Level::Critical);
    mAllocatorFlightFenceValues[AllocatorFlightIndex] = SubmitFenceValue;
    mCurrentAllocatorFlightIndex = (mCurrentAllocatorFlightIndex + 1) % ComputeAllocatorFlightCount;

    CompleteComputeTicket(RequestBatch.ComputeTicket, SubmitFenceValue);
}

void ComputeQueue::RecordDispatchRequest(const Interface::ComputeQueueDispatchRequest& DispatchRequest) {
    if (DispatchRequest.DescriptorHeaps.empty() == false) {
        mComputeCommandList->SetDescriptorHeaps(static_cast<UINT>(DispatchRequest.DescriptorHeaps.size()), DispatchRequest.DescriptorHeaps.data());
    }

    if (DispatchRequest.RootSignature != nullptr) {
        mComputeCommandList->SetComputeRootSignature(DispatchRequest.RootSignature.Get());
    }

    if (DispatchRequest.PipelineState != nullptr) {
        mComputeCommandList->SetPipelineState(DispatchRequest.PipelineState.Get());
    }

    if (DispatchRequest.RecordCommands) {
        DispatchRequest.RecordCommands(mComputeCommandList.Get());
    }

    if (DispatchRequest.ThreadGroupCountX > 0) {
        mComputeCommandList->Dispatch(DispatchRequest.ThreadGroupCountX, DispatchRequest.ThreadGroupCountY, DispatchRequest.ThreadGroupCountZ);
    }
}

void ComputeQueue::StopWorker() {
    if (mIsRunning.load() == true) {
        Flush();
        mIsRunning.store(false);
        mQueueCondition.notify_all();
        if (mWorkerThread.joinable() == true) {
            mWorkerThread.join();
        }
    }
}

void ComputeQueue::WaitForQueueIdle() {
    std::uint64_t IdleComputeTicket{ GenerateComputeTicket() };
    std::vector<Interface::ComputeQueueDispatchRequest> DispatchRequests{};
    bool EnqueueResult{ EnqueuePreparedComputeRequests(IdleComputeTicket, DispatchRequests) };
    ErrorHandler::report(EnqueueResult == false, "ComputeQueue", "Failed to enqueue idle marker.", ErrorHandler::Level::Critical);
    DispatchComputes();
    WaitFuture(IdleComputeTicket);
}

void ComputeQueue::RequestDispatch() const {
    {
        std::lock_guard<std::mutex> QueueGuard{ mQueueMutex };
        mDispatchRequested = true;
    }

    mQueueCondition.notify_one();
}

bool ComputeQueue::IsSubmitFenceComplete(std::uint64_t FenceValue) const {
    return mComputeFence->GetCompletedValue() >= FenceValue;
}

void ComputeQueue::WaitForSubmitFence(std::uint64_t FenceValue) const {
    if (IsSubmitFenceComplete(FenceValue) == true) {
        return;
    }

    StdOutput::PrintErrorLine("Waiting for compute queue fence value {} to be completed.", FenceValue);

    std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };

    ErrorHandler::report(mComputeFence->SetEventOnCompletion(FenceValue, mFenceEvent), "ComputeQueue", "Failed to set compute queue fence completion event.", ErrorHandler::Level::Critical);
    WaitForSingleObjectEx(mFenceEvent, INFINITE, FALSE);
}

bool ComputeQueue::EnqueuePreparedComputeRequests(std::uint64_t ComputeTicket, std::vector<Interface::ComputeQueueDispatchRequest>& DispatchRequests) {
    ComputeRequestBatch RequestBatch{};
    RequestBatch.ComputeTicket = ComputeTicket;
    RequestBatch.DispatchRequests = std::move(DispatchRequests);

    {
        std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
        ComputeTicketFenceState& FenceState{ mComputeTicketFenceStates[ComputeTicket] };
        FenceState.PendingBatchCount += 1;
    }

    {
        std::lock_guard<std::mutex> QueueGuard{ mQueueMutex };
        mPendingRequestBatches.push(std::move(RequestBatch));
    }

    return true;
}

std::uint64_t ComputeQueue::ResolveComputeTicketToFenceValue(std::uint64_t ComputeTicket) const {
    if (ComputeTicket == 0) {
        return 0;
    }

    std::unique_lock<std::mutex> FenceGuard{ mFenceMutex };
    std::unordered_map<std::uint64_t, ComputeTicketFenceState>::const_iterator FoundFence{ mComputeTicketFenceStates.find(ComputeTicket) };
    if (FoundFence == mComputeTicketFenceStates.end()) {
        return 0;
    }

    mFenceCondition.wait(FenceGuard, [this, ComputeTicket] {
        std::unordered_map<std::uint64_t, ComputeTicketFenceState>::const_iterator CurrentFence{ mComputeTicketFenceStates.find(ComputeTicket) };
        if (CurrentFence == mComputeTicketFenceStates.end()) {
            return true;
        }

        return CurrentFence->second.PendingBatchCount == 0;
    });

    FoundFence = mComputeTicketFenceStates.find(ComputeTicket);
    if (FoundFence == mComputeTicketFenceStates.end()) {
        return 0;
    }

    return FoundFence->second.LastSubmitFenceValue;
}

std::uint64_t ComputeQueue::GenerateComputeTicket() {
    return mComputeTicketCounter.fetch_add(1) + 1;
}

void ComputeQueue::CompleteComputeTicket(std::uint64_t ComputeTicket, std::uint64_t SubmitFenceValue) {
    {
        std::lock_guard<std::mutex> FenceGuard{ mFenceMutex };
        ComputeTicketFenceState& FenceState{ mComputeTicketFenceStates[ComputeTicket] };
        if (FenceState.LastSubmitFenceValue < SubmitFenceValue) {
            FenceState.LastSubmitFenceValue = SubmitFenceValue;
        }

        if (FenceState.PendingBatchCount > 0) {
            FenceState.PendingBatchCount -= 1;
        }
    }

    mFenceCondition.notify_all();
}
