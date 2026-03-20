#include "Model.h"
#include <cstring>
#include "Core/DX/CopyQueueId.h"
#include <utility>
#include "Asset/NumericTypes.h"

namespace {
    struct AttributeUploadSource final {
        Game::VertexAttributeKind Kind{};
        const void* DataPointer{ nullptr };
        std::size_t ByteSize{ 0 };
        UINT StrideInBytes{ 0 };
    };

    bool AllocateBufferResource(Interface::IGraphicsAllocator* Allocator, std::size_t ByteSize, std::unique_ptr<Interface::IAllocationHandle>& OutAllocation) {
        if (Allocator == nullptr || ByteSize == 0) {
            return false;
        }

        D3D12_RESOURCE_DESC ResourceDescription{};
        ResourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ResourceDescription.Alignment = 0;
        ResourceDescription.Width = static_cast<UINT64>(ByteSize);
        ResourceDescription.Height = 1;
        ResourceDescription.DepthOrArraySize = 1;
        ResourceDescription.MipLevels = 1;
        ResourceDescription.Format = DXGI_FORMAT_UNKNOWN;
        ResourceDescription.SampleDesc.Count = 1;
        ResourceDescription.SampleDesc.Quality = 0;
        ResourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ResourceDescription.Flags = D3D12_RESOURCE_FLAG_NONE;

        if (Allocator->CanAllocate(ResourceDescription) == false) {
            return false;
        }

        OutAllocation = Allocator->AllocatePlacedResource(ResourceDescription, D3D12_RESOURCE_STATE_COPY_DEST, nullptr);
        if (OutAllocation == nullptr) {
            return false;
        }

        return OutAllocation->IsValid();
    }

    void AppendAttributeSource(std::vector<AttributeUploadSource>& Sources, Game::VertexAttributeKind Kind, const void* DataPointer, std::size_t ByteSize, UINT StrideInBytes) {
        if (DataPointer == nullptr || ByteSize == 0) {
            return;
        }

        AttributeUploadSource Source{};
        Source.Kind = Kind;
        Source.DataPointer = DataPointer;
        Source.ByteSize = ByteSize;
        Source.StrideInBytes = StrideInBytes;
        Sources.push_back(Source);
    }
}

namespace Game {
    ModelNode::ModelNode()
        : mId{ 0 },
        mName{},
        mNodeToParent{},
        mChildren{},
        mSubMeshes{},
        mBoneInfos{},
        mVertexRawData{},
        mVertexAttributeRanges{},
        mVertexAllocation{},
        mVertexBufferViews{},
        mIndexRawData{},
        mIndexAllocation{},
        mIndexBufferView{},
        mHasVertexData{ false } {
    }

    ModelNode::~ModelNode() {
    }

    ModelNode::ModelNode(ModelNode&& Other) noexcept
        : mId{ Other.mId },
        mName{ std::move(Other.mName) },
        mNodeToParent{ Other.mNodeToParent },
        mChildren{ std::move(Other.mChildren) },
        mSubMeshes{ std::move(Other.mSubMeshes) },
        mBoneInfos{ std::move(Other.mBoneInfos) },
        mVertexRawData{ std::move(Other.mVertexRawData) },
        mVertexAttributeRanges{ std::move(Other.mVertexAttributeRanges) },
        mVertexAllocation{ std::move(Other.mVertexAllocation) },
        mVertexBufferViews{ std::move(Other.mVertexBufferViews) },
        mIndexRawData{ std::move(Other.mIndexRawData) },
        mIndexAllocation{ std::move(Other.mIndexAllocation) },
        mIndexBufferView{ Other.mIndexBufferView },
        mHasVertexData{ Other.mHasVertexData } {
        Other.mId = 0;
        Other.mHasVertexData = false;
        Other.mIndexBufferView = D3D12_INDEX_BUFFER_VIEW{};
    }

    ModelNode& ModelNode::operator=(ModelNode&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mId = Other.mId;
        mName = std::move(Other.mName);
        mNodeToParent = Other.mNodeToParent;
        mChildren = std::move(Other.mChildren);
        mSubMeshes = std::move(Other.mSubMeshes);
        mBoneInfos = std::move(Other.mBoneInfos);
        mVertexRawData = std::move(Other.mVertexRawData);
        mVertexAttributeRanges = std::move(Other.mVertexAttributeRanges);
        mVertexAllocation = std::move(Other.mVertexAllocation);
        mVertexBufferViews = std::move(Other.mVertexBufferViews);
        mIndexRawData = std::move(Other.mIndexRawData);
        mIndexAllocation = std::move(Other.mIndexAllocation);
        mIndexBufferView = Other.mIndexBufferView;
        mHasVertexData = Other.mHasVertexData;

        Other.mId = 0;
        Other.mHasVertexData = false;
        Other.mIndexBufferView = D3D12_INDEX_BUFFER_VIEW{};
        return *this;
    }

    std::uint32_t ModelNode::GetId() const {
        return mId;
    }

    const std::string& ModelNode::GetName() const {
        return mName;
    }

    const SimpleMath::Matrix& ModelNode::GetNodeToParent() const {
        return mNodeToParent;
    }


    const std::vector<std::uint32_t>& ModelNode::GetChildren() const {
        return mChildren;
    }

    const std::vector<ModelSubMesh>& ModelNode::GetSubMeshes() const {
        return mSubMeshes;
    }

    const ModelSubMesh& ModelNode::GetSubMesh(std::size_t index) const {
        return mSubMeshes[index];
    }

    const std::vector<ModelBoneInfo>& ModelNode::GetBoneInfos() const {
        return mBoneInfos;
    }

    bool ModelNode::HasBoneInfo() const {
        return mBoneInfos.empty() == false;
    }

    bool ModelNode::HasVertexData() const {
        return mHasVertexData;
    }

    const std::vector<D3D12_VERTEX_BUFFER_VIEW>& ModelNode::GetVertexBufferViews() const {
        return mVertexBufferViews;
    }

    const D3D12_INDEX_BUFFER_VIEW& ModelNode::GetIndexBufferView() const {
        return mIndexBufferView;
    }

    std::size_t ModelNode::GetVertexAttributeBufferCount() const {
        return mVertexAttributeRanges.size();
    }

    VertexAttributeKind ModelNode::GetVertexAttributeKind(std::size_t AttributeIndex) const {
        return mVertexAttributeRanges[AttributeIndex].Kind;
    }

    std::span<const std::byte> ModelNode::GetVertexAttributeRawData(std::size_t AttributeIndex) const {
        const VertexAttributeRange& Range{ mVertexAttributeRanges[AttributeIndex] };
        return std::span<const std::byte>{ mVertexRawData.data() + Range.Offset, Range.Size };
    }

    void ModelNode::SetBasicData(std::uint32_t IdValue, std::string NameValue, const SimpleMath::Matrix& NodeToParentValue, std::vector<std::uint32_t> ChildrenValue, std::vector<ModelSubMesh> SubMeshesValue, std::vector<ModelBoneInfo> BoneInfosValue) {
        mId = IdValue;
        mName = std::move(NameValue);
        mNodeToParent = NodeToParentValue;
        mChildren = std::move(ChildrenValue);
        mSubMeshes = std::move(SubMeshesValue);
        mBoneInfos = std::move(BoneInfosValue);
    }

    void ModelNode::SetVertexData(std::vector<std::byte> VertexRawDataValue, std::vector<VertexAttributeRange> VertexAttributeRangesValue, std::unique_ptr<Interface::IAllocationHandle> VertexAllocationValue, std::vector<D3D12_VERTEX_BUFFER_VIEW> VertexBufferViewsValue) {
        mVertexRawData = std::move(VertexRawDataValue);
        mVertexAttributeRanges = std::move(VertexAttributeRangesValue);
        mVertexAllocation = std::move(VertexAllocationValue);
        mVertexBufferViews = std::move(VertexBufferViewsValue);
        mHasVertexData = mVertexBufferViews.empty() == false;
    }

    void ModelNode::SetIndexData(std::vector<std::byte> IndexRawDataValue, std::unique_ptr<Interface::IAllocationHandle> IndexAllocationValue, const D3D12_INDEX_BUFFER_VIEW& IndexBufferViewValue) {
        mIndexRawData = std::move(IndexRawDataValue);
        mIndexAllocation = std::move(IndexAllocationValue);
        mIndexBufferView = IndexBufferViewValue;
    }

    Model::Model()
        : mNodes{},
        mNodeNameLookup{},
        mRootNodeIndex{ 0 },
        mHasRootNode{ false } {
    }

    Model::~Model() {
    }

    Model::Model(Model&& Other) noexcept
        : mNodes{ std::move(Other.mNodes) },
        mNodeNameLookup{ std::move(Other.mNodeNameLookup) },
        mRootNodeIndex{ Other.mRootNodeIndex },
        mHasRootNode{ Other.mHasRootNode } {
        Other.mRootNodeIndex = 0;
        Other.mHasRootNode = false;
    }

    Model& Model::operator=(Model&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mNodes = std::move(Other.mNodes);
        mNodeNameLookup = std::move(Other.mNodeNameLookup);
        mRootNodeIndex = Other.mRootNodeIndex;
        mHasRootNode = Other.mHasRootNode;
        Other.mRootNodeIndex = 0;
        Other.mHasRootNode = false;
        return *this;
    }

    bool Model::InitializeFromModelResult(const asset::ModelResult& ModelData, Interface::IGraphicsAllocator* Allocator, Interface::ICopyQueue* CopyQueue) {
        if (Allocator == nullptr || CopyQueue == nullptr) {
            return false;
        }

        mNodes.clear();
        mNodeNameLookup.clear();
        mRootNodeIndex = 0;
        mHasRootNode = false;

        const std::vector<std::unique_ptr<asset::ModelNode>>& SourceNodes{ ModelData.Nodes() };
        mNodes.resize(SourceNodes.size());

        for (std::size_t NodeIndex{ 0 }; NodeIndex < SourceNodes.size(); ++NodeIndex) {
            const asset::ModelNode& SourceNode{ *SourceNodes[NodeIndex] };
            ModelNode& DestinationNode{ mNodes[NodeIndex] };

            std::vector<std::uint32_t> Children{};
            const std::vector<asset::ModelNode*>& SourceChildren{ SourceNode.GetChildren() };
            Children.reserve(SourceChildren.size());
            for (const asset::ModelNode* ChildNode : SourceChildren) {
                for (std::size_t ChildIndex{ 0 }; ChildIndex < SourceNodes.size(); ++ChildIndex) {
                    if (SourceNodes[ChildIndex].get() == ChildNode) {
                        Children.push_back(static_cast<std::uint32_t>(ChildIndex));
                        break;
                    }
                }
            }

            std::vector<ModelSubMesh> SubMeshes{};
            const std::vector<asset::ModelNode::SubMesh>& SourceSubMeshes{ SourceNode.GetSubMeshes() };
            SubMeshes.reserve(SourceSubMeshes.size());
            for (const asset::ModelNode::SubMesh& SourceSubMesh : SourceSubMeshes) {
                ModelSubMesh SubMesh{};
                SubMesh.IndexOffset = SourceSubMesh.IndexOffset;
                SubMesh.IndexCount = SourceSubMesh.IndexCount;
                SubMesh.MaterialGroupItemIndex = SourceSubMesh.MaterialGroupItemIndex;
                SubMeshes.push_back(SubMesh);
            }

            std::vector<ModelBoneInfo> BoneInfos{};
            const std::vector<asset::ModelBoneInfo>& SourceBoneInfos{ SourceNode.BoneInfos() };
            BoneInfos.reserve(SourceBoneInfos.size());
            for (const asset::ModelBoneInfo& SourceBoneInfo : SourceBoneInfos) {
                ModelBoneInfo BoneInfo{};
                BoneInfo.SkinArrayIndex = SourceBoneInfo.SkinArrayIndex;
                BoneInfo.JointArrayIndex = SourceBoneInfo.JointArrayIndex;
                BoneInfo.InverseBindMatrix = asset::ToSimpleMath(SourceBoneInfo.InverseBindMatrix);
                BoneInfos.push_back(BoneInfo);
            }

            DestinationNode.SetBasicData(SourceNode.GetId(), SourceNode.GetName(), asset::ToSimpleMath(SourceNode.GetNodeToParent()), std::move(Children), std::move(SubMeshes), std::move(BoneInfos));

            std::vector<std::byte> VertexRawData{};
            std::vector<ModelNode::VertexAttributeRange> VertexRanges{};
            std::unique_ptr<Interface::IAllocationHandle> VertexAllocation{};
            std::vector<D3D12_VERTEX_BUFFER_VIEW> VertexBufferViews{};
            UploadVertexData(SourceNode.Vertices(), Allocator, CopyQueue, VertexRawData, VertexRanges, VertexAllocation, VertexBufferViews);
            DestinationNode.SetVertexData(std::move(VertexRawData), std::move(VertexRanges), std::move(VertexAllocation), std::move(VertexBufferViews));

            std::vector<std::byte> IndexRawData{};
            std::unique_ptr<Interface::IAllocationHandle> IndexAllocation{};
            D3D12_INDEX_BUFFER_VIEW IndexBufferView{};
            UploadIndexData(SourceNode.Indices(), Allocator, CopyQueue, IndexRawData, IndexAllocation, IndexBufferView);
            DestinationNode.SetIndexData(std::move(IndexRawData), std::move(IndexAllocation), IndexBufferView);

            mNodeNameLookup.insert_or_assign(DestinationNode.GetName(), static_cast<std::uint32_t>(NodeIndex));

            if (SourceNode.GetParent() == nullptr) {
                mRootNodeIndex = static_cast<std::uint32_t>(NodeIndex);
                mHasRootNode = true;
            }
        }

        return true;
    }

    const ModelNode* Model::GetRootNode() const {
        if (mHasRootNode == false) {
            return nullptr;
        }

        return &mNodes[mRootNodeIndex];
    }

    const ModelNode* Model::FindNodeByName(const std::string& NodeName) const {
        const auto FoundNode{ mNodeNameLookup.find(NodeName) };
        if (FoundNode == mNodeNameLookup.end()) {
            return nullptr;
        }

        return &mNodes[FoundNode->second];
    }

    const std::vector<ModelNode>& Model::GetNodes() const {
        return mNodes;
    }

    bool Model::UploadVertexData(const asset::VertexAttributes& Vertices, Interface::IGraphicsAllocator* Allocator, Interface::ICopyQueue* CopyQueue, std::vector<std::byte>& OutRawData, std::vector<ModelNode::VertexAttributeRange>& OutRanges, std::unique_ptr<Interface::IAllocationHandle>& OutAllocation, std::vector<D3D12_VERTEX_BUFFER_VIEW>& OutViews) const {
        std::vector<AttributeUploadSource> Sources{};
        AppendAttributeSource(Sources, VertexAttributeKind::Position, Vertices.Positions.data(), Vertices.Positions.size() * sizeof(asset::Vec3), sizeof(asset::Vec3));
        AppendAttributeSource(Sources, VertexAttributeKind::Normal, Vertices.Normals.data(), Vertices.Normals.size() * sizeof(asset::Vec3), sizeof(asset::Vec3));
        AppendAttributeSource(Sources, VertexAttributeKind::TexCoord0, Vertices.TexCoords[0].data(), Vertices.TexCoords[0].size() * sizeof(asset::Vec2), sizeof(asset::Vec2));
        AppendAttributeSource(Sources, VertexAttributeKind::TexCoord1, Vertices.TexCoords[1].data(), Vertices.TexCoords[1].size() * sizeof(asset::Vec2), sizeof(asset::Vec2));
        AppendAttributeSource(Sources, VertexAttributeKind::TexCoord2, Vertices.TexCoords[2].data(), Vertices.TexCoords[2].size() * sizeof(asset::Vec2), sizeof(asset::Vec2));
        AppendAttributeSource(Sources, VertexAttributeKind::TexCoord3, Vertices.TexCoords[3].data(), Vertices.TexCoords[3].size() * sizeof(asset::Vec2), sizeof(asset::Vec2));
        AppendAttributeSource(Sources, VertexAttributeKind::Color, Vertices.Colors.data(), Vertices.Colors.size() * sizeof(asset::Vec4), sizeof(asset::Vec4));
        AppendAttributeSource(Sources, VertexAttributeKind::Tangent, Vertices.Tangents.data(), Vertices.Tangents.size() * sizeof(asset::Vec3), sizeof(asset::Vec3));
        AppendAttributeSource(Sources, VertexAttributeKind::Bitangent, Vertices.Bitangents.data(), Vertices.Bitangents.size() * sizeof(asset::Vec3), sizeof(asset::Vec3));
        AppendAttributeSource(Sources, VertexAttributeKind::BoneIndices, Vertices.BoneIndices.data(), Vertices.BoneIndices.size() * sizeof(asset::UVec4), sizeof(asset::UVec4));
        AppendAttributeSource(Sources, VertexAttributeKind::BoneWeights, Vertices.BoneWeights.data(), Vertices.BoneWeights.size() * sizeof(asset::Vec4), sizeof(asset::Vec4));

        if (Sources.empty()) {
            return false;
        }

        std::size_t TotalByteSize{ 0 };
        for (const AttributeUploadSource& Source : Sources) {
            TotalByteSize += Source.ByteSize;
        }

        const bool AllocateResult{ AllocateBufferResource(Allocator, TotalByteSize, OutAllocation) };
        if (AllocateResult == false) {
            return false;
        }

        OutRawData.resize(TotalByteSize);
        OutRanges.clear();
        OutViews.clear();
        OutRanges.reserve(Sources.size());
        OutViews.reserve(Sources.size());

        std::size_t CurrentOffset{ 0 };
        for (const AttributeUploadSource& Source : Sources) {
            std::memcpy(OutRawData.data() + CurrentOffset, Source.DataPointer, Source.ByteSize);

            ModelNode::VertexAttributeRange Range{};
            Range.Kind = Source.Kind;
            Range.Offset = CurrentOffset;
            Range.Size = Source.ByteSize;
            Range.StrideInBytes = Source.StrideInBytes;
            OutRanges.push_back(Range);

            D3D12_VERTEX_BUFFER_VIEW View{};
            View.BufferLocation = OutAllocation->GetResource()->GetGPUVirtualAddress() + CurrentOffset;
            View.SizeInBytes = static_cast<UINT>(Source.ByteSize);
            View.StrideInBytes = Source.StrideInBytes;
            OutViews.push_back(View);

            CurrentOffset += Source.ByteSize;
        }

        Interface::CopyQueueCopyRequest Request{};
        Request.DestinationDefaultResource = OutAllocation->GetResource();
        Request.DestinationOffset = 0;
        Request.SourceData = OutRawData;
        bool EnqueueResult{ CopyQueue->EnqueueCopy(Core::DX::CopyQueueId::Model, Request) };
        if (EnqueueResult == false) {
            return false;
        }
        return true;
    }

    bool Model::UploadIndexData(const std::vector<std::uint32_t>& Indices, Interface::IGraphicsAllocator* Allocator, Interface::ICopyQueue* CopyQueue, std::vector<std::byte>& OutRawData, std::unique_ptr<Interface::IAllocationHandle>& OutAllocation, D3D12_INDEX_BUFFER_VIEW& OutView) const {
        if (Indices.empty()) {
            OutView = D3D12_INDEX_BUFFER_VIEW{};
            return false;
        }

        const std::size_t ByteSize{ Indices.size() * sizeof(std::uint32_t) };
        const bool AllocateResult{ AllocateBufferResource(Allocator, ByteSize, OutAllocation) };
        if (AllocateResult == false) {
            OutView = D3D12_INDEX_BUFFER_VIEW{};
            return false;
        }

        OutRawData.resize(ByteSize);
        std::memcpy(OutRawData.data(), Indices.data(), ByteSize);

        Interface::CopyQueueCopyRequest Request{};
        Request.DestinationDefaultResource = OutAllocation->GetResource();
        Request.DestinationOffset = 0;
        Request.SourceData = OutRawData;
        bool EnqueueResult{ CopyQueue->EnqueueCopy(Core::DX::CopyQueueId::Model, Request) };
        if (EnqueueResult == false) {
            OutView = D3D12_INDEX_BUFFER_VIEW{};
            return false;
        }

        OutView.BufferLocation = OutAllocation->GetResource()->GetGPUVirtualAddress();
        OutView.SizeInBytes = static_cast<UINT>(ByteSize);
        OutView.Format = DXGI_FORMAT_R32_UINT;
        return true;
    }
}
