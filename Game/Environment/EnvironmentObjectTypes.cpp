#include "EnvironmentObjectTypes.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace Game {
    namespace {
        struct EnvironmentObjectPrototypeInstanceRange final {
        public:
            std::uint32_t mPrototypeIndex{};
            std::uint32_t mInstanceOffsetInCell{};
            std::uint32_t mInstanceCount{};
        };

        const RegisteredMaterialGroup* ResolveRegisteredMaterialGroup(std::uint32_t MaterialGroupIndex, const std::vector<RegisteredMaterialGroup>& MaterialGroups) {
            if (MaterialGroups.empty() == true) {
                return nullptr;
            }

            std::uint32_t ResolvedMaterialGroupIndex{ MaterialGroupIndex };
            if (ResolvedMaterialGroupIndex >= MaterialGroups.size() || MaterialGroups[ResolvedMaterialGroupIndex].Items.empty() == true) {
                ResolvedMaterialGroupIndex = 0u;
            }

            if (ResolvedMaterialGroupIndex >= MaterialGroups.size() || MaterialGroups[ResolvedMaterialGroupIndex].Items.empty() == true) {
                return nullptr;
            }

            return &MaterialGroups[ResolvedMaterialGroupIndex];
        }

        RegisteredMaterialGroupItem ResolveRegisteredMaterialGroupItem(const RegisteredMaterialGroup* MaterialGroup, std::size_t MaterialGroupItemIndex) {
            RegisteredMaterialGroupItem Result{};
            if (MaterialGroup == nullptr || MaterialGroup->Items.empty() == true) {
                return Result;
            }

            std::size_t ResolvedItemIndex{ MaterialGroupItemIndex };
            if (ResolvedItemIndex >= MaterialGroup->Items.size()) {
                ResolvedItemIndex = 0ULL;
            }

            if (ResolvedItemIndex >= MaterialGroup->Items.size()) {
                return Result;
            }

            return MaterialGroup->Items[ResolvedItemIndex];
        }

        bool TryResolveModelRootNodeIndex(const Model& ModelValue, std::uint32_t& OutRootNodeIndex) {
            const ModelNode* RootNode{ ModelValue.GetRootNode() };
            const std::vector<ModelNode>& Nodes{ ModelValue.GetNodes() };
            if (RootNode == nullptr || Nodes.empty() == true) {
                return false;
            }

            OutRootNodeIndex = static_cast<std::uint32_t>(RootNode - Nodes.data());
            return OutRootNodeIndex < Nodes.size();
        }

        void ResolveModelNodeLocalTransformsRecursive(const std::vector<ModelNode>& Nodes, std::uint32_t NodeIndex, const SimpleMath::Matrix& ParentTransform, std::vector<SimpleMath::Matrix>& OutNodeLocalTransforms) {
            if (NodeIndex >= Nodes.size()) {
                return;
            }

            const ModelNode& Node{ Nodes[NodeIndex] };
            const SimpleMath::Matrix NodeLocalTransform{ Node.GetNodeToParent() * ParentTransform };
            OutNodeLocalTransforms[NodeIndex] = NodeLocalTransform;

            for (std::uint32_t ChildNodeIndex : Node.GetChildren()) {
                ResolveModelNodeLocalTransformsRecursive(Nodes, ChildNodeIndex, NodeLocalTransform, OutNodeLocalTransforms);
            }
        }

        std::vector<SimpleMath::Matrix> BuildModelNodeLocalTransforms(const Model& ModelValue) {
            const std::vector<ModelNode>& Nodes{ ModelValue.GetNodes() };
            std::vector<SimpleMath::Matrix> NodeLocalTransforms{};
            NodeLocalTransforms.resize(Nodes.size(), SimpleMath::Matrix::Identity);

            std::uint32_t RootNodeIndex{};
            const bool HasRootNode{ TryResolveModelRootNodeIndex(ModelValue, RootNodeIndex) };
            if (HasRootNode == false) {
                return NodeLocalTransforms;
            }

            ResolveModelNodeLocalTransformsRecursive(Nodes, RootNodeIndex, SimpleMath::Matrix::Identity, NodeLocalTransforms);
            return NodeLocalTransforms;
        }

        void IncludePoint(SimpleMath::Vector3& InOutMinimumPoint, SimpleMath::Vector3& InOutMaximumPoint, const SimpleMath::Vector3& Point) {
            InOutMinimumPoint.x = std::min(InOutMinimumPoint.x, Point.x);
            InOutMinimumPoint.y = std::min(InOutMinimumPoint.y, Point.y);
            InOutMinimumPoint.z = std::min(InOutMinimumPoint.z, Point.z);
            InOutMaximumPoint.x = std::max(InOutMaximumPoint.x, Point.x);
            InOutMaximumPoint.y = std::max(InOutMaximumPoint.y, Point.y);
            InOutMaximumPoint.z = std::max(InOutMaximumPoint.z, Point.z);
        }

        void IncludeBoundingBox(SimpleMath::Vector3& InOutMinimumPoint, SimpleMath::Vector3& InOutMaximumPoint, bool& InOutHasPoint, const DirectX::BoundingOrientedBox& BoundingBox, const SimpleMath::Matrix& Transform) {
            std::array<DirectX::XMFLOAT3, DirectX::BoundingOrientedBox::CORNER_COUNT> Corners{};
            BoundingBox.GetCorners(Corners.data());

            for (const DirectX::XMFLOAT3& Corner : Corners) {
                const SimpleMath::Vector3 TransformedPoint{ SimpleMath::Vector3::Transform(SimpleMath::Vector3{ Corner.x, Corner.y, Corner.z }, Transform) };
                if (InOutHasPoint == false) {
                    InOutMinimumPoint = TransformedPoint;
                    InOutMaximumPoint = TransformedPoint;
                    InOutHasPoint = true;
                    continue;
                }

                IncludePoint(InOutMinimumPoint, InOutMaximumPoint, TransformedPoint);
            }
        }

        DirectX::BoundingOrientedBox BuildAxisAlignedOrientedBox(const SimpleMath::Vector3& MinimumPoint, const SimpleMath::Vector3& MaximumPoint) {
            DirectX::BoundingOrientedBox BoundingBox{};
            const SimpleMath::Vector3 Center{ (MinimumPoint + MaximumPoint) * 0.5f };
            const SimpleMath::Vector3 Extents{ (MaximumPoint - MinimumPoint) * 0.5f };
            BoundingBox.Center = DirectX::XMFLOAT3{ Center.x, Center.y, Center.z };
            BoundingBox.Extents = DirectX::XMFLOAT3{ std::max(Extents.x, 0.0f), std::max(Extents.y, 0.0f), std::max(Extents.z, 0.0f) };
            BoundingBox.Orientation = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f };
            return BoundingBox;
        }

        void RefreshEnvironmentObjectPartRenderData(EnvironmentObjectPart& Part, std::uint32_t PartIndex, const std::vector<RegisteredMaterialGroup>& MaterialGroups) {
            Part.mSegments.clear();
            Part.mHasLocalBoundingBox = false;

            if (Part.mModel == nullptr) {
                return;
            }

            const std::vector<ModelNode>& Nodes{ Part.mModel->GetNodes() };
            const std::vector<SimpleMath::Matrix> NodeLocalTransforms{ BuildModelNodeLocalTransforms(*Part.mModel) };
            const RegisteredMaterialGroup* MaterialGroup{ ResolveRegisteredMaterialGroup(Part.mMaterialGroupIndex, MaterialGroups) };
            SimpleMath::Vector3 MinimumPoint{};
            SimpleMath::Vector3 MaximumPoint{};
            bool HasBoundingPoint{};

            for (std::uint32_t NodeIndex{}; NodeIndex < Nodes.size(); NodeIndex += 1u) {
                const ModelNode& Node{ Nodes[NodeIndex] };
                const std::vector<ModelSubMesh>& SubMeshes{ Node.GetSubMeshes() };
                if (SubMeshes.empty() == true || Node.IsSkinnedMesh() == true) {
                    continue;
                }

                const SimpleMath::Matrix SegmentLocalTransform{ NodeLocalTransforms[NodeIndex] * Part.mLocalTransform };
                for (std::uint32_t SubMeshIndex{}; SubMeshIndex < SubMeshes.size(); SubMeshIndex += 1u) {
                    const RegisteredMaterialGroupItem MaterialGroupItem{ ResolveRegisteredMaterialGroupItem(MaterialGroup, SubMeshes[SubMeshIndex].MaterialGroupItemIndex) };
                    EnvironmentObjectRenderSegment Segment{};
                    Segment.mPipeline = MaterialGroupItem.Pipeline;
                    Segment.mMesh = &Node;
                    Segment.mLocalTransform = SegmentLocalTransform;
                    Segment.mNodeIndex = NodeIndex;
                    Segment.mSubMeshIndex = SubMeshIndex;
                    Segment.mMaterialIndex = MaterialGroupItem.MaterialIndex;
                    Segment.mPartIndex = PartIndex;
                    Segment.mFlags = Part.mFlags;
                    Segment.mCastsShadow = Part.mCastsShadow;

                    if (Node.HasBoundingBox() == true) {
                        Segment.mLocalBoundingBox = Node.GetBoundingBox();
                        Segment.mLocalBoundingBox.Transform(Segment.mLocalBoundingBox, SegmentLocalTransform);
                        Segment.mHasLocalBoundingBox = true;
                        IncludeBoundingBox(MinimumPoint, MaximumPoint, HasBoundingPoint, Segment.mLocalBoundingBox, SimpleMath::Matrix::Identity);
                    }

                    Part.mSegments.push_back(Segment);
                }
            }

            if (HasBoundingPoint == true) {
                Part.mLocalBoundingBox = BuildAxisAlignedOrientedBox(MinimumPoint, MaximumPoint);
                Part.mHasLocalBoundingBox = true;
            }
        }

        void RefreshEnvironmentObjectLodBoundingBox(EnvironmentObjectLod& Lod) {
            Lod.mHasLocalBoundingBox = false;
            SimpleMath::Vector3 MinimumPoint{};
            SimpleMath::Vector3 MaximumPoint{};
            bool HasBoundingPoint{};

            for (const EnvironmentObjectPart& Part : Lod.mParts) {
                if (Part.mHasLocalBoundingBox == false) {
                    continue;
                }

                IncludeBoundingBox(MinimumPoint, MaximumPoint, HasBoundingPoint, Part.mLocalBoundingBox, SimpleMath::Matrix::Identity);
            }

            if (HasBoundingPoint == true) {
                Lod.mLocalBoundingBox = BuildAxisAlignedOrientedBox(MinimumPoint, MaximumPoint);
                Lod.mHasLocalBoundingBox = true;
            }
        }

        void RefreshEnvironmentObjectPrototypeBoundingBox(EnvironmentObjectPrototype& Prototype) {
            Prototype.mHasLocalBoundingBox = false;
            SimpleMath::Vector3 MinimumPoint{};
            SimpleMath::Vector3 MaximumPoint{};
            bool HasBoundingPoint{};

            for (const EnvironmentObjectLod& Lod : Prototype.mLods) {
                if (Lod.mHasLocalBoundingBox == false) {
                    continue;
                }

                IncludeBoundingBox(MinimumPoint, MaximumPoint, HasBoundingPoint, Lod.mLocalBoundingBox, SimpleMath::Matrix::Identity);
            }

            if (HasBoundingPoint == true) {
                Prototype.mLocalBoundingBox = BuildAxisAlignedOrientedBox(MinimumPoint, MaximumPoint);
                Prototype.mHasLocalBoundingBox = true;
            }
        }

        std::vector<EnvironmentObjectPrototypeInstanceRange> BuildPrototypeInstanceRanges(const EnvironmentObjectCell& Cell, const std::vector<EnvironmentObjectPrototype>& Prototypes) {
            std::vector<EnvironmentObjectPrototypeInstanceRange> Ranges{};
            std::uint32_t InstanceIndex{};

            while (InstanceIndex < Cell.mInstances.size()) {
                const std::uint32_t PrototypeIndex{ Cell.mInstances[InstanceIndex].mPrototypeIndex };
                const std::uint32_t RangeStart{ InstanceIndex };
                while (InstanceIndex < Cell.mInstances.size() && Cell.mInstances[InstanceIndex].mPrototypeIndex == PrototypeIndex) {
                    InstanceIndex += 1u;
                }

                if (PrototypeIndex >= Prototypes.size() || Prototypes[PrototypeIndex].mLods.empty() == true) {
                    continue;
                }

                EnvironmentObjectPrototypeInstanceRange Range{};
                Range.mPrototypeIndex = PrototypeIndex;
                Range.mInstanceOffsetInCell = RangeStart;
                Range.mInstanceCount = InstanceIndex - RangeStart;
                Ranges.push_back(Range);
            }

            return Ranges;
        }

        std::size_t ResolveMaximumLodLevelCount(const std::vector<EnvironmentObjectPrototypeInstanceRange>& Ranges, const std::vector<EnvironmentObjectPrototype>& Prototypes) {
            std::size_t MaximumLodLevelCount{};
            for (const EnvironmentObjectPrototypeInstanceRange& Range : Ranges) {
                if (Range.mPrototypeIndex >= Prototypes.size()) {
                    continue;
                }

                MaximumLodLevelCount = std::max(MaximumLodLevelCount, Prototypes[Range.mPrototypeIndex].mLods.size());
            }

            return MaximumLodLevelCount;
        }

        void AppendEnvironmentObjectBatch(const EnvironmentObjectRenderSegment& Segment, const EnvironmentObjectPrototypeInstanceRange& Range, std::uint32_t LodIndex, std::uint32_t SegmentIndex, std::vector<EnvironmentObjectBatch>& OutBatches) {
            EnvironmentObjectBatch Batch{};
            Batch.mPipeline = Segment.mPipeline;
            Batch.mMesh = Segment.mMesh;
            Batch.mLocalTransform = Segment.mLocalTransform;
            Batch.mPrototypeIndex = Range.mPrototypeIndex;
            Batch.mLodIndex = LodIndex;
            Batch.mPartIndex = Segment.mPartIndex;
            Batch.mSegmentIndex = SegmentIndex;
            Batch.mNodeIndex = Segment.mNodeIndex;
            Batch.mSubMeshIndex = Segment.mSubMeshIndex;
            Batch.mMaterialIndex = Segment.mMaterialIndex;
            Batch.mInstanceOffsetInCell = Range.mInstanceOffsetInCell;
            Batch.mInstanceCount = Range.mInstanceCount;
            Batch.mFlags = Segment.mFlags;
            Batch.mCastsShadow = Segment.mCastsShadow;
            OutBatches.push_back(Batch);
        }

        void AppendEnvironmentObjectBatchesForRange(const EnvironmentObjectPrototypeInstanceRange& Range, std::size_t LodLevel, const std::vector<EnvironmentObjectPrototype>& Prototypes, std::vector<EnvironmentObjectBatch>& OutBatches) {
            if (Range.mPrototypeIndex >= Prototypes.size()) {
                return;
            }

            const EnvironmentObjectPrototype& Prototype{ Prototypes[Range.mPrototypeIndex] };
            if (Prototype.mLods.empty() == true) {
                return;
            }

            const std::uint32_t LodIndex{ static_cast<std::uint32_t>(std::min(LodLevel, Prototype.mLods.size() - 1ULL)) };
            const EnvironmentObjectLod& Lod{ Prototype.mLods[LodIndex] };
            for (std::uint32_t PartIndex{}; PartIndex < Lod.mParts.size(); PartIndex += 1u) {
                const EnvironmentObjectPart& Part{ Lod.mParts[PartIndex] };
                for (std::uint32_t SegmentIndex{}; SegmentIndex < Part.mSegments.size(); SegmentIndex += 1u) {
                    AppendEnvironmentObjectBatch(Part.mSegments[SegmentIndex], Range, LodIndex, SegmentIndex, OutBatches);
                }
            }
        }
    }

    bool operator==(const EnvironmentObjectCellKey& Left, const EnvironmentObjectCellKey& Right) {
        return Left.mX == Right.mX && Left.mZ == Right.mZ;
    }

    bool operator!=(const EnvironmentObjectCellKey& Left, const EnvironmentObjectCellKey& Right) {
        return (Left == Right) == false;
    }

    bool operator<(const EnvironmentObjectCellKey& Left, const EnvironmentObjectCellKey& Right) {
        if (Left.mZ != Right.mZ) {
            return Left.mZ < Right.mZ;
        }

        return Left.mX < Right.mX;
    }

    std::size_t EnvironmentObjectCellKeyHasher::operator()(const EnvironmentObjectCellKey& Key) const {
        std::uint32_t Hash{ 2166136261u };
        Hash = (Hash ^ static_cast<std::uint32_t>(Key.mX)) * 16777619u;
        Hash = (Hash ^ static_cast<std::uint32_t>(Key.mZ)) * 16777619u;
        return static_cast<std::size_t>(Hash);
    }

    SimpleMath::Matrix BuildEnvironmentObjectInstanceWorldMatrix(const EnvironmentObjectInstance& Instance) {
        const SimpleMath::Matrix ScaleMatrix{ SimpleMath::Matrix::CreateScale(Instance.mScale) };
        const SimpleMath::Matrix RotationMatrix{ SimpleMath::Matrix::CreateRotationY(Instance.mYawRadians) };
        const SimpleMath::Matrix TranslationMatrix{ SimpleMath::Matrix::CreateTranslation(Instance.mPosition) };
        return ScaleMatrix * RotationMatrix * TranslationMatrix;
    }

    void RebuildEnvironmentObjectPrototypeRenderData(EnvironmentObjectPrototype& Prototype, const std::vector<RegisteredMaterialGroup>& MaterialGroups) {
        for (EnvironmentObjectLod& Lod : Prototype.mLods) {
            for (std::uint32_t PartIndex{}; PartIndex < Lod.mParts.size(); PartIndex += 1u) {
                RefreshEnvironmentObjectPartRenderData(Lod.mParts[PartIndex], PartIndex, MaterialGroups);
            }

            RefreshEnvironmentObjectLodBoundingBox(Lod);
        }

        RefreshEnvironmentObjectPrototypeBoundingBox(Prototype);
    }

    void RebuildEnvironmentObjectCellBatches(EnvironmentObjectCell& Cell, const std::vector<EnvironmentObjectPrototype>& Prototypes) {
        std::stable_sort(Cell.mInstances.begin(), Cell.mInstances.end(), [](const EnvironmentObjectInstance& Left, const EnvironmentObjectInstance& Right) {
            if (Left.mPrototypeIndex != Right.mPrototypeIndex) {
                return Left.mPrototypeIndex < Right.mPrototypeIndex;
            }

            return Left.mVariation < Right.mVariation;
        });

        const std::vector<EnvironmentObjectPrototypeInstanceRange> Ranges{ BuildPrototypeInstanceRanges(Cell, Prototypes) };
        const std::size_t MaximumLodLevelCount{ ResolveMaximumLodLevelCount(Ranges, Prototypes) };
        Cell.mBatchesByLodLevel.clear();
        Cell.mBatchesByLodLevel.resize(MaximumLodLevelCount);

        for (std::size_t LodLevel{}; LodLevel < MaximumLodLevelCount; LodLevel += 1ULL) {
            std::vector<EnvironmentObjectBatch>& Batches{ Cell.mBatchesByLodLevel[LodLevel] };
            for (const EnvironmentObjectPrototypeInstanceRange& Range : Ranges) {
                AppendEnvironmentObjectBatchesForRange(Range, LodLevel, Prototypes, Batches);
            }
        }

        RefreshEnvironmentObjectCellWorldBoundingBox(Cell, Prototypes);
        if (Cell.mState == EnvironmentObjectCellState::Unloaded && Cell.mInstances.empty() == false) {
            Cell.mState = EnvironmentObjectCellState::Generated;
        }
    }

    void RefreshEnvironmentObjectCellWorldBoundingBox(EnvironmentObjectCell& Cell, const std::vector<EnvironmentObjectPrototype>& Prototypes) {
        Cell.mHasWorldBoundingBox = false;
        SimpleMath::Vector3 MinimumPoint{};
        SimpleMath::Vector3 MaximumPoint{};
        bool HasBoundingPoint{};

        for (const EnvironmentObjectInstance& Instance : Cell.mInstances) {
            if (Instance.mPrototypeIndex >= Prototypes.size()) {
                continue;
            }

            const EnvironmentObjectPrototype& Prototype{ Prototypes[Instance.mPrototypeIndex] };
            if (Prototype.mHasLocalBoundingBox == false) {
                continue;
            }

            IncludeBoundingBox(MinimumPoint, MaximumPoint, HasBoundingPoint, Prototype.mLocalBoundingBox, BuildEnvironmentObjectInstanceWorldMatrix(Instance));
        }

        if (HasBoundingPoint == true) {
            Cell.mWorldBoundingBox = BuildAxisAlignedOrientedBox(MinimumPoint, MaximumPoint);
            Cell.mHasWorldBoundingBox = true;
        }
    }
}
