#include "DesciptorHeap.h"

DescriptorHeap::DescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors, bool shaderVisible) :	mHandleIncreasement(0), 
																																		mCurrentIndex(0), 
																																		mCapacity(numDescriptors) {
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = type;
	desc.NumDescriptors = numDescriptors;
	desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mHeap));

	mCPUStart = mHeap->GetCPUDescriptorHandleForHeapStart();
	if (shaderVisible) mGPUStart = mHeap->GetGPUDescriptorHandleForHeapStart();
	mHandleIncreasement = device->GetDescriptorHandleIncrementSize(type);
}

DescriptorHeap::DescriptorHeap(DescriptorHeap&& other) noexcept {
	if (this != &other) {
		mHeap = std::move(other.mHeap);
		mCPUStart = other.mCPUStart;
		mGPUStart = other.mGPUStart;
		mHandleIncreasement = other.mHandleIncreasement;
		mCurrentIndex = other.mCurrentIndex;
		mCapacity = other.mCapacity;

		other.mCPUStart = { 0 };
		other.mGPUStart = { 0 };
		other.mHandleIncreasement = 0;
		other.mCurrentIndex = 0;
		other.mCapacity = 0;
	}
}

DescriptorHeap& DescriptorHeap::operator=(DescriptorHeap&& other) noexcept {
	if (this != &other) {
		mHeap = std::move(other.mHeap);
		mCPUStart = other.mCPUStart;
		mGPUStart = other.mGPUStart;
		mHandleIncreasement = other.mHandleIncreasement;
		mCurrentIndex = other.mCurrentIndex;
		mCapacity = other.mCapacity;

		other.mCPUStart = { 0 };
		other.mGPUStart = { 0 };
		other.mHandleIncreasement = 0;
		other.mCurrentIndex = 0;
		other.mCapacity = 0;
	}
	return *this; 
}

DescriptorHandle DescriptorHeap::Allocate() {
    if (mCurrentIndex >= mCapacity) {
        ErrorHandler::report("DescriptorHeap", "Out of descriptors!", ErrorHandler::Level::Critical);
    }

    DescriptorHandle handle;
    handle.Index = mCurrentIndex;
    handle.CpuHandle.ptr = mCPUStart.ptr + (mCurrentIndex * mHandleIncreasement);

    if (mHeap->GetDesc().Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
        handle.GpuHandle.ptr = mGPUStart.ptr + (mCurrentIndex * mHandleIncreasement);
    }

    mCurrentIndex++;
    return handle;
}

ID3D12DescriptorHeap* DescriptorHeap::GetHeap() const {
	return mHeap.Get();
}
