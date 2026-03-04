#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <memory>
#include <filesystem>
#include "External/Include/DirectXTK12/d3dx12.h"
#include "Core/Common.h"
#include "Core/DX/DesciptorHeap.h"

namespace TextureUtils {
    DXGI_FORMAT GetSrvFormat(DXGI_FORMAT DefaultFormat);
    DXGI_FORMAT GetDsvFormat(DXGI_FORMAT DefaultFormat);
    DXGI_FORMAT GetRtvFormat(DXGI_FORMAT DefaultFormat);
    DXGI_FORMAT GetUavFormat(DXGI_FORMAT DefaultFormat);
}

namespace Core {
    namespace DX {
        enum class TextureUsage {
            ShaderResource,
            RenderTarget,
            DepthStencil,
            UnorderedAccess
        };

        class Texture {
        public:
            using Ptr = std::shared_ptr<Texture>;

        public:
            Texture(const std::string& Name = "Unnamed");
            ~Texture();
            Texture(const Texture& Other) = delete;
            Texture& operator=(const Texture& Other) = delete;
            Texture(Texture&& Other) noexcept;
            Texture& operator=(Texture&& Other) noexcept;

        public:
            static Texture::Ptr LoadFromFile(ID3D12Device* Device, Interface::IGraphicsAllocator* GraphicsAllocator, Interface::ICopyQueue* CopyQueue, const std::filesystem::path& Path);
            static Texture::Ptr LoadFromFile(ID3D12Device* Device, ID3D12GraphicsCommandList* CmdList, const std::filesystem::path& Path);
            static Texture::Ptr CreateTarget(ID3D12Device* Device, Interface::IGraphicsAllocator* GraphicsAllocator, uint32_t Width, uint32_t Height, DXGI_FORMAT Format, TextureUsage Usage, const D3D12_CLEAR_VALUE* OptimizedClearValue = nullptr, uint16_t MipLevels = 1);
            static Texture::Ptr CreateTarget(ID3D12Device* Device, uint32_t Width, uint32_t Height, DXGI_FORMAT Format, TextureUsage Usage, const D3D12_CLEAR_VALUE* OptimizedClearValue = nullptr, uint16_t MipLevels = 1);
            static Texture::Ptr CreateFromResource(ID3D12Resource* ExternalResource, const std::string& Name);

        public:
            void CreateSRV(ID3D12Device* Device, DescriptorHeap& Heap);
            void CreateRTV(ID3D12Device* Device, DescriptorHeap& Heap);
            void CreateDSV(ID3D12Device* Device, DescriptorHeap& Heap);
            void CreateUAV(ID3D12Device* Device, DescriptorHeap& Heap, uint32_t MipSlice = 0);

            D3D12_GPU_DESCRIPTOR_HANDLE GetSRV() const;
            D3D12_GPU_DESCRIPTOR_HANDLE GetUAV() const;
            D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() const;
            D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const;

            void WaitForUpload(Interface::ICopyQueue& CopyQueue);
            void Transition(ID3D12GraphicsCommandList* CmdList, D3D12_RESOURCE_STATES NewState);

            bool HasPendingUpload() const;
            uint64_t GetUploadFenceValue() const;

            ID3D12Resource* GetResource() const;
            DXGI_FORMAT GetFormat() const;
            uint32_t GetWidth() const;
            uint32_t GetHeight() const;

        private:
            ComPtr<ID3D12Resource> mResource{ nullptr };
            std::unique_ptr<Interface::IAllocationHandle> mResourceAllocation{};
            D3D12_RESOURCE_DESC mResourceDesc{};
            D3D12_RESOURCE_STATES mCurrentState{ D3D12_RESOURCE_STATE_COMMON };
            std::string mName{};
            DescriptorHandle mSrvHandle{};
            DescriptorHandle mRtvHandle{};
            DescriptorHandle mDsvHandle{};
            DescriptorHandle mUavHandle{};
            uint64_t mUploadFenceValue{};
        };

        using TexPtr = Texture::Ptr;
    }
}
