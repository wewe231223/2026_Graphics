#include "PhysicsActorRepository.h"

#include <utility>

PhysicsActorRepository::PhysicsActorRepository()
    : mActors{} {
}

PhysicsActorRepository::~PhysicsActorRepository() {
}

PhysicsActorRepository::PhysicsActorRepository(const PhysicsActorRepository& Other)
    : mActors{} {
    std::size_t ActorCount{ Other.mActors.size() };
    mActors.reserve(ActorCount);

    for (std::size_t ActorIndex{ 0U }; ActorIndex < ActorCount; ++ActorIndex) {
        const std::unique_ptr<PhysicsActorBase>& SourceActorPointer{ Other.mActors[ActorIndex] };
        if (SourceActorPointer == nullptr) {
            continue;
        }

        std::unique_ptr<PhysicsActorBase> ClonedActor{ SourceActorPointer->Clone() };
        if (ClonedActor == nullptr) {
            continue;
        }

        mActors.push_back(std::move(ClonedActor));
    }
}

PhysicsActorRepository& PhysicsActorRepository::operator=(const PhysicsActorRepository& Other) {
    if (this == &Other) {
        return *this;
    }

    mActors.clear();
    std::size_t ActorCount{ Other.mActors.size() };
    mActors.reserve(ActorCount);

    for (std::size_t ActorIndex{ 0U }; ActorIndex < ActorCount; ++ActorIndex) {
        const std::unique_ptr<PhysicsActorBase>& SourceActorPointer{ Other.mActors[ActorIndex] };
        if (SourceActorPointer == nullptr) {
            continue;
        }

        std::unique_ptr<PhysicsActorBase> ClonedActor{ SourceActorPointer->Clone() };
        if (ClonedActor == nullptr) {
            continue;
        }

        mActors.push_back(std::move(ClonedActor));
    }

    return *this;
}

PhysicsActorRepository::PhysicsActorRepository(PhysicsActorRepository&& Other) noexcept
    : mActors{ std::move(Other.mActors) } {
}

PhysicsActorRepository& PhysicsActorRepository::operator=(PhysicsActorRepository&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    mActors = std::move(Other.mActors);

    return *this;
}

std::unique_ptr<IPhysicsActorRepository> PhysicsActorRepository::Clone() const {
    std::unique_ptr<IPhysicsActorRepository> ClonedRepository{ std::make_unique<PhysicsActorRepository>(*this) };
    return ClonedRepository;
}

PhysicsDynamicActor* PhysicsActorRepository::CreateDynamicActor(const PhysicsDynamicActor::ActorDesc& Desc) {
    std::unique_ptr<PhysicsActorBase> NewActor{ std::make_unique<PhysicsDynamicActor>(Desc) };
    PhysicsDynamicActor* CreatedActor{ static_cast<PhysicsDynamicActor*>(NewActor.get()) };
    mActors.push_back(std::move(NewActor));
    return CreatedActor;
}

PhysicsKinematicActor* PhysicsActorRepository::CreateKinematicActor(const PhysicsKinematicActor::ActorDesc& Desc) {
    std::unique_ptr<PhysicsActorBase> NewActor{ std::make_unique<PhysicsKinematicActor>(Desc) };
    PhysicsKinematicActor* CreatedActor{ static_cast<PhysicsKinematicActor*>(NewActor.get()) };
    mActors.push_back(std::move(NewActor));
    return CreatedActor;
}

PhysicsTerrainActor* PhysicsActorRepository::CreateTerrainActor(const PhysicsTerrainActor::ActorDesc& Desc) {
    std::unique_ptr<PhysicsActorBase> NewActor{ std::make_unique<PhysicsTerrainActor>(Desc) };
    PhysicsTerrainActor* CreatedActor{ static_cast<PhysicsTerrainActor*>(NewActor.get()) };
    mActors.push_back(std::move(NewActor));
    return CreatedActor;
}

void PhysicsActorRepository::AddActor(std::unique_ptr<PhysicsActorBase> Actor) {
    mActors.push_back(std::move(Actor));
}

void PhysicsActorRepository::ClearActors() {
    mActors.clear();
}

PhysicsActorBase* PhysicsActorRepository::GetActor(std::size_t Index) {
    if (Index >= mActors.size()) {
        return nullptr;
    }

    return mActors[Index].get();
}

const PhysicsActorBase* PhysicsActorRepository::GetActor(std::size_t Index) const {
    if (Index >= mActors.size()) {
        return nullptr;
    }

    return mActors[Index].get();
}

PhysicsTerrainActor* PhysicsActorRepository::GetTerrainActor(std::size_t Index) {
    PhysicsActorBase* ActorPointer{ GetActor(Index) };
    if (ActorPointer == nullptr || ActorPointer->IsTerrainActor() == false) {
        return nullptr;
    }

    PhysicsTerrainActor* TerrainActorPointer{ static_cast<PhysicsTerrainActor*>(ActorPointer) };
    return TerrainActorPointer;
}

const PhysicsTerrainActor* PhysicsActorRepository::GetTerrainActor(std::size_t Index) const {
    const PhysicsActorBase* ActorPointer{ GetActor(Index) };
    if (ActorPointer == nullptr || ActorPointer->IsTerrainActor() == false) {
        return nullptr;
    }

    const PhysicsTerrainActor* TerrainActorPointer{ static_cast<const PhysicsTerrainActor*>(ActorPointer) };
    return TerrainActorPointer;
}

std::size_t PhysicsActorRepository::GetActorCount() const {
    std::size_t ActorCount{ mActors.size() };
    return ActorCount;
}

std::vector<PhysicsDynamicActor*> PhysicsActorRepository::CollectDynamicActors() {
    std::vector<PhysicsDynamicActor*> DynamicActors{};
    CollectDynamicActors(DynamicActors);
    return DynamicActors;
}

std::vector<const PhysicsDynamicActor*> PhysicsActorRepository::CollectDynamicActors() const {
    std::vector<const PhysicsDynamicActor*> DynamicActors{};
    CollectDynamicActors(DynamicActors);
    return DynamicActors;
}

std::vector<PhysicsKinematicActor*> PhysicsActorRepository::CollectKinematicActors() {
    std::vector<PhysicsKinematicActor*> KinematicActors{};
    CollectKinematicActors(KinematicActors);
    return KinematicActors;
}

std::vector<const PhysicsKinematicActor*> PhysicsActorRepository::CollectKinematicActors() const {
    std::vector<const PhysicsKinematicActor*> KinematicActors{};
    CollectKinematicActors(KinematicActors);
    return KinematicActors;
}

std::vector<const PhysicsStaticActor*> PhysicsActorRepository::CollectStaticActors() const {
    std::vector<const PhysicsStaticActor*> StaticActors{};
    CollectStaticActors(StaticActors);
    return StaticActors;
}

std::vector<PhysicsTerrainActor*> PhysicsActorRepository::CollectTerrainActors() {
    std::vector<PhysicsTerrainActor*> TerrainActors{};
    CollectTerrainActors(TerrainActors);
    return TerrainActors;
}

std::vector<const PhysicsTerrainActor*> PhysicsActorRepository::CollectTerrainActors() const {
    std::vector<const PhysicsTerrainActor*> TerrainActors{};
    CollectTerrainActors(TerrainActors);
    return TerrainActors;
}

void PhysicsActorRepository::CollectDynamicActors(std::vector<PhysicsDynamicActor*>& OutActors) {
    OutActors.clear();
    std::size_t ActorCount{ mActors.size() };
    OutActors.reserve(ActorCount);

    for (std::size_t ActorIndex{ 0U }; ActorIndex < ActorCount; ++ActorIndex) {
        PhysicsActorBase* CurrentActor{ mActors[ActorIndex].get() };
        if (CurrentActor == nullptr || CurrentActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic) {
            continue;
        }

        PhysicsDynamicActor* DynamicActor{ static_cast<PhysicsDynamicActor*>(CurrentActor) };
        OutActors.push_back(DynamicActor);
    }
}

void PhysicsActorRepository::CollectDynamicActors(std::vector<const PhysicsDynamicActor*>& OutActors) const {
    OutActors.clear();
    std::size_t ActorCount{ mActors.size() };
    OutActors.reserve(ActorCount);

    for (std::size_t ActorIndex{ 0U }; ActorIndex < ActorCount; ++ActorIndex) {
        const PhysicsActorBase* CurrentActor{ mActors[ActorIndex].get() };
        if (CurrentActor == nullptr || CurrentActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Dynamic) {
            continue;
        }

        const PhysicsDynamicActor* DynamicActor{ static_cast<const PhysicsDynamicActor*>(CurrentActor) };
        OutActors.push_back(DynamicActor);
    }
}

void PhysicsActorRepository::CollectKinematicActors(std::vector<PhysicsKinematicActor*>& OutActors) {
    OutActors.clear();
    std::size_t ActorCount{ mActors.size() };
    OutActors.reserve(ActorCount);

    for (std::size_t ActorIndex{ 0U }; ActorIndex < ActorCount; ++ActorIndex) {
        PhysicsActorBase* CurrentActor{ mActors[ActorIndex].get() };
        if (CurrentActor == nullptr || CurrentActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic) {
            continue;
        }

        PhysicsKinematicActor* KinematicActor{ static_cast<PhysicsKinematicActor*>(CurrentActor) };
        OutActors.push_back(KinematicActor);
    }
}

void PhysicsActorRepository::CollectKinematicActors(std::vector<const PhysicsKinematicActor*>& OutActors) const {
    OutActors.clear();
    std::size_t ActorCount{ mActors.size() };
    OutActors.reserve(ActorCount);

    for (std::size_t ActorIndex{ 0U }; ActorIndex < ActorCount; ++ActorIndex) {
        const PhysicsActorBase* CurrentActor{ mActors[ActorIndex].get() };
        if (CurrentActor == nullptr || CurrentActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Kinematic) {
            continue;
        }

        const PhysicsKinematicActor* KinematicActor{ static_cast<const PhysicsKinematicActor*>(CurrentActor) };
        OutActors.push_back(KinematicActor);
    }
}

void PhysicsActorRepository::CollectStaticActors(std::vector<const PhysicsStaticActor*>& OutActors) const {
    OutActors.clear();
    std::size_t ActorCount{ mActors.size() };
    OutActors.reserve(ActorCount);

    for (std::size_t ActorIndex{ 0U }; ActorIndex < ActorCount; ++ActorIndex) {
        const PhysicsActorBase* CurrentActor{ mActors[ActorIndex].get() };
        if (CurrentActor == nullptr || CurrentActor->GetActorType() != PhysicsActorBase::PhysicsActorType::Static) {
            continue;
        }

        const PhysicsStaticActor* StaticActor{ static_cast<const PhysicsStaticActor*>(CurrentActor) };
        OutActors.push_back(StaticActor);
    }
}

void PhysicsActorRepository::CollectTerrainActors(std::vector<PhysicsTerrainActor*>& OutActors) {
    OutActors.clear();
    std::size_t ActorCount{ mActors.size() };
    OutActors.reserve(ActorCount);

    for (std::size_t ActorIndex{ 0U }; ActorIndex < ActorCount; ++ActorIndex) {
        PhysicsActorBase* CurrentActor{ mActors[ActorIndex].get() };
        if (CurrentActor == nullptr || CurrentActor->IsTerrainActor() == false) {
            continue;
        }

        PhysicsTerrainActor* TerrainActor{ static_cast<PhysicsTerrainActor*>(CurrentActor) };
        OutActors.push_back(TerrainActor);
    }
}

void PhysicsActorRepository::CollectTerrainActors(std::vector<const PhysicsTerrainActor*>& OutActors) const {
    OutActors.clear();
    std::size_t ActorCount{ mActors.size() };
    OutActors.reserve(ActorCount);

    for (std::size_t ActorIndex{ 0U }; ActorIndex < ActorCount; ++ActorIndex) {
        const PhysicsActorBase* CurrentActor{ mActors[ActorIndex].get() };
        if (CurrentActor == nullptr || CurrentActor->IsTerrainActor() == false) {
            continue;
        }

        const PhysicsTerrainActor* TerrainActor{ static_cast<const PhysicsTerrainActor*>(CurrentActor) };
        OutActors.push_back(TerrainActor);
    }
}
