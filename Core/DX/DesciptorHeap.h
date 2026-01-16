#pragma once
#include "Utility/DirectXInclude.h"
#include "Utility/ErrorHandler.h"

struct DescriptorHandle {
    D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle{ 0 };
    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle{ 0 }; 
    uint32_t Index{ 0 };

    bool IsValid() const { 
        return CpuHandle.ptr != 0; 
    }
};

class DescriptorHeap {
public:
    DescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors, bool shaderVisible);

	~DescriptorHeap() = default;

	DescriptorHeap(const DescriptorHeap&) = delete;
	DescriptorHeap& operator=(const DescriptorHeap&) = delete;

	DescriptorHeap(DescriptorHeap&&) noexcept;
	DescriptorHeap& operator=(DescriptorHeap&&) noexcept;

public:
    DescriptorHandle Allocate();
    ID3D12DescriptorHeap* GetHeap() const;

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE mCPUStart{ 0 };
    D3D12_GPU_DESCRIPTOR_HANDLE mGPUStart{ 0 };
    uint32_t mHandleIncreasement{};
    uint32_t mCurrentIndex{};
    uint32_t mCapacity{};
};