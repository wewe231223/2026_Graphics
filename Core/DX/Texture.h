#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <memory>
#include <filesystem>
#include "External/Include/DirectXTK12/d3dx12.h"
#include "Core/Common.h"
#include "Core/DX/DesciptorHeap.h" 

namespace TextureUtils {
    inline DXGI_FORMAT GetSrvFormat(DXGI_FORMAT defaultFormat) {
        switch (defaultFormat) {
            case DXGI_FORMAT_R24G8_TYPELESS: 
                return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            case DXGI_FORMAT_R32_TYPELESS: 
                return DXGI_FORMAT_R32_FLOAT;
            case DXGI_FORMAT_R16_TYPELESS: 
                return DXGI_FORMAT_R16_UNORM;
            case DXGI_FORMAT_R8G8B8A8_TYPELESS: 
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            default: 
                return defaultFormat; 
        }
    }

    inline DXGI_FORMAT GetDsvFormat(DXGI_FORMAT defaultFormat) {
        switch (defaultFormat) {
            case DXGI_FORMAT_R24G8_TYPELESS: 
                return DXGI_FORMAT_D24_UNORM_S8_UINT;
            case DXGI_FORMAT_R32_TYPELESS: 
                return DXGI_FORMAT_D32_FLOAT;
            case DXGI_FORMAT_R16_TYPELESS: 
                return DXGI_FORMAT_D16_UNORM;
            default: 
                return defaultFormat;
        }
    }

    inline DXGI_FORMAT GetRtvFormat(DXGI_FORMAT defaultFormat) {
        switch (defaultFormat) {
            case DXGI_FORMAT_R8G8B8A8_TYPELESS: 
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case DXGI_FORMAT_B8G8R8A8_TYPELESS: 
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            default: return defaultFormat;
        }
    }

    inline DXGI_FORMAT GetUavFormat(DXGI_FORMAT defaultFormat) {
        switch (defaultFormat) {
            case DXGI_FORMAT_R32_TYPELESS: 
                return DXGI_FORMAT_R32_FLOAT;
            case DXGI_FORMAT_R32G32_TYPELESS: 
                return DXGI_FORMAT_R32G32_FLOAT;
            case DXGI_FORMAT_R32G32B32_TYPELESS: 
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case DXGI_FORMAT_R32G32B32A32_TYPELESS: 
                return DXGI_FORMAT_R32G32B32A32_FLOAT;

            case DXGI_FORMAT_R16_TYPELESS: 
                return DXGI_FORMAT_R16_FLOAT;
            case DXGI_FORMAT_R16G16_TYPELESS: 
                return DXGI_FORMAT_R16G16_FLOAT;
            case DXGI_FORMAT_R16G16B16A16_TYPELESS: 
                return DXGI_FORMAT_R16G16B16A16_FLOAT;

            case DXGI_FORMAT_R8G8B8A8_TYPELESS: 
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case DXGI_FORMAT_B8G8R8A8_TYPELESS: 
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            case DXGI_FORMAT_R8_TYPELESS:       
                return DXGI_FORMAT_R8_UNORM;

            default: 
                return defaultFormat;
        }
    }
}

namespace Core {
    namespace DX {
        enum class TextureUsage {
            ShaderResource,
            RenderTarget,
            DepthStencil,
            UnorderedAccess,
            RenderTargetUnorderedAccess
        };

        class Texture {
        public:
            using Ptr = std::shared_ptr<Texture>;

            // 생성자
            Texture(const std::string& name = "Unnamed");
            ~Texture();

            Texture(const Texture&) = delete;
            Texture& operator=(const Texture&) = delete;

            Texture(Texture&&) noexcept;
            Texture& operator=(Texture&&) noexcept;

        public:
            static Texture::Ptr CreateTarget(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format, TextureUsage usage, const D3D12_CLEAR_VALUE* optimizedClearValue = nullptr, uint16_t mipLevels = 1);
            static Texture::Ptr CreateFromResource(ID3D12Resource* externalResource, const std::string& name);
            static Texture::Ptr CreateFromFile(ID3D12Device* Device, const std::filesystem::path& SourcePath);

        public:
            bool Load(Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator);
            void Unload();
            bool IsLoaded() const;
            bool IsCopyInFlight() const;
            void WaitForCopyCompletion() const;
            Interface::Future GetCopyFuture() const;
            void CreateSRV(ID3D12Device* Device, Interface::IDescriptorHeap* Heap);
            void CreateRTV(ID3D12Device* Device, Interface::IDescriptorHeap* Heap);
            void CreateDSV(ID3D12Device* Device, Interface::IDescriptorHeap* Heap);
            void CreateUAV(ID3D12Device* Device, Interface::IDescriptorHeap* Heap, uint32_t MipSlice = 0);

            D3D12_GPU_DESCRIPTOR_HANDLE GetSRV() const;
            D3D12_GPU_DESCRIPTOR_HANDLE GetUAV() const;
            D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() const; 
            D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const; 
            DescriptorHandle GetSRVDescriptorHandle() const;
            DescriptorHandle GetUAVDescriptorHandle() const;

            void Transition(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES newState);

            ID3D12Resource* GetResource() const { return mResource.Get(); }
            DXGI_FORMAT GetFormat() const { return mResourceDESC.Format; }
            uint32_t GetWidth() const { return static_cast<uint32_t>(mResourceDESC.Width); }
            uint32_t GetHeight() const { return mResourceDESC.Height; }

        private:
            ComPtr<ID3D12Resource> mResource{ nullptr };
            std::unique_ptr<Interface::IAllocationHandle> mAllocationHandle{};
            D3D12_RESOURCE_DESC mResourceDESC{};
            D3D12_RESOURCE_STATES mCurrentState{ D3D12_RESOURCE_STATE_COMMON };
            ID3D12Device* mDevice{ nullptr };
            std::string mName{};
            std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> mSourceLayouts{};
            std::vector<std::byte> mSourceData{};
            Interface::Future mCopyFuture{};

            DescriptorHandle mSRVHandle;
            DescriptorHandle mRTVHandle;
            DescriptorHandle mDSVHandle;
            DescriptorHandle mUAVHandle;
        };

        using TexPtr = Texture::Ptr; 

    }
}
