#include "DynamicAabbTreePhysicsSpatialQuery.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <utility>

#include "PhysicsLib/Simulation/Repository/IPhysicsActorRepository.h"

#undef max
#undef min

DynamicAabbTreePhysicsSpatialQuery::DynamicAabbTreePhysicsSpatialQuery()
    : mRootNodeIndex{ InvalidNodeIndex },
      mSynchronizationVersion{},
      mTreeNodes{},
      mFreeNodeIndices{},
      mNodeIndexByActor{},
      mQueryNodeIndices{} {
}

DynamicAabbTreePhysicsSpatialQuery::~DynamicAabbTreePhysicsSpatialQuery() {
}

DynamicAabbTreePhysicsSpatialQuery::DynamicAabbTreePhysicsSpatialQuery(const DynamicAabbTreePhysicsSpatialQuery& Other)
    : IPhysicsSpatialQuery{ Other },
      mRootNodeIndex{ InvalidNodeIndex },
      mSynchronizationVersion{},
      mTreeNodes{},
      mFreeNodeIndices{},
      mNodeIndexByActor{},
      mQueryNodeIndices{} {
}

DynamicAabbTreePhysicsSpatialQuery& DynamicAabbTreePhysicsSpatialQuery::operator=(const DynamicAabbTreePhysicsSpatialQuery& Other) {
    if (this == &Other) {
        return *this;
    }

    IPhysicsSpatialQuery::operator=(Other);
    mRootNodeIndex = InvalidNodeIndex;
    mSynchronizationVersion = 0U;
    mTreeNodes.clear();
    mFreeNodeIndices.clear();
    mNodeIndexByActor.clear();
    mQueryNodeIndices.clear();
    return *this;
}

DynamicAabbTreePhysicsSpatialQuery::DynamicAabbTreePhysicsSpatialQuery(DynamicAabbTreePhysicsSpatialQuery&& Other) noexcept
    : IPhysicsSpatialQuery{ std::move(Other) },
      mRootNodeIndex{ Other.mRootNodeIndex },
      mSynchronizationVersion{ Other.mSynchronizationVersion },
      mTreeNodes{ std::move(Other.mTreeNodes) },
      mFreeNodeIndices{ std::move(Other.mFreeNodeIndices) },
      mNodeIndexByActor{ std::move(Other.mNodeIndexByActor) },
      mQueryNodeIndices{ std::move(Other.mQueryNodeIndices) } {
    Other.mRootNodeIndex = InvalidNodeIndex;
    Other.mSynchronizationVersion = 0U;
}

DynamicAabbTreePhysicsSpatialQuery& DynamicAabbTreePhysicsSpatialQuery::operator=(DynamicAabbTreePhysicsSpatialQuery&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    IPhysicsSpatialQuery::operator=(std::move(Other));
    mRootNodeIndex = Other.mRootNodeIndex;
    mSynchronizationVersion = Other.mSynchronizationVersion;
    mTreeNodes = std::move(Other.mTreeNodes);
    mFreeNodeIndices = std::move(Other.mFreeNodeIndices);
    mNodeIndexByActor = std::move(Other.mNodeIndexByActor);
    mQueryNodeIndices = std::move(Other.mQueryNodeIndices);
    Other.mRootNodeIndex = InvalidNodeIndex;
    Other.mSynchronizationVersion = 0U;
    return *this;
}

std::unique_ptr<IPhysicsSpatialQuery> DynamicAabbTreePhysicsSpatialQuery::Clone() const {
    std::unique_ptr<IPhysicsSpatialQuery> ClonedQuery{ std::make_unique<DynamicAabbTreePhysicsSpatialQuery>(*this) };
    return ClonedQuery;
}

void DynamicAabbTreePhysicsSpatialQuery::Synchronize(IPhysicsActorRepository& ActorRepository) {
    ResetSynchronizationVersionIfNeeded();
    const std::size_t ActorCount{ ActorRepository.GetActorCount() };

    for (std::size_t ActorIndex{}; ActorIndex < ActorCount; ++ActorIndex) {
        PhysicsActorBase* Actor{ ActorRepository.GetActor(ActorIndex) };
        if (Actor == nullptr || ShouldCreateProxyForActor(*Actor) == false) {
            continue;
        }

        UpdateActorProxy(*Actor);
    }

    RemoveStaleActorProxies();
}

std::vector<PhysicsDynamicCollisionPairCandidate> DynamicAabbTreePhysicsSpatialQuery::QueryDynamicCollisionPairs(IPhysicsActorRepository& ActorRepository) const {
    (void)ActorRepository;

    std::vector<PhysicsDynamicCollisionPairCandidate> PairCandidates{};
    std::vector<PhysicsActorBase*> CandidateActors{};
    CandidateActors.reserve(16U);
    const std::less<PhysicsActorBase*> ActorOrder{};

    for (const std::pair<PhysicsActorBase* const, int>& ActorNodePair : mNodeIndexByActor) {
        PhysicsActorBase* FirstActor{ ActorNodePair.first };
        const int FirstNodeIndex{ ActorNodePair.second };
        if (FirstActor == nullptr || FirstNodeIndex == InvalidNodeIndex || FirstActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic || FirstActor->GetIsActive() == false || FirstActor->GetInverseMass() <= 0.0F) {
            continue;
        }

        CandidateActors.clear();
        QueryBounds(mTreeNodes[FirstNodeIndex].mBounds, CandidateActors);
        for (PhysicsActorBase* SecondActor : CandidateActors) {
            if (SecondActor == nullptr || SecondActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic || SecondActor->GetIsActive() == false || SecondActor->GetInverseMass() <= 0.0F || ActorOrder(FirstActor, SecondActor) == false) {
                continue;
            }

            PairCandidates.push_back(PhysicsDynamicCollisionPairCandidate{ static_cast<PhysicsDynamicActor*>(FirstActor), static_cast<PhysicsDynamicActor*>(SecondActor) });
        }
    }

    return PairCandidates;
}

void DynamicAabbTreePhysicsSpatialQuery::QueryActorCollisionCandidates(IPhysicsActorRepository& ActorRepository, const PhysicsActorBase& Actor, std::vector<PhysicsActorBase*>& OutActors) const {
    (void)ActorRepository;

    OutActors.clear();
    if (ShouldCreateProxyForActor(Actor) == false) {
        return;
    }

    const AxisAlignedBounds ActorBounds{ BuildActorBounds(Actor) };
    QueryBounds(ActorBounds, OutActors);
    const auto NewEndIterator{ std::remove_if(OutActors.begin(), OutActors.end(), [&Actor](const PhysicsActorBase* CandidateActor) { return CandidateActor == &Actor; }) };
    OutActors.erase(NewEndIterator, OutActors.end());
}

int DynamicAabbTreePhysicsSpatialQuery::AllocateTreeNode() {
    if (mFreeNodeIndices.empty() == false) {
        const int NodeIndex{ mFreeNodeIndices.back() };
        mFreeNodeIndices.pop_back();
        mTreeNodes[NodeIndex] = TreeNode{};
        return NodeIndex;
    }

    const int NodeIndex{ static_cast<int>(mTreeNodes.size()) };
    mTreeNodes.push_back(TreeNode{});
    return NodeIndex;
}

void DynamicAabbTreePhysicsSpatialQuery::FreeTreeNode(int NodeIndex) {
    if (NodeIndex == InvalidNodeIndex || NodeIndex < 0 || static_cast<std::size_t>(NodeIndex) >= mTreeNodes.size()) {
        return;
    }

    mTreeNodes[NodeIndex] = TreeNode{};
    mFreeNodeIndices.push_back(NodeIndex);
}

void DynamicAabbTreePhysicsSpatialQuery::InsertLeafNode(int LeafNodeIndex) {
    if (LeafNodeIndex == InvalidNodeIndex) {
        return;
    }

    if (mRootNodeIndex == InvalidNodeIndex) {
        mRootNodeIndex = LeafNodeIndex;
        mTreeNodes[LeafNodeIndex].mParent = InvalidNodeIndex;
        return;
    }

    const AxisAlignedBounds LeafBounds{ mTreeNodes[LeafNodeIndex].mBounds };
    int SiblingNodeIndex{ mRootNodeIndex };
    while (IsLeafNode(SiblingNodeIndex) == false) {
        const int LeftNodeIndex{ mTreeNodes[SiblingNodeIndex].mLeft };
        const int RightNodeIndex{ mTreeNodes[SiblingNodeIndex].mRight };
        const AxisAlignedBounds CombinedBounds{ MergeBounds(mTreeNodes[SiblingNodeIndex].mBounds, LeafBounds) };
        const float InheritedCost{ GetBoundsSurfaceArea(CombinedBounds) - GetBoundsSurfaceArea(mTreeNodes[SiblingNodeIndex].mBounds) };
        const AxisAlignedBounds LeftCombinedBounds{ MergeBounds(mTreeNodes[LeftNodeIndex].mBounds, LeafBounds) };
        const AxisAlignedBounds RightCombinedBounds{ MergeBounds(mTreeNodes[RightNodeIndex].mBounds, LeafBounds) };
        const float LeftCost{ GetBoundsSurfaceArea(LeftCombinedBounds) - GetBoundsSurfaceArea(mTreeNodes[LeftNodeIndex].mBounds) + InheritedCost };
        const float RightCost{ GetBoundsSurfaceArea(RightCombinedBounds) - GetBoundsSurfaceArea(mTreeNodes[RightNodeIndex].mBounds) + InheritedCost };
        SiblingNodeIndex = LeftCost <= RightCost ? LeftNodeIndex : RightNodeIndex;
    }

    const int PreviousParentNodeIndex{ mTreeNodes[SiblingNodeIndex].mParent };
    const int NewParentNodeIndex{ AllocateTreeNode() };
    TreeNode& NewParentNode{ mTreeNodes[NewParentNodeIndex] };
    NewParentNode.mParent = PreviousParentNodeIndex;
    NewParentNode.mBounds = MergeBounds(LeafBounds, mTreeNodes[SiblingNodeIndex].mBounds);
    NewParentNode.mHeight = mTreeNodes[SiblingNodeIndex].mHeight + 1;
    NewParentNode.mLeft = SiblingNodeIndex;
    NewParentNode.mRight = LeafNodeIndex;
    mTreeNodes[SiblingNodeIndex].mParent = NewParentNodeIndex;
    mTreeNodes[LeafNodeIndex].mParent = NewParentNodeIndex;

    if (PreviousParentNodeIndex == InvalidNodeIndex) {
        mRootNodeIndex = NewParentNodeIndex;
    } else if (mTreeNodes[PreviousParentNodeIndex].mLeft == SiblingNodeIndex) {
        mTreeNodes[PreviousParentNodeIndex].mLeft = NewParentNodeIndex;
    } else {
        mTreeNodes[PreviousParentNodeIndex].mRight = NewParentNodeIndex;
    }

    UpdateAncestors(NewParentNodeIndex);
}

void DynamicAabbTreePhysicsSpatialQuery::RemoveLeafNode(int LeafNodeIndex) {
    if (LeafNodeIndex == InvalidNodeIndex || LeafNodeIndex == mRootNodeIndex) {
        if (LeafNodeIndex == mRootNodeIndex) {
            mRootNodeIndex = InvalidNodeIndex;
        }

        return;
    }

    const int ParentNodeIndex{ mTreeNodes[LeafNodeIndex].mParent };
    if (ParentNodeIndex == InvalidNodeIndex) {
        return;
    }

    const int GrandParentNodeIndex{ mTreeNodes[ParentNodeIndex].mParent };
    const int SiblingNodeIndex{ mTreeNodes[ParentNodeIndex].mLeft == LeafNodeIndex ? mTreeNodes[ParentNodeIndex].mRight : mTreeNodes[ParentNodeIndex].mLeft };
    if (GrandParentNodeIndex == InvalidNodeIndex) {
        mRootNodeIndex = SiblingNodeIndex;
        mTreeNodes[SiblingNodeIndex].mParent = InvalidNodeIndex;
        FreeTreeNode(ParentNodeIndex);
        mTreeNodes[LeafNodeIndex].mParent = InvalidNodeIndex;
        return;
    }

    if (mTreeNodes[GrandParentNodeIndex].mLeft == ParentNodeIndex) {
        mTreeNodes[GrandParentNodeIndex].mLeft = SiblingNodeIndex;
    } else {
        mTreeNodes[GrandParentNodeIndex].mRight = SiblingNodeIndex;
    }

    mTreeNodes[SiblingNodeIndex].mParent = GrandParentNodeIndex;
    FreeTreeNode(ParentNodeIndex);
    mTreeNodes[LeafNodeIndex].mParent = InvalidNodeIndex;
    UpdateAncestors(GrandParentNodeIndex);
}

int DynamicAabbTreePhysicsSpatialQuery::BalanceNode(int NodeIndex) {
    if (NodeIndex == InvalidNodeIndex || IsLeafNode(NodeIndex) || mTreeNodes[NodeIndex].mHeight < 2) {
        return NodeIndex;
    }

    const int LeftNodeIndex{ mTreeNodes[NodeIndex].mLeft };
    const int RightNodeIndex{ mTreeNodes[NodeIndex].mRight };
    const int HeightDifference{ mTreeNodes[RightNodeIndex].mHeight - mTreeNodes[LeftNodeIndex].mHeight };
    if (HeightDifference > 1) {
        const int RightLeftNodeIndex{ mTreeNodes[RightNodeIndex].mLeft };
        const int RightRightNodeIndex{ mTreeNodes[RightNodeIndex].mRight };
        const int ParentNodeIndex{ mTreeNodes[NodeIndex].mParent };
        mTreeNodes[RightNodeIndex].mLeft = NodeIndex;
        mTreeNodes[RightNodeIndex].mParent = ParentNodeIndex;
        mTreeNodes[NodeIndex].mParent = RightNodeIndex;

        if (ParentNodeIndex == InvalidNodeIndex) {
            mRootNodeIndex = RightNodeIndex;
        } else if (mTreeNodes[ParentNodeIndex].mLeft == NodeIndex) {
            mTreeNodes[ParentNodeIndex].mLeft = RightNodeIndex;
        } else {
            mTreeNodes[ParentNodeIndex].mRight = RightNodeIndex;
        }

        if (mTreeNodes[RightLeftNodeIndex].mHeight > mTreeNodes[RightRightNodeIndex].mHeight) {
            mTreeNodes[RightNodeIndex].mRight = RightLeftNodeIndex;
            mTreeNodes[NodeIndex].mRight = RightRightNodeIndex;
            mTreeNodes[RightRightNodeIndex].mParent = NodeIndex;
            mTreeNodes[RightLeftNodeIndex].mParent = RightNodeIndex;
            mTreeNodes[NodeIndex].mBounds = MergeBounds(mTreeNodes[LeftNodeIndex].mBounds, mTreeNodes[RightRightNodeIndex].mBounds);
            mTreeNodes[RightNodeIndex].mBounds = MergeBounds(mTreeNodes[NodeIndex].mBounds, mTreeNodes[RightLeftNodeIndex].mBounds);
            mTreeNodes[NodeIndex].mHeight = 1 + std::max(mTreeNodes[LeftNodeIndex].mHeight, mTreeNodes[RightRightNodeIndex].mHeight);
            mTreeNodes[RightNodeIndex].mHeight = 1 + std::max(mTreeNodes[NodeIndex].mHeight, mTreeNodes[RightLeftNodeIndex].mHeight);
        } else {
            mTreeNodes[RightNodeIndex].mRight = RightRightNodeIndex;
            mTreeNodes[NodeIndex].mRight = RightLeftNodeIndex;
            mTreeNodes[RightLeftNodeIndex].mParent = NodeIndex;
            mTreeNodes[RightRightNodeIndex].mParent = RightNodeIndex;
            mTreeNodes[NodeIndex].mBounds = MergeBounds(mTreeNodes[LeftNodeIndex].mBounds, mTreeNodes[RightLeftNodeIndex].mBounds);
            mTreeNodes[RightNodeIndex].mBounds = MergeBounds(mTreeNodes[NodeIndex].mBounds, mTreeNodes[RightRightNodeIndex].mBounds);
            mTreeNodes[NodeIndex].mHeight = 1 + std::max(mTreeNodes[LeftNodeIndex].mHeight, mTreeNodes[RightLeftNodeIndex].mHeight);
            mTreeNodes[RightNodeIndex].mHeight = 1 + std::max(mTreeNodes[NodeIndex].mHeight, mTreeNodes[RightRightNodeIndex].mHeight);
        }

        return RightNodeIndex;
    }

    if (HeightDifference < -1) {
        const int LeftLeftNodeIndex{ mTreeNodes[LeftNodeIndex].mLeft };
        const int LeftRightNodeIndex{ mTreeNodes[LeftNodeIndex].mRight };
        const int ParentNodeIndex{ mTreeNodes[NodeIndex].mParent };
        mTreeNodes[LeftNodeIndex].mLeft = NodeIndex;
        mTreeNodes[LeftNodeIndex].mParent = ParentNodeIndex;
        mTreeNodes[NodeIndex].mParent = LeftNodeIndex;

        if (ParentNodeIndex == InvalidNodeIndex) {
            mRootNodeIndex = LeftNodeIndex;
        } else if (mTreeNodes[ParentNodeIndex].mLeft == NodeIndex) {
            mTreeNodes[ParentNodeIndex].mLeft = LeftNodeIndex;
        } else {
            mTreeNodes[ParentNodeIndex].mRight = LeftNodeIndex;
        }

        if (mTreeNodes[LeftLeftNodeIndex].mHeight > mTreeNodes[LeftRightNodeIndex].mHeight) {
            mTreeNodes[LeftNodeIndex].mRight = LeftLeftNodeIndex;
            mTreeNodes[NodeIndex].mLeft = LeftRightNodeIndex;
            mTreeNodes[LeftRightNodeIndex].mParent = NodeIndex;
            mTreeNodes[LeftLeftNodeIndex].mParent = LeftNodeIndex;
            mTreeNodes[NodeIndex].mBounds = MergeBounds(mTreeNodes[RightNodeIndex].mBounds, mTreeNodes[LeftRightNodeIndex].mBounds);
            mTreeNodes[LeftNodeIndex].mBounds = MergeBounds(mTreeNodes[NodeIndex].mBounds, mTreeNodes[LeftLeftNodeIndex].mBounds);
            mTreeNodes[NodeIndex].mHeight = 1 + std::max(mTreeNodes[RightNodeIndex].mHeight, mTreeNodes[LeftRightNodeIndex].mHeight);
            mTreeNodes[LeftNodeIndex].mHeight = 1 + std::max(mTreeNodes[NodeIndex].mHeight, mTreeNodes[LeftLeftNodeIndex].mHeight);
        } else {
            mTreeNodes[LeftNodeIndex].mRight = LeftRightNodeIndex;
            mTreeNodes[NodeIndex].mLeft = LeftLeftNodeIndex;
            mTreeNodes[LeftLeftNodeIndex].mParent = NodeIndex;
            mTreeNodes[LeftRightNodeIndex].mParent = LeftNodeIndex;
            mTreeNodes[NodeIndex].mBounds = MergeBounds(mTreeNodes[RightNodeIndex].mBounds, mTreeNodes[LeftLeftNodeIndex].mBounds);
            mTreeNodes[LeftNodeIndex].mBounds = MergeBounds(mTreeNodes[NodeIndex].mBounds, mTreeNodes[LeftRightNodeIndex].mBounds);
            mTreeNodes[NodeIndex].mHeight = 1 + std::max(mTreeNodes[RightNodeIndex].mHeight, mTreeNodes[LeftLeftNodeIndex].mHeight);
            mTreeNodes[LeftNodeIndex].mHeight = 1 + std::max(mTreeNodes[NodeIndex].mHeight, mTreeNodes[LeftRightNodeIndex].mHeight);
        }

        return LeftNodeIndex;
    }

    return NodeIndex;
}

void DynamicAabbTreePhysicsSpatialQuery::UpdateAncestors(int NodeIndex) {
    int CurrentNodeIndex{ NodeIndex };
    while (CurrentNodeIndex != InvalidNodeIndex) {
        CurrentNodeIndex = BalanceNode(CurrentNodeIndex);
        if (CurrentNodeIndex == InvalidNodeIndex) {
            return;
        }

        TreeNode& CurrentNode{ mTreeNodes[CurrentNodeIndex] };
        if (IsLeafNode(CurrentNodeIndex) == false) {
            const int LeftNodeIndex{ CurrentNode.mLeft };
            const int RightNodeIndex{ CurrentNode.mRight };
            CurrentNode.mBounds = MergeBounds(mTreeNodes[LeftNodeIndex].mBounds, mTreeNodes[RightNodeIndex].mBounds);
            CurrentNode.mHeight = 1 + std::max(mTreeNodes[LeftNodeIndex].mHeight, mTreeNodes[RightNodeIndex].mHeight);
        }

        CurrentNodeIndex = CurrentNode.mParent;
    }
}

void DynamicAabbTreePhysicsSpatialQuery::UpdateActorProxy(PhysicsActorBase& Actor) {
    const AxisAlignedBounds ActorBounds{ BuildActorBounds(Actor) };
    const std::unordered_map<PhysicsActorBase*, int>::iterator FoundNodeIndexIterator{ mNodeIndexByActor.find(&Actor) };
    if (FoundNodeIndexIterator == mNodeIndexByActor.end()) {
        const int NewNodeIndex{ AllocateTreeNode() };
        TreeNode& NewNode{ mTreeNodes[NewNodeIndex] };
        NewNode.mActor = &Actor;
        NewNode.mBounds = ActorBounds;
        NewNode.mHeight = 0;
        NewNode.mSynchronizationVersion = mSynchronizationVersion;
        mNodeIndexByActor.insert_or_assign(&Actor, NewNodeIndex);
        InsertLeafNode(NewNodeIndex);
        return;
    }

    const int NodeIndex{ FoundNodeIndexIterator->second };
    TreeNode& Node{ mTreeNodes[NodeIndex] };
    Node.mSynchronizationVersion = mSynchronizationVersion;
    if (DoesNodeContainBounds(NodeIndex, ActorBounds)) {
        return;
    }

    RemoveLeafNode(NodeIndex);
    Node.mBounds = ActorBounds;
    Node.mHeight = 0;
    InsertLeafNode(NodeIndex);
}

void DynamicAabbTreePhysicsSpatialQuery::RemoveStaleActorProxies() {
    std::unordered_map<PhysicsActorBase*, int>::iterator ActorNodeIterator{ mNodeIndexByActor.begin() };
    while (ActorNodeIterator != mNodeIndexByActor.end()) {
        const int NodeIndex{ ActorNodeIterator->second };
        if (NodeIndex != InvalidNodeIndex && mTreeNodes[NodeIndex].mSynchronizationVersion == mSynchronizationVersion) {
            ++ActorNodeIterator;
            continue;
        }

        RemoveLeafNode(NodeIndex);
        FreeTreeNode(NodeIndex);
        ActorNodeIterator = mNodeIndexByActor.erase(ActorNodeIterator);
    }
}

void DynamicAabbTreePhysicsSpatialQuery::QueryBounds(const AxisAlignedBounds& Bounds, std::vector<PhysicsActorBase*>& OutActors) const {
    if (mRootNodeIndex == InvalidNodeIndex) {
        return;
    }

    mQueryNodeIndices.clear();
    mQueryNodeIndices.push_back(mRootNodeIndex);
    while (mQueryNodeIndices.empty() == false) {
        const int NodeIndex{ mQueryNodeIndices.back() };
        mQueryNodeIndices.pop_back();
        if (NodeIndex == InvalidNodeIndex || IsOverlappingBounds(Bounds, mTreeNodes[NodeIndex].mBounds) == false) {
            continue;
        }

        const TreeNode& Node{ mTreeNodes[NodeIndex] };
        if (IsLeafNode(NodeIndex)) {
            if (Node.mActor != nullptr) {
                OutActors.push_back(Node.mActor);
            }

            continue;
        }

        mQueryNodeIndices.push_back(Node.mLeft);
        mQueryNodeIndices.push_back(Node.mRight);
    }
}

void DynamicAabbTreePhysicsSpatialQuery::ResetSynchronizationVersionIfNeeded() {
    if (mSynchronizationVersion == std::numeric_limits<std::uint64_t>::max()) {
        mSynchronizationVersion = 0U;
        for (TreeNode& Node : mTreeNodes) {
            Node.mSynchronizationVersion = 0U;
        }
    }

    mSynchronizationVersion += 1U;
}

bool DynamicAabbTreePhysicsSpatialQuery::IsLeafNode(int NodeIndex) const {
    return NodeIndex != InvalidNodeIndex && mTreeNodes[NodeIndex].mLeft == InvalidNodeIndex;
}

bool DynamicAabbTreePhysicsSpatialQuery::ShouldCreateProxyForActor(const PhysicsActorBase& Actor) const {
    return Actor.GetIsActive() && Actor.IsTerrainActor() == false;
}

bool DynamicAabbTreePhysicsSpatialQuery::DoesNodeContainBounds(int NodeIndex, const AxisAlignedBounds& Bounds) const {
    if (NodeIndex == InvalidNodeIndex) {
        return false;
    }

    const AxisAlignedBounds& NodeBounds{ mTreeNodes[NodeIndex].mBounds };
    return NodeBounds.mMinimum.x <= Bounds.mMinimum.x && NodeBounds.mMinimum.y <= Bounds.mMinimum.y && NodeBounds.mMinimum.z <= Bounds.mMinimum.z && NodeBounds.mMaximum.x >= Bounds.mMaximum.x && NodeBounds.mMaximum.y >= Bounds.mMaximum.y && NodeBounds.mMaximum.z >= Bounds.mMaximum.z;
}

DynamicAabbTreePhysicsSpatialQuery::AxisAlignedBounds DynamicAabbTreePhysicsSpatialQuery::BuildActorBounds(const PhysicsActorBase& Actor) const {
    DirectX::XMFLOAT3 Corners[8]{};
    Actor.GetFatWorldBoundingBox().GetCorners(Corners);
    AxisAlignedBounds Bounds{};
    Bounds.mMinimum = DirectX::SimpleMath::Vector3{ Corners[0].x, Corners[0].y, Corners[0].z };
    Bounds.mMaximum = Bounds.mMinimum;

    for (std::size_t CornerIndex{ 1U }; CornerIndex < 8U; ++CornerIndex) {
        Bounds.mMinimum.x = std::min(Bounds.mMinimum.x, Corners[CornerIndex].x);
        Bounds.mMinimum.y = std::min(Bounds.mMinimum.y, Corners[CornerIndex].y);
        Bounds.mMinimum.z = std::min(Bounds.mMinimum.z, Corners[CornerIndex].z);
        Bounds.mMaximum.x = std::max(Bounds.mMaximum.x, Corners[CornerIndex].x);
        Bounds.mMaximum.y = std::max(Bounds.mMaximum.y, Corners[CornerIndex].y);
        Bounds.mMaximum.z = std::max(Bounds.mMaximum.z, Corners[CornerIndex].z);
    }

    return Bounds;
}

DynamicAabbTreePhysicsSpatialQuery::AxisAlignedBounds DynamicAabbTreePhysicsSpatialQuery::MergeBounds(const AxisAlignedBounds& FirstBounds, const AxisAlignedBounds& SecondBounds) const {
    AxisAlignedBounds MergedBounds{};
    MergedBounds.mMinimum.x = std::min(FirstBounds.mMinimum.x, SecondBounds.mMinimum.x);
    MergedBounds.mMinimum.y = std::min(FirstBounds.mMinimum.y, SecondBounds.mMinimum.y);
    MergedBounds.mMinimum.z = std::min(FirstBounds.mMinimum.z, SecondBounds.mMinimum.z);
    MergedBounds.mMaximum.x = std::max(FirstBounds.mMaximum.x, SecondBounds.mMaximum.x);
    MergedBounds.mMaximum.y = std::max(FirstBounds.mMaximum.y, SecondBounds.mMaximum.y);
    MergedBounds.mMaximum.z = std::max(FirstBounds.mMaximum.z, SecondBounds.mMaximum.z);
    return MergedBounds;
}

bool DynamicAabbTreePhysicsSpatialQuery::IsOverlappingBounds(const AxisAlignedBounds& FirstBounds, const AxisAlignedBounds& SecondBounds) const {
    return FirstBounds.mMinimum.x <= SecondBounds.mMaximum.x && SecondBounds.mMinimum.x <= FirstBounds.mMaximum.x && FirstBounds.mMinimum.y <= SecondBounds.mMaximum.y && SecondBounds.mMinimum.y <= FirstBounds.mMaximum.y && FirstBounds.mMinimum.z <= SecondBounds.mMaximum.z && SecondBounds.mMinimum.z <= FirstBounds.mMaximum.z;
}

float DynamicAabbTreePhysicsSpatialQuery::GetBoundsSurfaceArea(const AxisAlignedBounds& Bounds) const {
    const DirectX::SimpleMath::Vector3 Extents{ Bounds.mMaximum - Bounds.mMinimum };
    const float Width{ std::max(Extents.x, 0.0F) };
    const float Height{ std::max(Extents.y, 0.0F) };
    const float Depth{ std::max(Extents.z, 0.0F) };
    return 2.0F * ((Width * Height) + (Height * Depth) + (Depth * Width));
}
