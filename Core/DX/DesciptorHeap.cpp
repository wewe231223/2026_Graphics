#include "DesciptorHeap.h"

using namespace	Core::DX;

DescriptorHandle::DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu, uint32_t index) : mCpuHandle(cpu), mGpuHandle(gpu), mIndex(index) {}

DescriptorHandle::DescriptorHandle(DescriptorHandle&& other) noexcept : mCpuHandle(std::exchange(other.mCpuHandle, { 0 }))
																		,mGpuHandle(std::exchange(other.mGpuHandle, { 0 }))
																		,mIndex(std::exchange(other.mIndex, 0)) {}

DescriptorHandle& DescriptorHandle::operator=(DescriptorHandle&& other) noexcept {
	if (this != &other) {
		mCpuHandle = std::exchange(other.mCpuHandle, { 0 });
		mGpuHandle = std::exchange(other.mGpuHandle, { 0 });
		mIndex = std::exchange(other.mIndex, 0);
	}
	return *this;
}

bool DescriptorHandle::IsValid() const {
	return mCpuHandle.ptr != 0;
}

bool DescriptorHandle::IsShaderVisible() const {
	return mGpuHandle.ptr != 0;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHandle::GetCPU() const {
	return mCpuHandle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHandle::GetGPU() const {
	return mGpuHandle; 
}

uint32_t DescriptorHandle::GetIndex() const {
	return mIndex;
}


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

	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{ mCPUStart };
	cpuHandle.ptr += static_cast<UINT64>(mCurrentIndex) * mHandleIncreasement;

	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{ 0 };
	if (mGPUStart.ptr != 0) {
		gpuHandle = mGPUStart;
		gpuHandle.ptr += static_cast<UINT64>(mCurrentIndex) * mHandleIncreasement;
	}

	DescriptorHandle handle(cpuHandle, gpuHandle, mCurrentIndex);
	mCurrentIndex++;

	return handle;
}

ID3D12DescriptorHeap* DescriptorHeap::GetHeap() const {
	return mHeap.Get();
}

