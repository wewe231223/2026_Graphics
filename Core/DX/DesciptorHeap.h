#pragma once
#include "Utility/DirectXInclude.h"
#include "Utility/ErrorHandler.h"

namespace Core {
    namespace DX {
        class DescriptorHandle {
        public:
            DescriptorHandle() = default;
            DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu, uint32_t index);

            ~DescriptorHandle() = default;

            DescriptorHandle(const DescriptorHandle&) = delete;
            DescriptorHandle& operator=(const DescriptorHandle&) = delete;

            DescriptorHandle(DescriptorHandle&& other) noexcept;
            DescriptorHandle& operator=(DescriptorHandle&& other) noexcept;

            bool IsValid() const;
            bool IsShaderVisible() const;

            D3D12_CPU_DESCRIPTOR_HANDLE GetCPU() const;
            D3D12_GPU_DESCRIPTOR_HANDLE GetGPU() const;
            uint32_t GetIndex() const;

        private:
            D3D12_CPU_DESCRIPTOR_HANDLE mCpuHandle{ 0 };
            D3D12_GPU_DESCRIPTOR_HANDLE mGpuHandle{ 0 };
            uint32_t mIndex{ 0 };
        };

        class DescriptorHeap {
        public:
            DescriptorHeap() = default; 
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
            ComPtr<ID3D12DescriptorHeap> mHeap{ nullptr };
            D3D12_CPU_DESCRIPTOR_HANDLE mCPUStart{ 0 };
            D3D12_GPU_DESCRIPTOR_HANDLE mGPUStart{ 0 };
            uint32_t mHandleIncreasement{};
            uint32_t mCurrentIndex{};
            uint32_t mCapacity{};
        };
    }
}
