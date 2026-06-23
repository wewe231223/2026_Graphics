#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <DirectXTK12/SimpleMath.h>

#include "IPhysicsSpatialQuery.h"

class DynamicAabbTreePhysicsSpatialQuery final : public IPhysicsSpatialQuery {
public:
    static constexpr int InvalidNodeIndex{ -1 };

    struct AxisAlignedBounds final {
        DirectX::SimpleMath::Vector3 mMinimum{};
        DirectX::SimpleMath::Vector3 mMaximum{};
    };

    struct TreeNode final {
        PhysicsActorBase* mActor{};
        AxisAlignedBounds mBounds{};
        int mParent{ InvalidNodeIndex };
        int mLeft{ InvalidNodeIndex };
        int mRight{ InvalidNodeIndex };
        int mHeight{ -1 };
        std::uint64_t mSynchronizationVersion{};
    };

    DynamicAabbTreePhysicsSpatialQuery();
    ~DynamicAabbTreePhysicsSpatialQuery() override;
    DynamicAabbTreePhysicsSpatialQuery(const DynamicAabbTreePhysicsSpatialQuery& Other);
    DynamicAabbTreePhysicsSpatialQuery& operator=(const DynamicAabbTreePhysicsSpatialQuery& Other);
    DynamicAabbTreePhysicsSpatialQuery(DynamicAabbTreePhysicsSpatialQuery&& Other) noexcept;
    DynamicAabbTreePhysicsSpatialQuery& operator=(DynamicAabbTreePhysicsSpatialQuery&& Other) noexcept;

public:
    std::unique_ptr<IPhysicsSpatialQuery> Clone() const override;
    void Synchronize(IPhysicsActorRepository& ActorRepository) override;

    std::vector<PhysicsDynamicCollisionPairCandidate> QueryDynamicCollisionPairs(IPhysicsActorRepository& ActorRepository) const override;
    void QueryActorCollisionCandidates(IPhysicsActorRepository& ActorRepository, const PhysicsActorBase& Actor, std::vector<PhysicsActorBase*>& OutActors) const override;

private:
    int AllocateTreeNode();
    void FreeTreeNode(int NodeIndex);
    void InsertLeafNode(int LeafNodeIndex);
    void RemoveLeafNode(int LeafNodeIndex);
    int BalanceNode(int NodeIndex);
    void UpdateAncestors(int NodeIndex);
    void UpdateActorProxy(PhysicsActorBase& Actor);
    void RemoveStaleActorProxies();
    void QueryBounds(const AxisAlignedBounds& Bounds, std::vector<PhysicsActorBase*>& OutActors) const;
    void ResetSynchronizationVersionIfNeeded();

    bool IsLeafNode(int NodeIndex) const;
    bool ShouldCreateProxyForActor(const PhysicsActorBase& Actor) const;
    bool DoesNodeContainBounds(int NodeIndex, const AxisAlignedBounds& Bounds) const;
    AxisAlignedBounds BuildActorBounds(const PhysicsActorBase& Actor) const;
    AxisAlignedBounds MergeBounds(const AxisAlignedBounds& FirstBounds, const AxisAlignedBounds& SecondBounds) const;
    bool IsOverlappingBounds(const AxisAlignedBounds& FirstBounds, const AxisAlignedBounds& SecondBounds) const;
    float GetBoundsSurfaceArea(const AxisAlignedBounds& Bounds) const;

private:
    int mRootNodeIndex{ InvalidNodeIndex };
    std::uint64_t mSynchronizationVersion{};
    std::vector<TreeNode> mTreeNodes{};
    std::vector<int> mFreeNodeIndices{};
    std::unordered_map<PhysicsActorBase*, int> mNodeIndexByActor{};
    mutable std::vector<int> mQueryNodeIndices{};
};
