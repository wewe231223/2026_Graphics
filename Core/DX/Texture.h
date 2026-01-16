#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <memory>
#include <filesystem>
#include "External/Include/DirectXTK12/d3dx12.h"
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
}


enum class TextureUsage {
    ShaderResource,
    RenderTarget,
    DepthStencil,
    UnorderedAccess
};

// TODO : 다듬고, git 올리기. 라이브러리 파일들은 안올라가는거 까먹지 말기. 

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
    static Texture::Ptr LoadFromFile(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::filesystem::path& path);
    static Texture::Ptr CreateTarget(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format, TextureUsage usage, const D3D12_CLEAR_VALUE* optimizedClearValue = nullptr);

public:
    void CreateSRV(ID3D12Device* device, const DescriptorHandle& handle);
    void CreateRTV(ID3D12Device* device, const DescriptorHandle& handle);
    void CreateDSV(ID3D12Device* device, const DescriptorHandle& handle);
    void CreateUAV(ID3D12Device* device, const DescriptorHandle& handle, uint32_t mipSlice = 0);

    void ReleaseUploadBuffer();
    void Transition(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES newState);

    ID3D12Resource* GetResource() const { return mResource.Get(); }
    DXGI_FORMAT GetFormat() const { return mResourceDESC.Format; }
    uint32_t GetWidth() const { return static_cast<uint32_t>(mResourceDESC.Width); }
    uint32_t GetHeight() const { return mResourceDESC.Height; }

private:
    ComPtr<ID3D12Resource> mResource{ nullptr };
    ComPtr<ID3D12Resource> mUploadHeap{ nullptr };

    D3D12_RESOURCE_DESC mResourceDESC{};
    D3D12_RESOURCE_STATES mCurrentState{ D3D12_RESOURCE_STATE_COMMON };
    std::string mName{};
};