#include "Texture.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include "Utility/ErrorHandler.h"

using namespace Core::DX;

DXGI_FORMAT TextureUtils::GetSrvFormat(DXGI_FORMAT defaultFormat) {
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

DXGI_FORMAT TextureUtils::GetDsvFormat(DXGI_FORMAT defaultFormat) {
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

DXGI_FORMAT TextureUtils::GetRtvFormat(DXGI_FORMAT defaultFormat) {
    switch (defaultFormat) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        default:
            return defaultFormat;
    }
}

DXGI_FORMAT TextureUtils::GetUavFormat(DXGI_FORMAT defaultFormat) {
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

Texture::Texture(const std::string& name)
    : mResource{},
    mResourceAllocation{},
    mResourceDesc{},
    mCurrentState{ D3D12_RESOURCE_STATE_COMMON },
    mName{ name },
    mSrvHandle{},
    mRtvHandle{},
    mDsvHandle{},
    mUavHandle{},
    mUploadFenceValue{} {
}

Texture::~Texture() {
    mResourceAllocation.reset();
}

Texture::Texture(Texture&& other) noexcept
    : mResource{ std::move(other.mResource) },
    mResourceAllocation{ std::move(other.mResourceAllocation) },
    mResourceDesc{ other.mResourceDesc },
    mCurrentState{ other.mCurrentState },
    mName{ std::move(other.mName) },
    mSrvHandle{ std::move(other.mSrvHandle) },
    mRtvHandle{ std::move(other.mRtvHandle) },
    mDsvHandle{ std::move(other.mDsvHandle) },
    mUavHandle{ std::move(other.mUavHandle) },
    mUploadFenceValue{ other.mUploadFenceValue } {
    other.mUploadFenceValue = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    mResource = std::move(other.mResource);
    mResourceAllocation = std::move(other.mResourceAllocation);
    mResourceDesc = other.mResourceDesc;
    mCurrentState = other.mCurrentState;
    mName = std::move(other.mName);
    mSrvHandle = std::move(other.mSrvHandle);
    mRtvHandle = std::move(other.mRtvHandle);
    mDsvHandle = std::move(other.mDsvHandle);
    mUavHandle = std::move(other.mUavHandle);
    mUploadFenceValue = other.mUploadFenceValue;
    other.mUploadFenceValue = 0;
    return *this;
}

Texture::Ptr Texture::LoadFromFile(ID3D12Device* device, Interface::IGraphicsAllocator* graphicsAllocator, Interface::ICopyQueue* copyQueue, const std::filesystem::path& path) {
    if (device == nullptr || graphicsAllocator == nullptr || copyQueue == nullptr) {
        return nullptr;
    }

    HRESULT hr{ S_OK };
    DirectX::ScratchImage image{};
    DirectX::TexMetadata metadata{};

    std::wstring extension{ path.extension().wstring() };
    std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
    if (extension != L".dds") {
        ErrorHandler::report("Texture", "Only DDS texture format is supported: " + path.string(), ErrorHandler::Level::Critical);
        return nullptr;
    }

    hr = DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);

    if (FAILED(hr)) {
        ErrorHandler::report("Texture", "Failed to load texture file: " + path.string(), ErrorHandler::Level::Critical);
    }

    Texture::Ptr texture{ std::make_shared<Texture>(path.string()) };
    texture->mResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(metadata.format, static_cast<UINT64>(metadata.width), static_cast<UINT>(metadata.height), static_cast<UINT16>(metadata.arraySize), static_cast<UINT16>(metadata.mipLevels), 1, 0, D3D12_RESOURCE_FLAG_NONE);
    texture->mCurrentState = D3D12_RESOURCE_STATE_COPY_DEST;

    std::unique_ptr<Interface::IAllocationHandle> allocation{ graphicsAllocator->AllocatePlacedResource(Interface::GraphicsAllocationType::Texture, texture->mResourceDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr) };
    if (allocation == nullptr || allocation->IsValid() == false) {
        ErrorHandler::report("Texture", "Failed to allocate texture resource: " + path.string(), ErrorHandler::Level::Critical);
    }

    texture->mResource = allocation->GetResource();
    texture->mResourceAllocation = std::move(allocation);

    std::vector<D3D12_SUBRESOURCE_DATA> subresources{};
    size_t imageCount{ image.GetImageCount() };
    subresources.reserve(imageCount);
    const DirectX::Image* images{ image.GetImages() };
    for (size_t imageIndex{ 0 }; imageIndex < imageCount; imageIndex++) {
        D3D12_SUBRESOURCE_DATA subresource{};
        subresource.pData = images[imageIndex].pixels;
        subresource.RowPitch = static_cast<LONG_PTR>(images[imageIndex].rowPitch);
        subresource.SlicePitch = static_cast<LONG_PTR>(images[imageIndex].slicePitch);
        subresources.push_back(subresource);
    }

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subresources.size());
    std::vector<UINT> rowCounts(subresources.size());
    std::vector<UINT64> rowSizes(subresources.size());
    UINT64 requiredBytes{ 0 };
    device->GetCopyableFootprints(&texture->mResourceDesc, 0, static_cast<UINT>(subresources.size()), 0, layouts.data(), rowCounts.data(), rowSizes.data(), &requiredBytes);

    std::vector<std::byte> uploadData(static_cast<size_t>(requiredBytes));
    std::vector<Interface::CopyQueueTextureCopySubresource> textureSubresources{};
    textureSubresources.reserve(subresources.size());
    for (size_t subresourceIndex{ 0 }; subresourceIndex < subresources.size(); subresourceIndex++) {
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout{ layouts[subresourceIndex] };
        const D3D12_SUBRESOURCE_DATA& sourceSubresource{ subresources[subresourceIndex] };
        uint32_t depth{ layout.Footprint.Depth };
        uint32_t rowCount{ rowCounts[subresourceIndex] };

        for (uint32_t depthIndex{ 0 }; depthIndex < depth; depthIndex++) {
            const std::byte* sourceSliceStart{ static_cast<const std::byte*>(sourceSubresource.pData) + static_cast<size_t>(sourceSubresource.SlicePitch) * depthIndex };
            std::byte* destinationSliceStart{ uploadData.data() + layout.Offset + static_cast<size_t>(layout.Footprint.RowPitch) * rowCount * depthIndex };
            for (uint32_t rowIndex{ 0 }; rowIndex < rowCount; rowIndex++) {
                const std::byte* sourceRowStart{ sourceSliceStart + static_cast<size_t>(sourceSubresource.RowPitch) * rowIndex };
                std::byte* destinationRowStart{ destinationSliceStart + static_cast<size_t>(layout.Footprint.RowPitch) * rowIndex };
                std::memcpy(destinationRowStart, sourceRowStart, static_cast<size_t>(rowSizes[subresourceIndex]));
            }
        }

        Interface::CopyQueueTextureCopySubresource copySubresource{};
        copySubresource.Footprint = layout;
        copySubresource.DestinationSubresourceIndex = static_cast<uint32_t>(subresourceIndex);
        textureSubresources.push_back(copySubresource);
    }

    Interface::CopyRequest copyRequest{};
    copyRequest.CopyType = Interface::CopyQueueCopyType::Texture;
    copyRequest.DestinationDefaultResource = texture->mResource.Get();
    copyRequest.SourceData = std::move(uploadData);
    copyRequest.TextureSubresources = std::move(textureSubresources);

    texture->mUploadFenceValue = copyQueue->EnqueueCopy(copyRequest);
    return texture;
}


Texture::Ptr Texture::LoadFromFile(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::filesystem::path& path) {
    (void)cmdList;
    ErrorHandler::report("Texture", "LoadFromFile without allocator/copyqueue is not supported: " + path.string(), ErrorHandler::Level::Critical);
    return nullptr;
}

Texture::Ptr Texture::CreateTarget(ID3D12Device* device, Interface::IGraphicsAllocator* graphicsAllocator, uint32_t width, uint32_t height, DXGI_FORMAT format, TextureUsage usage, const D3D12_CLEAR_VALUE* optimizedClearValue, uint16_t mipLevels) {
    if (device == nullptr || graphicsAllocator == nullptr) {
        return nullptr;
    }

    Texture::Ptr texture{ std::make_shared<Texture>("InternalTexture") };

    D3D12_RESOURCE_FLAGS flags{ D3D12_RESOURCE_FLAG_NONE };
    D3D12_RESOURCE_STATES initialState{ D3D12_RESOURCE_STATE_COMMON };
    if (usage == TextureUsage::RenderTarget) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        initialState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    else if (usage == TextureUsage::DepthStencil) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    else if (usage == TextureUsage::UnorderedAccess) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    texture->mResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, mipLevels, 1, 0, flags);
    texture->mCurrentState = initialState;

    std::unique_ptr<Interface::IAllocationHandle> allocation{ graphicsAllocator->AllocatePlacedResource(Interface::GraphicsAllocationType::Texture, texture->mResourceDesc, initialState, optimizedClearValue) };
    if (allocation == nullptr || allocation->IsValid() == false) {
        ErrorHandler::report("Texture", "Failed to allocate target texture.", ErrorHandler::Level::Critical);
    }

    texture->mResource = allocation->GetResource();
    texture->mResourceAllocation = std::move(allocation);
    return texture;
}


Texture::Ptr Texture::CreateTarget(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format, TextureUsage usage, const D3D12_CLEAR_VALUE* optimizedClearValue, uint16_t mipLevels) {
    if (device == nullptr) {
        return nullptr;
    }

    Texture::Ptr texture{ std::make_shared<Texture>("InternalTexture") };

    D3D12_RESOURCE_FLAGS flags{ D3D12_RESOURCE_FLAG_NONE };
    D3D12_RESOURCE_STATES initialState{ D3D12_RESOURCE_STATE_COMMON };
    if (usage == TextureUsage::RenderTarget) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        initialState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    else if (usage == TextureUsage::DepthStencil) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    else if (usage == TextureUsage::UnorderedAccess) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    texture->mResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, mipLevels, 1, 0, flags);
    texture->mCurrentState = initialState;
    CD3DX12_HEAP_PROPERTIES heapProp{ D3D12_HEAP_TYPE_DEFAULT };
    HRESULT hr{ device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &texture->mResourceDesc, initialState, optimizedClearValue, IID_PPV_ARGS(&texture->mResource)) };
    ErrorHandler::report(FAILED(hr), "Texture", "Failed to create committed target texture.", ErrorHandler::Level::Critical);
    return texture;
}

Texture::Ptr Texture::CreateFromResource(ID3D12Resource* externalResource, const std::string& name) {
    if (externalResource == nullptr) {
        return nullptr;
    }

    Texture::Ptr texture{ std::make_shared<Texture>(name) };
    texture->mResource = externalResource;
    texture->mResourceDesc = externalResource->GetDesc();
    texture->mCurrentState = D3D12_RESOURCE_STATE_COMMON;
    return texture;
}

void Texture::CreateSRV(ID3D12Device* device, DescriptorHeap& heap) {
    mSrvHandle = heap.Allocate();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = TextureUtils::GetSrvFormat(mResourceDesc.Format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = mResourceDesc.MipLevels;

    device->CreateShaderResourceView(mResource.Get(), &srvDesc, mSrvHandle.GetCPU());
}

void Texture::CreateRTV(ID3D12Device* device, DescriptorHeap& heap) {
    mRtvHandle = heap.Allocate();

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = TextureUtils::GetRtvFormat(mResourceDesc.Format);
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;

    device->CreateRenderTargetView(mResource.Get(), &rtvDesc, mRtvHandle.GetCPU());
}

void Texture::CreateDSV(ID3D12Device* device, DescriptorHeap& heap) {
    mDsvHandle = heap.Allocate();

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = TextureUtils::GetDsvFormat(mResourceDesc.Format);
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    device->CreateDepthStencilView(mResource.Get(), &dsvDesc, mDsvHandle.GetCPU());
}

void Texture::CreateUAV(ID3D12Device* device, DescriptorHeap& heap, uint32_t mipSlice) {
    mUavHandle = heap.Allocate();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = TextureUtils::GetUavFormat(mResourceDesc.Format);
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = mipSlice;

    device->CreateUnorderedAccessView(mResource.Get(), nullptr, &uavDesc, mUavHandle.GetCPU());
}

D3D12_GPU_DESCRIPTOR_HANDLE Texture::GetSRV() const {
    return mSrvHandle.IsShaderVisible() ? mSrvHandle.GetGPU() : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

D3D12_GPU_DESCRIPTOR_HANDLE Texture::GetUAV() const {
    return mSrvHandle.IsShaderVisible() ? mUavHandle.GetGPU() : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetRTV() const {
    return mRtvHandle.IsValid() ? mRtvHandle.GetCPU() : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetDSV() const {
    return mDsvHandle.IsValid() ? mDsvHandle.GetCPU() : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

void Texture::WaitForUpload(Interface::ICopyQueue& copyQueue) {
    if (mUploadFenceValue == 0) {
        return;
    }

    copyQueue.WaitForFence(mUploadFenceValue);
    mUploadFenceValue = 0;
}

bool Texture::HasPendingUpload() const {
    return mUploadFenceValue != 0;
}

uint64_t Texture::GetUploadFenceValue() const {
    return mUploadFenceValue;
}

void Texture::Transition(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES newState) {
    if (mCurrentState != newState) {
        CD3DX12_RESOURCE_BARRIER barrier{ CD3DX12_RESOURCE_BARRIER::Transition(mResource.Get(), mCurrentState, newState) };
        cmdList->ResourceBarrier(1, &barrier);
        mCurrentState = newState;
    }
}

ID3D12Resource* Texture::GetResource() const {
    return mResource.Get();
}

DXGI_FORMAT Texture::GetFormat() const {
    return mResourceDesc.Format;
}

uint32_t Texture::GetWidth() const {
    return static_cast<uint32_t>(mResourceDesc.Width);
}

uint32_t Texture::GetHeight() const {
    return mResourceDesc.Height;
}
