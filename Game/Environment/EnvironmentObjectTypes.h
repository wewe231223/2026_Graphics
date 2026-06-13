#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <DirectXCollision.h>
#include <DirectXTK12/SimpleMath.h>

#include "Game/Base/Common.h"
#include "Game/Model/AssetRegistryBackEnd.h"
#include "Game/Model/Model.h"

namespace Game {
    constexpr std::uint32_t InvalidEnvironmentObjectIndex{ 0xffffffffu };

    enum class EnvironmentObjectCellState : std::uint32_t {
        Unloaded,
        Generated,
        UploadPending,
        Resident,
        EvictPending
    };

    struct EnvironmentObjectCellKey final {
    public:
        std::int32_t mX{};
        std::int32_t mZ{};
    };

    bool operator==(const EnvironmentObjectCellKey& Left, const EnvironmentObjectCellKey& Right);
    bool operator!=(const EnvironmentObjectCellKey& Left, const EnvironmentObjectCellKey& Right);
    bool operator<(const EnvironmentObjectCellKey& Left, const EnvironmentObjectCellKey& Right);

    struct EnvironmentObjectCellKeyHasher final {
    public:
        std::size_t operator()(const EnvironmentObjectCellKey& Key) const;
    };

    struct EnvironmentObjectRenderSegment final {
    public:
        const Interface::IPipeline* mPipeline{};
        const Interface::IModelNode* mMesh{};
        SimpleMath::Matrix mLocalTransform{ SimpleMath::Matrix::Identity };
        SimpleMath::Vector4 mProceduralParameters{};
        DirectX::BoundingOrientedBox mLocalBoundingBox{};
        std::uint32_t mNodeIndex{};
        std::uint32_t mSubMeshIndex{};
        std::uint32_t mMaterialIndex{};
        std::uint32_t mPartIndex{};
        std::uint32_t mFlags{};
        RFD::EnvironmentDrawKind mDrawKind{ RFD::EnvironmentDrawKind::Model };
        bool mCastsShadow{ true };
        bool mHasLocalBoundingBox{};
    };

    enum class EnvironmentObjectPartKind : std::uint32_t {
        Model,
        CrossBillboard
    };

    struct EnvironmentObjectPart final {
    public:
        std::shared_ptr<Model> mModel{};
        SimpleMath::Matrix mLocalTransform{ SimpleMath::Matrix::Identity };
        SimpleMath::Vector4 mProceduralParameters{};
        DirectX::BoundingOrientedBox mLocalBoundingBox{};
        std::vector<EnvironmentObjectRenderSegment> mSegments{};
        std::uint32_t mMaterialGroupIndex{};
        std::uint32_t mFlags{};
        EnvironmentObjectPartKind mKind{ EnvironmentObjectPartKind::Model };
        bool mCastsShadow{ true };
        bool mHasLocalBoundingBox{};
    };

    struct EnvironmentObjectLod final {
    public:
        std::vector<EnvironmentObjectPart> mParts{};
        DirectX::BoundingOrientedBox mLocalBoundingBox{};
        float mMaximumDistance{};
        bool mHasLocalBoundingBox{};
    };

    struct EnvironmentObjectPrototype final {
    public:
        std::string mName{};
        std::vector<EnvironmentObjectLod> mLods{};
        DirectX::BoundingOrientedBox mLocalBoundingBox{};
        bool mHasLocalBoundingBox{};
    };

    struct EnvironmentObjectInstance final {
    public:
        SimpleMath::Vector3 mPosition{};
        float mYawRadians{};
        float mScale{ 1.0f };
        std::uint32_t mPrototypeIndex{ InvalidEnvironmentObjectIndex };
        std::uint32_t mVariation{};
    };

    struct EnvironmentObjectBatch final {
    public:
        const Interface::IPipeline* mPipeline{};
        const Interface::IModelNode* mMesh{};
        SimpleMath::Matrix mLocalTransform{ SimpleMath::Matrix::Identity };
        std::uint32_t mPrototypeIndex{};
        std::uint32_t mLodIndex{};
        std::uint32_t mPartIndex{};
        std::uint32_t mSegmentIndex{};
        std::uint32_t mNodeIndex{};
        std::uint32_t mSubMeshIndex{};
        std::uint32_t mMaterialIndex{};
        std::uint32_t mInstanceOffsetInCell{};
        std::uint32_t mInstanceCount{};
        std::uint32_t mFlags{};
        RFD::EnvironmentDrawKind mDrawKind{ RFD::EnvironmentDrawKind::Model };
        SimpleMath::Vector4 mProceduralParameters{};
        bool mCastsShadow{ true };
    };

    struct EnvironmentObjectCell final {
    public:
        EnvironmentObjectCellKey mKey{};
        DirectX::BoundingOrientedBox mWorldBoundingBox{};
        std::vector<EnvironmentObjectInstance> mInstances{};
        std::vector<std::vector<EnvironmentObjectBatch>> mBatchesByLodLevel{};
        EnvironmentObjectCellState mState{ EnvironmentObjectCellState::Unloaded };
        std::uint64_t mGenerationVersion{};
        std::uint64_t mLastTouchedFrame{};
        bool mHasWorldBoundingBox{};
    };

    SimpleMath::Matrix BuildEnvironmentObjectInstanceWorldMatrix(const EnvironmentObjectInstance& Instance);
    void RebuildEnvironmentObjectPrototypeRenderData(EnvironmentObjectPrototype& Prototype, const std::vector<RegisteredMaterialGroup>& MaterialGroups);
    void RebuildEnvironmentObjectCellBatches(EnvironmentObjectCell& Cell, const std::vector<EnvironmentObjectPrototype>& Prototypes);
    void RefreshEnvironmentObjectCellWorldBoundingBox(EnvironmentObjectCell& Cell, const std::vector<EnvironmentObjectPrototype>& Prototypes);
}
