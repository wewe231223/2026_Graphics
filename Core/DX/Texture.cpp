#include "Texture.h"
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <stdexcept>
#include "Core/DX/CopyQueueId.h"
#include "Utility/ErrorHandler.h"

using namespace DirectX;
using namespace Core::DX;

Texture::Texture(const std::string& name)
    : mResource{ nullptr },
    mResourceDESC{},
    mCurrentState{ D3D12_RESOURCE_STATE_COMMON },
    mName{ name },
    mSRVHandle{},
    mRTVHandle{},
    mDSVHandle{},
    mUAVHandle{} {
}

Texture::~Texture() {
}

Texture::Texture(Texture&& other) noexcept
    : mResource(std::move(other.mResource)),
    mResourceDESC(std::move(other.mResourceDESC)),
    mCurrentState(other.mCurrentState),
    mName(std::move(other.mName)),
    mSRVHandle(std::move(other.mSRVHandle)),
    mRTVHandle(std::move(other.mRTVHandle)),
    mDSVHandle(std::move(other.mDSVHandle)),
    mUAVHandle(std::move(other.mUAVHandle)) {
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        mResource = std::move(other.mResource);
        mResourceDESC = std::move(other.mResourceDESC);
        mCurrentState = other.mCurrentState;
        mName = std::move(other.mName);
        mSRVHandle = std::move(other.mSRVHandle);
        mRTVHandle = std::move(other.mRTVHandle);
        mDSVHandle = std::move(other.mDSVHandle);
        mUAVHandle = std::move(other.mUAVHandle);
    }

    return *this;
}

Texture::Ptr Texture::LoadFromFile(ID3D12Device* Device, Interface::ICopyQueue* CopyQueue, const std::filesystem::path& SourcePath) {
    if (Device == nullptr or CopyQueue == nullptr) {
        return nullptr;
    }

    std::wstring Extension{ SourcePath.extension().wstring() };
    std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
    if (Extension != L".dds") {
        return nullptr;
    }

    TexMetadata Metadata{};
    ScratchImage sImage{};
    HRESULT LoadResult{ LoadFromDDSFile(SourcePath.c_str(), DDS_FLAGS_NONE, &Metadata, sImage) };
    if (FAILED(LoadResult)) {
        ErrorHandler::report("Texture", "Failed to load DDS texture file: " + SourcePath.string(), ErrorHandler::Level::Critical);
        return nullptr;
    }

    Texture::Ptr NewTexture{ std::make_shared<Texture>(SourcePath.string()) };
    NewTexture->mResourceDESC = CD3DX12_RESOURCE_DESC::Tex2D(Metadata.format, static_cast<UINT64>(Metadata.width), static_cast<UINT>(Metadata.height), static_cast<UINT16>(Metadata.arraySize), static_cast<UINT16>(Metadata.mipLevels), 1, 0, D3D12_RESOURCE_FLAG_NONE);
    NewTexture->mCurrentState = D3D12_RESOURCE_STATE_COPY_DEST;

    CD3DX12_HEAP_PROPERTIES HeapProperties{ D3D12_HEAP_TYPE_DEFAULT };
    HRESULT CreateResourceResult{ Device->CreateCommittedResource(&HeapProperties, D3D12_HEAP_FLAG_NONE, &NewTexture->mResourceDESC, NewTexture->mCurrentState, nullptr, IID_PPV_ARGS(&NewTexture->mResource)) };
    if (FAILED(CreateResourceResult)) {
        ErrorHandler::report("Texture", "Failed to create texture resource: " + SourcePath.string(), ErrorHandler::Level::Critical);
        return nullptr;
    }

    NewTexture->mResource->SetName(SourcePath.c_str());

    UINT SubresourceCount{ static_cast<UINT>(sImage.GetImageCount()) };
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> Layouts(SubresourceCount);
    std::vector<UINT> NumRows(SubresourceCount);
    std::vector<UINT64> RowSizesInBytes(SubresourceCount);
    UINT64 RequiredSize{};
    Device->GetCopyableFootprints(&NewTexture->mResourceDESC, 0, SubresourceCount, 0, Layouts.data(), NumRows.data(), RowSizesInBytes.data(), &RequiredSize);

    std::vector<std::byte> UploadData(static_cast<std::size_t>(RequiredSize));
    const Image* SourceImages{ sImage.GetImages() };
    for (UINT SubresourceIndex = 0; SubresourceIndex < SubresourceCount; ++SubresourceIndex) {
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& Layout{ Layouts[SubresourceIndex] };
        const Image& SourceImage{ SourceImages[SubresourceIndex] };
        std::byte* DestinationBase{ UploadData.data() + Layout.Offset };
        std::size_t SlicePitch{ SourceImage.slicePitch };
        std::size_t RowPitch{ SourceImage.rowPitch };
        std::uint32_t SliceCount{ static_cast<std::uint32_t>(Layout.Footprint.Depth) };
        std::uint32_t RowCount{ NumRows[SubresourceIndex] };

        for (std::uint32_t SliceIndex = 0; SliceIndex < SliceCount; ++SliceIndex) {
            const std::byte* SourceSliceBase{ reinterpret_cast<const std::byte*>(SourceImage.pixels) + (SlicePitch * SliceIndex) };
            std::byte* DestinationSliceBase{ DestinationBase + (static_cast<std::size_t>(Layout.Footprint.RowPitch) * RowCount * SliceIndex) };

            for (std::uint32_t RowIndex = 0; RowIndex < RowCount; ++RowIndex) {
                const std::byte* SourceRow{ SourceSliceBase + (RowPitch * RowIndex) };
                std::byte* DestinationRow{ DestinationSliceBase + (static_cast<std::size_t>(Layout.Footprint.RowPitch) * RowIndex) };
                std::memcpy(DestinationRow, SourceRow, static_cast<std::size_t>(RowSizesInBytes[SubresourceIndex]));
            }
        }
    }

    Interface::CopyQueueTextureCopyRequest CopyRequest{};
    CopyRequest.DestinationTextureResource = NewTexture->mResource;
    CopyRequest.SourceLayouts = std::move(Layouts);
    CopyRequest.SourceData = std::move(UploadData);
    bool EnqueueResult{ CopyQueue->EnqueueTextureCopy(CopyQueueId::Texture, CopyRequest) };
    if (EnqueueResult == false) {
        ErrorHandler::report("Texture", "Failed to enqueue texture upload copy request: " + SourcePath.string(), ErrorHandler::Level::Critical);
        return nullptr;
    }

    return NewTexture;
}

Texture::Ptr Texture::CreateTarget(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format, TextureUsage usage, const D3D12_CLEAR_VALUE* optimizedClearValue, uint16_t mipLevels) {
    auto tex = std::make_shared<Texture>("InternalTexture");

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

    tex->mResourceDESC = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, mipLevels, 1, 0, flags);
    tex->mCurrentState = initialState;

    auto heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    if (FAILED(device->CreateCommittedResource( &heapProp, D3D12_HEAP_FLAG_NONE, &tex->mResourceDESC, initialState, optimizedClearValue, IID_PPV_ARGS(&tex->mResource)))) {
        ErrorHandler::report("Texture", "Failed to create target texture.", ErrorHandler::Level::Critical);
    }

    return tex;
}

Texture::Ptr Texture::CreateFromResource(ID3D12Resource* externalResource, const std::string& name) {
    auto tex = std::make_shared<Texture>(name);

    tex->mResource = externalResource;
    tex->mResourceDESC = externalResource->GetDesc();
    tex->mCurrentState = D3D12_RESOURCE_STATE_COMMON;

    return tex;
}

void Texture::CreateSRV(ID3D12Device* device, DescriptorHeap& heap) {
    mSRVHandle = heap.Allocate();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = TextureUtils::GetSrvFormat(mResourceDESC.Format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = mResourceDESC.MipLevels;

    device->CreateShaderResourceView(mResource.Get(), &srvDesc, mSRVHandle.GetCPU());
}

void Texture::CreateRTV(ID3D12Device* device, DescriptorHeap& heap) {
    mRTVHandle = heap.Allocate();

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = TextureUtils::GetRtvFormat(mResourceDESC.Format);
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;

    device->CreateRenderTargetView(mResource.Get(), &rtvDesc, mRTVHandle.GetCPU());
}

void Texture::CreateDSV(ID3D12Device* device, DescriptorHeap& heap) {
    mDSVHandle = heap.Allocate();

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = TextureUtils::GetDsvFormat(mResourceDESC.Format);
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    device->CreateDepthStencilView(mResource.Get(), &dsvDesc, mDSVHandle.GetCPU());
}

void Texture::CreateUAV(ID3D12Device* device, DescriptorHeap& heap, uint32_t mipSlice) {
    mUAVHandle = heap.Allocate();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = TextureUtils::GetUavFormat(mResourceDESC.Format);
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = mipSlice;

    device->CreateUnorderedAccessView(mResource.Get(), nullptr, &uavDesc, mUAVHandle.GetCPU());
}

D3D12_GPU_DESCRIPTOR_HANDLE Texture::GetSRV() const {
    return mSRVHandle.IsShaderVisible() ? mSRVHandle.GetGPU() : D3D12_GPU_DESCRIPTOR_HANDLE();
}

D3D12_GPU_DESCRIPTOR_HANDLE Texture::GetUAV() const {
    return mSRVHandle.IsShaderVisible() ? mUAVHandle.GetGPU() : D3D12_GPU_DESCRIPTOR_HANDLE();
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetRTV() const {
    return mRTVHandle.IsValid() ? mRTVHandle.GetCPU() : D3D12_CPU_DESCRIPTOR_HANDLE();
}

D3D12_CPU_DESCRIPTOR_HANDLE Texture::GetDSV() const {
    return mDSVHandle.IsValid() ? mDSVHandle.GetCPU() : D3D12_CPU_DESCRIPTOR_HANDLE();
}

void Texture::Transition(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES newState) {
    if (mCurrentState != newState) {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mResource.Get(), mCurrentState, newState);
        cmdList->ResourceBarrier(1, &barrier);
        mCurrentState = newState;
    }
}
