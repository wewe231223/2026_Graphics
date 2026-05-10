#include "World.h"

using namespace Arche;

World::WorldReadOnlyView::WorldReadOnlyView()
    : mWorld{} {
}

World::WorldReadOnlyView::~WorldReadOnlyView() {
}

World::WorldReadOnlyView::WorldReadOnlyView(const WorldReadOnlyView& Other)
    : mWorld{ Other.mWorld } {
}

World::WorldReadOnlyView& World::WorldReadOnlyView::operator=(const WorldReadOnlyView& Other) {
    if (this == &Other) {
        return *this;
    }

    mWorld = Other.mWorld;
    return *this;
}

World::WorldReadOnlyView::WorldReadOnlyView(WorldReadOnlyView&& Other) noexcept
    : mWorld{ Other.mWorld } {
}

World::WorldReadOnlyView& World::WorldReadOnlyView::operator=(WorldReadOnlyView&& Other) noexcept {
    if (this == &Other) {
        return *this;
    }

    mWorld = Other.mWorld;
    return *this;
}

void World::WorldReadOnlyView::BindWorld(const World* TargetWorld) {
    mWorld = TargetWorld;
}

std::uint64_t World::WorldReadOnlyView::GetStructureVersion() const {
    if (mWorld == nullptr) {
        return 0;
    }

    return mWorld->GetStructureVersion();
}

World::World()
    : mEntityTable{},
    mFreeIndices{},
    mArcheTypes{},
    mQueryCaches{},
    mDeferredStructuralCommands{},
    mWorldRwLock{},
    mDeferredQueueLock{},
    mStructureVersion{},
    mReadOnlyView{} {
    mReadOnlyView.BindWorld(this);
}

World::~World() = default;


Archetype* World::GetOrCreateArchetype(std::span<const TypeID> SortedIDs, std::span<const size_t> Sizes, std::span<const size_t> Aligns) {
    for (const std::unique_ptr<Archetype>& Arch : mArcheTypes) {
        const std::vector<TypeID>& Signature{ Arch->GetSignature() };

        if (Signature.size() == SortedIDs.size() && std::equal(Signature.begin(), Signature.end(), SortedIDs.begin())) {
            return Arch.get();
        }
    }

    std::vector<TypeID> VecIDs{ SortedIDs.begin(), SortedIDs.end() };
    std::vector<size_t> VecSizes{ Sizes.begin(), Sizes.end() };
    std::vector<size_t> VecAligns{ Aligns.begin(), Aligns.end() };

    mArcheTypes.push_back(std::make_unique<Archetype>(std::move(VecIDs), std::move(VecSizes), std::move(VecAligns)));
    Archetype* NewArch{ mArcheTypes.back().get() };

    for (QueryCache& Cache : mQueryCaches) {
        const std::vector<TypeID>& Signature{ NewArch->GetSignature() };

        if (std::includes(Signature.begin(), Signature.end(), Cache.mSignature.begin(), Cache.mSignature.end())) {
            Cache.mArchetypes.push_back(NewArch);
        }
    }

    return NewArch;
}


std::uint64_t World::GetStructureVersion() const {
    return mStructureVersion.load();
}

const World::WorldReadOnlyView& World::GetReadOnlyView() const {
    return mReadOnlyView;
}

void World::TouchStructureVersion() {
    mStructureVersion.fetch_add(1);
}

void World::DestroyEntity(EntityID Id) {
    std::unique_lock<std::shared_mutex> Lock{ mWorldRwLock };
    DestroyEntityInternal(Id);
}

void World::DestroyEntityInternal(EntityID Id) {
    if (Id.index >= mEntityTable.size()) {
        return;
    }

    EntityRecord& Record{ mEntityTable[Id.index] };

    if (!Record.active || Record.generation != Id.generation) {
        return;
    }

    EntityID MovedID{ Record.archetype->PopEntity(Record.chunkIndex, Record.entityIndex) };

    if (MovedID != Id) {
        EntityRecord& MovedRecord{ mEntityTable[MovedID.index] };
        MovedRecord.chunkIndex = Record.chunkIndex;
        MovedRecord.entityIndex = Record.entityIndex;
    }

    Record.active = false;
    Record.archetype = nullptr;
    ++Record.generation;

    mFreeIndices.push_back(Id.index);
    TouchStructureVersion();
}

void World::GetArchetypeInfo(Archetype* Arch, std::vector<TypeID>& OutIds, std::vector<size_t>& OutSizes, std::vector<size_t>& OutAligns) {
    OutIds.reserve(Arch->mLayout.size());
    OutSizes.reserve(Arch->mLayout.size());
    OutAligns.reserve(Arch->mLayout.size());

    for (const Archetype::Column& Value : Arch->mLayout) {
        OutIds.push_back(Value.id);
        OutSizes.push_back(Value.size);
        OutAligns.push_back(Value.align);
    }
}

void World::DeferDestroyEntity(EntityID Id) {
    std::lock_guard<std::mutex> Lock{ mDeferredQueueLock };

    mDeferredStructuralCommands.emplace_back([Id](World& TargetWorld) {
        TargetWorld.DestroyEntityInternal(Id);
    });
}

void World::FlushDeferredStructuralChanges() {
    std::deque<std::function<void(World&)>> Commands{};

    {
        std::lock_guard<std::mutex> Lock{ mDeferredQueueLock };
        Commands.swap(mDeferredStructuralCommands);
    }

    std::unique_lock<std::shared_mutex> Lock{ mWorldRwLock };

    for (std::function<void(World&)>& Command : Commands) {
        Command(*this);
    }
}
