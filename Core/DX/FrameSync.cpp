#include "FrameSync.h"
#include <Windows.h>
#include <stdexcept>
#include "Utility/ErrorHandler.h"

using namespace Core::DX;

FrameSync::FrameSync(ID3D12Device* device) {
    mFenceValues.fill(0);

    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)))) {
		ErrorHandler::report("FrameSync", "Failed to create Fence", ErrorHandler::Level::Critical);
    }

    mSyncEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    if (mSyncEvent == nullptr) {
		ErrorHandler::report("FrameSync", "Failed to create Fence Event", ErrorHandler::Level::Critical);
    }
}

FrameSync::~FrameSync() {
    if (mSyncEvent != nullptr) {
        CloseHandle(mSyncEvent);
    }
}

FrameSync::FrameSync(FrameSync&& other) noexcept {
	if (this != &other) {
		mFence = std::move(other.mFence);
		mSyncEvent = other.mSyncEvent;
		other.mSyncEvent = nullptr;
		mFenceValues = other.mFenceValues;
		mFrameIndex = other.mFrameIndex;
		mValueCounter = other.mValueCounter;
	}

    other.mSyncEvent = nullptr; 
}

FrameSync& FrameSync::operator=(FrameSync&& other) noexcept {
	if (this != &other) {
		mFence = std::move(other.mFence);
		mSyncEvent = other.mSyncEvent;
		other.mSyncEvent = nullptr;
		mFenceValues = other.mFenceValues;
		mFrameIndex = other.mFrameIndex;
		mValueCounter = other.mValueCounter;
	}
	other.mSyncEvent = nullptr;
	return *this;
}

void FrameSync::Sync(ID3D12CommandQueue* commandQueue) {
    const uint64_t signalValue = ++mValueCounter;
    commandQueue->Signal(mFence.Get(), signalValue);
    mFenceValues[mFrameIndex] = signalValue;

	mFrameIndex = (mFrameIndex + 1) % Constants::FrameCount<size_t>;

    const uint64_t waitValue = mFenceValues[mFrameIndex];
    if (mFence->GetCompletedValue() < waitValue) {
        mFence->SetEventOnCompletion(waitValue, mSyncEvent);
        WaitForSingleObjectEx(mSyncEvent, INFINITE, FALSE);
    }
}

void FrameSync::Flush(ID3D12CommandQueue* commandQueue) {
    const uint64_t signalValue = ++mValueCounter;
    commandQueue->Signal(mFence.Get(), signalValue);

    if (mFence->GetCompletedValue() < signalValue) {
        mFence->SetEventOnCompletion(signalValue, mSyncEvent);
        WaitForSingleObjectEx(mSyncEvent, INFINITE, FALSE);
    }

    mFenceValues.fill(signalValue);
}

uint32_t Core::DX::FrameSync::GetCurrentIndex() const {
    return mFrameIndex;
}
