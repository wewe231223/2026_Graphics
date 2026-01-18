#include "Texture.h"
#include <algorithm>
#include <stdexcept>


using namespace DirectX;
using namespace Core::DX;

Texture::Texture(const std::string& name) : mName(name) {}

Texture::~Texture() {
    ReleaseUploadBuffer();
}

void Texture::ReleaseUploadBuffer() {
    if (mUploadHeap) {
        mUploadHeap.Reset();
    }
}

Texture::Texture(Texture&& other) noexcept :    mResource(std::move(other.mResource)),
                                                mUploadHeap(std::move(other.mUploadHeap)), 
                                                mResourceDESC(std::move(other.mResourceDESC)),
                                                mCurrentState(other.mCurrentState), 
                                                mName(std::move(other.mName)) {

}

Texture& Texture::operator=(Texture&& other) noexcept {
	if (this != &other) {
		mResource = std::move(other.mResource);
		mUploadHeap = std::move(other.mUploadHeap);
		mResourceDESC = std::move(other.mResourceDESC);
		mCurrentState = other.mCurrentState;
		mName = std::move(other.mName);
	}
	return *this;
}

Texture::Ptr Texture::LoadFromFile(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::filesystem::path& path) {
    HRESULT hr{ S_OK };
    ScratchImage image;
    TexMetadata metadata;

    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    if (ext == L".dds") {
        hr = LoadFromDDSFile(path.c_str(), DDS_FLAGS_NONE, &metadata, image);
    }
    else if (ext == L".tga") {
        hr = LoadFromTGAFile(path.c_str(), &metadata, image);
    }
    else if (ext == L".hdr") {
        hr = LoadFromHDRFile(path.c_str(), &metadata, image);
    }
    else {
        hr = LoadFromWICFile(path.c_str(), WIC_FLAGS_NONE, &metadata, image);
    }

    if (FAILED(hr)) {
        ErrorHandler::report("Texture", "Failed to load texture file: " + path.string(), ErrorHandler::Level::Critical);
    }

    auto tex = std::make_shared<Texture>(path.string());

    tex->mResourceDESC = CD3DX12_RESOURCE_DESC::Tex2D(
        metadata.format,
        static_cast<UINT64>(metadata.width),
        static_cast<UINT>(metadata.height),
        static_cast<UINT16>(metadata.arraySize),
        static_cast<UINT16>(metadata.mipLevels),
        1, 0, D3D12_RESOURCE_FLAG_NONE
    );

    auto heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    tex->mCurrentState = D3D12_RESOURCE_STATE_COPY_DEST;

    if (FAILED(device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &tex->mResourceDESC, tex->mCurrentState, nullptr, IID_PPV_ARGS(&tex->mResource)))) {
		ErrorHandler::report("Texture", "Failed to create texture resource: " + path.string(), ErrorHandler::Level::Critical);
    }
    tex->mResource->SetName(path.c_str());

    const Image* images = image.GetImages();
    size_t nImages = image.GetImageCount();

    std::vector<D3D12_SUBRESOURCE_DATA> subresources;
    subresources.reserve(nImages);
    for (size_t i = 0; i < nImages; ++i) {
        D3D12_SUBRESOURCE_DATA subData = {};
        subData.pData = images[i].pixels;
        subData.RowPitch = static_cast<LONG_PTR>(images[i].rowPitch);
        subData.SlicePitch = static_cast<LONG_PTR>(images[i].slicePitch);
        subresources.push_back(subData);
    }

    UINT64 uploadBufferSize = GetRequiredIntermediateSize(tex->mResource.Get(), 0, static_cast<UINT>(nImages));

    auto uploadHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

    if (FAILED(device->CreateCommittedResource(&uploadHeapProp, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&tex->mUploadHeap)))) {
		ErrorHandler::report("Texture", "Failed to create texture upload buffer: " + path.string(), ErrorHandler::Level::Critical);
    }
    tex->mUploadHeap->SetName(L"Texture_UploadBuffer");

    UpdateSubresources(cmdList, tex->mResource.Get(), tex->mUploadHeap.Get(), 0, 0, static_cast<UINT>(nImages), subresources.data());

    tex->Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    return tex;
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

void Texture::CreateSRV(ID3D12Device* device, const DescriptorHandle& handle) {
    if (!handle.IsValid()) {
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};

    srvDesc.Format = TextureUtils::GetSrvFormat(mResourceDESC.Format);

    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

    if (mResourceDESC.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
        srvDesc.Texture2D.MipLevels = 1;
    }
    else {
        srvDesc.Texture2D.MipLevels = mResourceDESC.MipLevels;
    }

    device->CreateShaderResourceView(mResource.Get(), &srvDesc, handle.CpuHandle);
}

void Texture::CreateRTV(ID3D12Device* device, const DescriptorHandle& handle) {
    if (!handle.IsValid()) {
        return;
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};

    rtvDesc.Format = TextureUtils::GetRtvFormat(mResourceDESC.Format);

    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;

    device->CreateRenderTargetView(mResource.Get(), &rtvDesc, handle.CpuHandle);
}

void Texture::CreateDSV(ID3D12Device* device, const DescriptorHandle& handle) {
    if (!handle.IsValid()) {
        return;
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};

    dsvDesc.Format = TextureUtils::GetDsvFormat(mResourceDESC.Format);

    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    device->CreateDepthStencilView(mResource.Get(), &dsvDesc, handle.CpuHandle);
}

void Texture::CreateUAV(ID3D12Device* device, const DescriptorHandle& handle, uint32_t mipSlice) {
    if (!handle.IsValid()) {
        return;
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = mResourceDESC.Format; 
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = mipSlice;

    device->CreateUnorderedAccessView(mResource.Get(), nullptr, &uavDesc, handle.CpuHandle);
}

void Texture::Transition(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES newState) {
    if (mCurrentState != newState) {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mResource.Get(), mCurrentState, newState);
        cmdList->ResourceBarrier(1, &barrier);
        mCurrentState = newState;
    }
}