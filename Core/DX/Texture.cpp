#include "Texture.h"
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <stdexcept>
#include "Utility/ErrorHandler.h"

using namespace DirectX;
using namespace Core::DX;

namespace {
    bool IsSupportedDdsPath(const std::filesystem::path& SourcePath) {
        std::wstring Extension{ SourcePath.extension().wstring() };
        std::transform(Extension.begin(), Extension.end(), Extension.begin(), ::towlower);
        return Extension == L".dds";
    }
}

Texture::Texture(const std::string& name)
    : mResource{ nullptr },
    mAllocationHandle{},
    mResourceDESC{},
    mCurrentState{ D3D12_RESOURCE_STATE_COMMON },
    mDevice{ nullptr },
    mName{ name },
    mSourceLayouts{},
    mSourceData{},
    mCopyFuture{},
    mSRVHandle{},
    mRTVHandle{},
    mDSVHandle{},
    mUAVHandle{} {
}

Texture::~Texture() {
}

Texture::Texture(Texture&& other) noexcept
    : mResource(std::move(other.mResource)),
    mAllocationHandle(std::move(other.mAllocationHandle)),
    mResourceDESC(std::move(other.mResourceDESC)),
    mCurrentState(other.mCurrentState),
    mDevice(other.mDevice),
    mName(std::move(other.mName)),
    mSourceLayouts(std::move(other.mSourceLayouts)),
    mSourceData(std::move(other.mSourceData)),
    mCopyFuture(std::move(other.mCopyFuture)),
    mSRVHandle(std::move(other.mSRVHandle)),
    mRTVHandle(std::move(other.mRTVHandle)),
    mDSVHandle(std::move(other.mDSVHandle)),
    mUAVHandle(std::move(other.mUAVHandle)) {
    other.mDevice = nullptr;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        mResource = std::move(other.mResource);
        mAllocationHandle = std::move(other.mAllocationHandle);
        mResourceDESC = std::move(other.mResourceDESC);
        mCurrentState = other.mCurrentState;
        mDevice = other.mDevice;
        mName = std::move(other.mName);
        mSourceLayouts = std::move(other.mSourceLayouts);
        mSourceData = std::move(other.mSourceData);
        mCopyFuture = std::move(other.mCopyFuture);
        mSRVHandle = std::move(other.mSRVHandle);
        mRTVHandle = std::move(other.mRTVHandle);
        mDSVHandle = std::move(other.mDSVHandle);
        mUAVHandle = std::move(other.mUAVHandle);
        other.mDevice = nullptr;
    }

    return *this;
}

Texture::Ptr Texture::CreateFromFile(ID3D12Device* Device, const std::filesystem::path& SourcePath) {
    if (Device == nullptr) {
        return nullptr;
    }

    if (IsSupportedDdsPath(SourcePath) == false) {
        return nullptr;
    }

    TexMetadata Metadata{};
    ScratchImage SourceImageSet{};
    HRESULT LoadResult{ LoadFromDDSFile(SourcePath.c_str(), DDS_FLAGS_NONE, &Metadata, SourceImageSet) };
    if (FAILED(LoadResult)) {
        ErrorHandler::report("Texture", "Failed to load DDS texture file: " + SourcePath.string(), ErrorHandler::Level::Critical);
        return nullptr;
    }

    Texture::Ptr NewTexture{ std::make_shared<Texture>(SourcePath.string()) };
    NewTexture->mDevice = Device;
    NewTexture->mResourceDESC = CD3DX12_RESOURCE_DESC::Tex2D(Metadata.format, static_cast<UINT64>(Metadata.width), static_cast<UINT>(Metadata.height), static_cast<UINT16>(Metadata.arraySize), static_cast<UINT16>(Metadata.mipLevels), 1, 0, D3D12_RESOURCE_FLAG_NONE);
    NewTexture->mCurrentState = D3D12_RESOURCE_STATE_COMMON;

    const UINT SubresourceCount{ static_cast<UINT>(SourceImageSet.GetImageCount()) };
    NewTexture->mSourceLayouts.resize(SubresourceCount);
    std::vector<UINT> NumRows(SubresourceCount);
    std::vector<UINT64> RowSizesInBytes(SubresourceCount);
    UINT64 RequiredSize{};
    Device->GetCopyableFootprints(&NewTexture->mResourceDESC, 0, SubresourceCount, 0, NewTexture->mSourceLayouts.data(), NumRows.data(), RowSizesInBytes.data(), &RequiredSize);

    NewTexture->mSourceData.resize(static_cast<std::size_t>(RequiredSize));
    const Image* SourceImages{ SourceImageSet.GetImages() };
    for (UINT SubresourceIndex{ 0 }; SubresourceIndex < SubresourceCount; ++SubresourceIndex) {
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& Layout{ NewTexture->mSourceLayouts[SubresourceIndex] };
        const Image& SourceImage{ SourceImages[SubresourceIndex] };
        std::byte* DestinationBase{ NewTexture->mSourceData.data() + Layout.Offset };
        const std::size_t SlicePitch{ SourceImage.slicePitch };
        const std::size_t RowPitch{ SourceImage.rowPitch };
        const std::uint32_t SliceCount{ static_cast<std::uint32_t>(Layout.Footprint.Depth) };
        const std::uint32_t RowCount{ NumRows[SubresourceIndex] };

        for (std::uint32_t SliceIndex{ 0 }; SliceIndex < SliceCount; ++SliceIndex) {
            const std::byte* SourceSliceBase{ reinterpret_cast<const std::byte*>(SourceImage.pixels) + (SlicePitch * SliceIndex) };
            std::byte* DestinationSliceBase{ DestinationBase + (static_cast<std::size_t>(Layout.Footprint.RowPitch) * RowCount * SliceIndex) };

            for (std::uint32_t RowIndex{ 0 }; RowIndex < RowCount; ++RowIndex) {
                const std::byte* SourceRow{ SourceSliceBase + (RowPitch * RowIndex) };
                std::byte* DestinationRow{ DestinationSliceBase + (static_cast<std::size_t>(Layout.Footprint.RowPitch) * RowIndex) };
                std::memcpy(DestinationRow, SourceRow, static_cast<std::size_t>(RowSizesInBytes[SubresourceIndex]));
            }
        }
    }

    return NewTexture;
}

bool Texture::Load(Interface::ICopyQueue* CopyQueue, Interface::IGraphicsAllocator* Allocator) {
    if (IsLoaded() == true) {
        return true;
    }

    if (CopyQueue == nullptr || Allocator == nullptr || mDevice == nullptr) {
        return false;
    }

    if (Allocator->CanAllocate(mResourceDESC) == false) {
        ErrorHandler::report("Texture", "Graphics allocator cannot allocate texture resource: " + mName, ErrorHandler::Level::Critical);
        return false;
    }

    std::wstring resourceName{ mName.begin(), mName.end() };
    Interface::AllocatePlacedResourceParameters allocationParameters{ mResourceDESC, mCurrentState, nullptr, resourceName.c_str() };
    mAllocationHandle = Allocator->AllocatePlacedResource(allocationParameters);
    if (mAllocationHandle == nullptr || mAllocationHandle->IsValid() == false) {
        ErrorHandler::report("Texture", "Failed to create texture resource: " + mName, ErrorHandler::Level::Critical);
        return false;
    }

    mResource = mAllocationHandle->GetResource();


    Interface::CopyQueueTextureCopyRequest CopyRequest{};
    CopyRequest.DestinationTextureResource = mResource;
    CopyRequest.SourceLayouts = mSourceLayouts;
    CopyRequest.SourceData = mSourceData;
    mCopyFuture = CopyQueue->EnqueueTextureCopyFuture(CopyRequest);
    if (mCopyFuture.IsValid() == false) {
        ErrorHandler::report("Texture", "Failed to enqueue texture upload copy request: " + mName, ErrorHandler::Level::Critical);
        mResource.Reset();
        mAllocationHandle.reset();
        return false;
    }

    return true;
}

#include "utility/stdoutput.h"
void Texture::Unload() {
    if (mCopyFuture.IsInFlight() == true) {
        mCopyFuture.Wait();
    }

    mResource.Reset();
    mAllocationHandle.reset();
    mCurrentState = D3D12_RESOURCE_STATE_COMMON;
    mCopyFuture = Interface::CopyFuture{};

	StdOutput::Print("Texture unloaded: {}", mName);
    
}


bool Texture::IsCopyInFlight() const {
    return mCopyFuture.IsInFlight();
}

void Texture::WaitForCopyCompletion() const {
    mCopyFuture.Wait();
}

Interface::CopyFuture Texture::GetCopyFuture() const {
    return mCopyFuture;
}

bool Texture::IsLoaded() const {
    return mResource != nullptr && mAllocationHandle != nullptr && mAllocationHandle->IsValid() == true;
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

    if (tex->mResource != nullptr) {
        std::wstring ResourceName{ tex->mName.begin(), tex->mName.end() };
        tex->mResource->SetName(ResourceName.c_str());
    }

    return tex;
}

Texture::Ptr Texture::CreateFromResource(ID3D12Resource* externalResource, const std::string& name) {
    auto tex = std::make_shared<Texture>(name);

    tex->mResource = externalResource;
    tex->mResourceDESC = externalResource->GetDesc();
    tex->mCurrentState = D3D12_RESOURCE_STATE_COMMON;

    if (externalResource != nullptr) {
        std::wstring ResourceName{ name.begin(), name.end() };
        externalResource->SetName(ResourceName.c_str());
    }

    return tex;
}

void Texture::CreateSRV(ID3D12Device* Device, Interface::IDescriptorHeap* Heap) {
    if (Heap == nullptr) {
        return;
    }

    mSRVHandle = Heap->Allocate();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = TextureUtils::GetSrvFormat(mResourceDESC.Format);
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = mResourceDESC.MipLevels;

    Device->CreateShaderResourceView(mResource.Get(), &srvDesc, mSRVHandle.GetCPU());
}

void Texture::CreateRTV(ID3D12Device* Device, Interface::IDescriptorHeap* Heap) {
    if (Heap == nullptr) {
        return;
    }

    mRTVHandle = Heap->Allocate();

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = TextureUtils::GetRtvFormat(mResourceDESC.Format);
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;

    Device->CreateRenderTargetView(mResource.Get(), &rtvDesc, mRTVHandle.GetCPU());
}

void Texture::CreateDSV(ID3D12Device* Device, Interface::IDescriptorHeap* Heap) {
    if (Heap == nullptr) {
        return;
    }

    mDSVHandle = Heap->Allocate();

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = TextureUtils::GetDsvFormat(mResourceDESC.Format);
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    Device->CreateDepthStencilView(mResource.Get(), &dsvDesc, mDSVHandle.GetCPU());
}

void Texture::CreateUAV(ID3D12Device* Device, Interface::IDescriptorHeap* Heap, uint32_t MipSlice) {
    if (Heap == nullptr) {
        return;
    }

    mUAVHandle = Heap->Allocate();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = TextureUtils::GetUavFormat(mResourceDESC.Format);
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = MipSlice;

    Device->CreateUnorderedAccessView(mResource.Get(), nullptr, &uavDesc, mUAVHandle.GetCPU());
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

DescriptorHandle Texture::GetSRVDescriptorHandle() const {
    return mSRVHandle;
}

void Texture::Transition(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES newState) {
    if (mCurrentState != newState) {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mResource.Get(), mCurrentState, newState);
        cmdList->ResourceBarrier(1, &barrier);
        mCurrentState = newState;
    }
}
