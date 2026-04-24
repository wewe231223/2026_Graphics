#include "BruteForcePhysicsSpatialQuery.h"

#include <algorithm>
#include <utility>

#undef min
#undef max

namespace {
    struct AxisAlignedBounds {
        DirectX::SimpleMath::Vector3 mMinimum;
        DirectX::SimpleMath::Vector3 mMaximum;
    };

    struct DynamicBroadPhaseEntry {
        PhysicsDynamicActor* mDynamicActor;
        AxisAlignedBounds mFatBounds;
    };

    AxisAlignedBounds MakeAxisAlignedBounds(const DirectX::BoundingOrientedBox& BoundingBox) {
        DirectX::XMFLOAT3 Corners[8]{};
        BoundingBox.GetCorners(Corners);

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

    bool IsOverlappingAxisAlignedBounds(const AxisAlignedBounds& FirstBounds, const AxisAlignedBounds& SecondBounds) {
        bool IsOverlappingX{ FirstBounds.mMinimum.x <= SecondBounds.mMaximum.x && SecondBounds.mMinimum.x <= FirstBounds.mMaximum.x };
        bool IsOverlappingY{ FirstBounds.mMinimum.y <= SecondBounds.mMaximum.y && SecondBounds.mMinimum.y <= FirstBounds.mMaximum.y };
        bool IsOverlappingZ{ FirstBounds.mMinimum.z <= SecondBounds.mMaximum.z && SecondBounds.mMinimum.z <= FirstBounds.mMaximum.z };
        bool IsOverlapping{ IsOverlappingX && IsOverlappingY && IsOverlappingZ };
        return IsOverlapping;
    }
}

BruteForcePhysicsSpatialQuery::BruteForcePhysicsSpatialQuery() {
}

BruteForcePhysicsSpatialQuery::~BruteForcePhysicsSpatialQuery() {
}

BruteForcePhysicsSpatialQuery::BruteForcePhysicsSpatialQuery(const BruteForcePhysicsSpatialQuery& Other)
    : IPhysicsSpatialQuery{ Other } {
}

BruteForcePhysicsSpatialQuery& BruteForcePhysicsSpatialQuery::operator=(const BruteForcePhysicsSpatialQuery& Other) {
    if (this == &Other) {
        return *this;
    }

    IPhysicsSpatialQuery::operator=(Other);

    return *this;
}

BruteForcePhysicsSpatialQuery::BruteForcePhysicsSpatialQuery(BruteForcePhysicsSpatialQuery&& Other) noexcept
    : IPhysicsSpatialQuery{ std::move(Other) } {
}

BruteForcePhysicsSpatialQuery& BruteForcePhysicsSpatialQuery::operator=(BruteForcePhysicsSpatialQuery&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    IPhysicsSpatialQuery::operator=(std::move(Other));

    return *this;
}

std::unique_ptr<IPhysicsSpatialQuery> BruteForcePhysicsSpatialQuery::Clone() const {
    std::unique_ptr<IPhysicsSpatialQuery> ClonedQuery{ std::make_unique<BruteForcePhysicsSpatialQuery>(*this) };
    return ClonedQuery;
}

std::vector<PhysicsDynamicCollisionPairCandidate> BruteForcePhysicsSpatialQuery::QueryDynamicCollisionPairs(IPhysicsActorRepository& ActorRepository) const {
    std::vector<PhysicsDynamicActor*> DynamicActors{ ActorRepository.CollectDynamicActors() };
    std::size_t DynamicActorCount{ DynamicActors.size() };

    std::vector<DynamicBroadPhaseEntry> DynamicEntries{};
    DynamicEntries.reserve(DynamicActorCount);

    for (std::size_t ActorIndex{ 0U }; ActorIndex < DynamicActorCount; ++ActorIndex) {
        PhysicsDynamicActor* DynamicActor{ DynamicActors[ActorIndex] };
        if (DynamicActor == nullptr || !DynamicActor->GetIsActive() || DynamicActor->GetInverseMass() <= 0.0F) {
            continue;
        }

        AxisAlignedBounds FatBounds{ MakeAxisAlignedBounds(DynamicActor->GetFatWorldBoundingBox()) };
        DynamicEntries.push_back(DynamicBroadPhaseEntry{ DynamicActor, FatBounds });
    }

    std::vector<PhysicsDynamicCollisionPairCandidate> PairCandidates{};
    std::size_t DynamicEntryCount{ DynamicEntries.size() };
    if (DynamicEntryCount < 2U) {
        return PairCandidates;
    }

    std::sort(DynamicEntries.begin(), DynamicEntries.end(), [](const DynamicBroadPhaseEntry& Left, const DynamicBroadPhaseEntry& Right) {
        return Left.mFatBounds.mMinimum.x < Right.mFatBounds.mMinimum.x;
    });

    PairCandidates.reserve(DynamicEntryCount * 4U);
    for (std::size_t FirstIndex{ 0U }; FirstIndex < DynamicEntryCount; ++FirstIndex) {
        const DynamicBroadPhaseEntry& FirstEntry{ DynamicEntries[FirstIndex] };
        for (std::size_t SecondIndex{ FirstIndex + 1U }; SecondIndex < DynamicEntryCount; ++SecondIndex) {
            const DynamicBroadPhaseEntry& SecondEntry{ DynamicEntries[SecondIndex] };
            if (SecondEntry.mFatBounds.mMinimum.x > FirstEntry.mFatBounds.mMaximum.x) {
                break;
            }

            bool IsOverlappingFatBounds{ IsOverlappingAxisAlignedBounds(FirstEntry.mFatBounds, SecondEntry.mFatBounds) };
            if (!IsOverlappingFatBounds) {
                continue;
            }

            PairCandidates.push_back(PhysicsDynamicCollisionPairCandidate{ FirstEntry.mDynamicActor, SecondEntry.mDynamicActor });
        }
    }

    return PairCandidates;
}
