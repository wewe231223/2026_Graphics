
namespace Arche {
    template <TrivialComponent T>
    const T* World::WorldReadOnlyView::GetComponent(EntityID Id) const {
        if (mWorld == nullptr) {
            return nullptr;
        }

        return mWorld->GetComponent<T>(Id);
    }


    template <TrivialComponent... Ts>
    EntityID World::CreateEntity(Ts... Args) {
        std::unique_lock<std::shared_mutex> Lock{ mWorldRwLock };
        return CreateEntityInternal<Ts...>(std::forward<Ts>(Args)...);
    }

    template <TrivialComponent... Ts>
    EntityID World::CreateEntity() {
        std::unique_lock<std::shared_mutex> Lock{ mWorldRwLock };
        return CreateEntityInternal<Ts...>(Ts{}...);
    }

    template <TrivialComponent T>
    void World::AddComponent(EntityID Id, T Component) {
        std::unique_lock<std::shared_mutex> Lock{ mWorldRwLock };
        AddComponentInternal<T>(Id, std::move(Component));
    }

    template <TrivialComponent T>
    T* World::GetComponent(EntityID Id) {
        std::shared_lock<std::shared_mutex> Lock{ mWorldRwLock };
        return GetComponentUnsafe<T>(Id);
    }

    template <TrivialComponent T>
    const T* World::GetComponent(EntityID Id) const {
        std::shared_lock<std::shared_mutex> Lock{ mWorldRwLock };
        return GetComponentUnsafe<T>(Id);
    }

    template <TrivialComponent T, typename Func>
    bool World::ReadComponent(EntityID Id, Func&& Reader) const {
        std::shared_lock<std::shared_mutex> Lock{ mWorldRwLock };
        const T* Component{ GetComponentUnsafe<T>(Id) };

        if (Component == nullptr) {
            return false;
        }

        std::forward<Func>(Reader)(*Component);
        return true;
    }

    template <TrivialComponent T, typename Func>
    bool World::WriteComponent(EntityID Id, Func&& Writer) {
        std::unique_lock<std::shared_mutex> Lock{ mWorldRwLock };
        T* Component{ GetComponentUnsafe<T>(Id) };

        if (Component == nullptr) {
            return false;
        }

        std::forward<Func>(Writer)(*Component);
        return true;
    }

    template <TrivialComponent... Ts>
    std::vector<Archetype*>* World::GetTargetArchetypes() {
        std::vector<TypeID> QuerySig{ TypeInfo<Ts>::ID... };
        std::sort(QuerySig.begin(), QuerySig.end());

        for (QueryCache& Cache : mQueryCaches) {
            if (Cache.mSignature == QuerySig) {
                return &Cache.mArchetypes;
            }
        }

        QueryCache NewCache{};
        NewCache.mSignature = QuerySig;

        for (const std::unique_ptr<Archetype>& Arch : mArcheTypes) {
            const std::vector<TypeID>& Signature{ Arch->GetSignature() };

            if (std::includes(Signature.begin(), Signature.end(), QuerySig.begin(), QuerySig.end())) {
                NewCache.mArchetypes.emplace_back(Arch.get());
            }
        }

        mQueryCaches.emplace_back(std::move(NewCache));
        return &mQueryCaches.back().mArchetypes;
    }

    template <TrivialComponent... Ts, typename Func>
    void World::ForEach(Func&& FuncObject) {
        std::shared_lock<std::shared_mutex> Lock{ mWorldRwLock };

        for (Archetype* Arch : *GetTargetArchetypes<Ts...>()) {
            for (Chunk* ChunkPtr : Arch->GetChunks()) {
                std::tuple<Ts*...> Pointers{ static_cast<Ts*>(Arch->GetBaseComponentArray(ChunkPtr, TypeInfo<Ts>::ID))... };
                size_t Count{ ChunkPtr->count };

                for (size_t Index{ 0 }; Index < Count; ++Index) {
                    std::apply([&](auto*... Ptrs) { FuncObject(Ptrs[Index]...); }, Pointers);
                }
            }
        }
    }

    template <TrivialComponent... Ts>
    World::QueryView<Ts...> World::Query() {
        std::shared_lock<std::shared_mutex> Lock{ mWorldRwLock };
        return QueryView<Ts...>(GetTargetArchetypes<Ts...>());
    }

    template <TrivialComponent... Ts>
    void World::DeferCreateEntity(Ts... Args) {
        std::lock_guard<std::mutex> Lock{ mDeferredQueueLock };
        std::tuple<std::decay_t<Ts>...> Data{ std::forward<Ts>(Args)... };

        mDeferredStructuralCommands.emplace_back([Data](World& TargetWorld) mutable {
            std::apply([&](auto&&... Items) { TargetWorld.CreateEntityInternal<std::decay_t<Ts>...>(std::forward<decltype(Items)>(Items)...); }, std::move(Data));
        });
    }

    template <TrivialComponent T>
    void World::DeferAddComponent(EntityID Id, T Component) {
        std::lock_guard<std::mutex> Lock{ mDeferredQueueLock };
        std::decay_t<T> Data{ std::move(Component) };

        mDeferredStructuralCommands.emplace_back([Id, Data](World& TargetWorld) mutable {
            TargetWorld.AddComponentInternal<std::decay_t<T>>(Id, std::move(Data));
        });
    }

    template <TrivialComponent... Ts>
    EntityID World::CreateEntityInternal(Ts... Args) {
        std::uint32_t Index{};

        if (!mFreeIndices.empty()) {
            Index = mFreeIndices.back();
            mFreeIndices.pop_back();
        }
        else {
            Index = static_cast<std::uint32_t>(mEntityTable.size());
            mEntityTable.resize(Index + 1);
            mEntityTable[Index].generation = 0;
        }

        EntityRecord& Record{ mEntityTable[Index] };
        Record.active = true;
        EntityID Id{ Index, Record.generation };

        constexpr size_t Count{ sizeof...(Ts) };

        struct ComponentMeta {
            TypeID mId;
            size_t mSize;
            size_t mAlign;
            const void* mPtr;
        };

        std::array<ComponentMeta, Count> Meta{ { { TypeInfo<Ts>::ID, sizeof(Ts), alignof(Ts), &Args }... } };
        std::sort(Meta.begin(), Meta.end(), [](const ComponentMeta& Left, const ComponentMeta& Right) { return Left.mId < Right.mId; });

        std::array<TypeID, Count> IDs{};
        std::array<size_t, Count> Sizes{};
        std::array<size_t, Count> Aligns{};
        std::vector<std::pair<TypeID, const void*>> Pointers{};
        Pointers.reserve(Count);

        for (size_t MetaIndex{ 0 }; MetaIndex < Count; ++MetaIndex) {
            IDs[MetaIndex] = Meta[MetaIndex].mId;
            Sizes[MetaIndex] = Meta[MetaIndex].mSize;
            Aligns[MetaIndex] = Meta[MetaIndex].mAlign;
            Pointers.emplace_back(Meta[MetaIndex].mId, Meta[MetaIndex].mPtr);
        }

        Archetype* Arch{ GetOrCreateArchetype(IDs, Sizes, Aligns) };
        Arch->PushEntity(Id, Pointers);

        Record.archetype = Arch;
        Record.chunkIndex = static_cast<std::uint32_t>(Arch->GetChunks().size() - 1);
        Record.entityIndex = Arch->GetChunks().back()->count - 1;
        TouchStructureVersion();
        return Id;
    }

    template <TrivialComponent T>
    void World::AddComponentInternal(EntityID Id, T Component) {
        if (Id.index >= mEntityTable.size()) {
            return;
        }

        EntityRecord& Record{ mEntityTable[Id.index] };

        if (!Record.active || Record.generation != Id.generation) {
            return;
        }

        Archetype* OldArch{ Record.archetype };

        if (OldArch->HasType(TypeInfo<T>::ID)) {
            return;
        }

        std::vector<TypeID> IDs{};
        std::vector<size_t> Sizes{};
        std::vector<size_t> Aligns{};
        GetArchetypeInfo(OldArch, IDs, Sizes, Aligns);

        IDs.emplace_back(TypeInfo<T>::ID);
        Sizes.emplace_back(sizeof(T));
        Aligns.emplace_back(alignof(T));

        struct Meta {
            TypeID mId;
            size_t mSize;
            size_t mAlign;
        };

        std::vector<Meta> Combined{};

        for (size_t Index{ 0 }; Index < IDs.size(); ++Index) {
            Combined.emplace_back(IDs[Index], Sizes[Index], Aligns[Index]);
        }

        std::sort(Combined.begin(), Combined.end(), [](const Meta& Left, const Meta& Right) { return Left.mId < Right.mId; });

        IDs.clear();
        Sizes.clear();
        Aligns.clear();

        for (const Meta& Value : Combined) {
            IDs.emplace_back(Value.mId);
            Sizes.emplace_back(Value.mSize);
            Aligns.emplace_back(Value.mAlign);
        }

        Archetype* NewArch{ GetOrCreateArchetype(IDs, Sizes, Aligns) };
        std::vector<std::pair<TypeID, const void*>> MoveData{};
        Chunk* OldChunk{ OldArch->GetChunks()[Record.chunkIndex] };

        for (TypeID CurrentTypeID : OldArch->GetSignature()) {
            MoveData.emplace_back(CurrentTypeID, OldArch->GetComponentPtr(OldChunk, Record.entityIndex, CurrentTypeID));
        }

        MoveData.emplace_back(TypeInfo<T>::ID, &Component);
        NewArch->PushEntity(Id, MoveData);

        EntityID MovedID{ OldArch->PopEntity(Record.chunkIndex, Record.entityIndex) };

        if (MovedID != Id) {
            EntityRecord& MovedRecord{ mEntityTable[MovedID.index] };
            MovedRecord.chunkIndex = Record.chunkIndex;
            MovedRecord.entityIndex = Record.entityIndex;
        }

        Record.archetype = NewArch;
        Record.chunkIndex = static_cast<std::uint32_t>(NewArch->GetChunks().size() - 1);
        Record.entityIndex = NewArch->GetChunks().back()->count - 1;
        TouchStructureVersion();
    }

    template <TrivialComponent T>
    T* World::GetComponentUnsafe(EntityID Id) {
        if (Id.index >= mEntityTable.size()) {
            return nullptr;
        }

        EntityRecord& Record{ mEntityTable[Id.index] };

        if (!Record.active || Record.generation != Id.generation) {
            return nullptr;
        }

        if (Record.archetype == nullptr) {
            return nullptr;
        }

        const std::vector<Chunk*>& Chunks{ Record.archetype->GetChunks() };

        if (Record.chunkIndex >= Chunks.size()) {
            return nullptr;
        }

        Chunk* ChunkPtr{ Chunks[Record.chunkIndex] };

        if (Record.entityIndex >= ChunkPtr->count) {
            return nullptr;
        }

        return static_cast<T*>(Record.archetype->GetComponentPtr(ChunkPtr, Record.entityIndex, TypeInfo<T>::ID));
    }

    template <TrivialComponent T>
    const T* World::GetComponentUnsafe(EntityID Id) const {
        return const_cast<World*>(this)->GetComponentUnsafe<T>(Id);
    }

    template <TrivialComponent... Ts>
    World::QueryView<Ts...>::Iterator::Iterator(std::vector<Archetype*>::const_iterator It, std::vector<Archetype*>::const_iterator End)
        : mArchIt{ It }, mArchEnd{ End }, mChunkIt{}, mEntityIndex{ 0 }, mCurrentTargetArray{} {
        if (mArchIt != mArchEnd) {
            mChunkIt = (*mArchIt)->GetChunks().begin();
        }

        AdvanceToValidChunk();
    }

    template <TrivialComponent... Ts>
    typename World::QueryView<Ts...>::Iterator& World::QueryView<Ts...>::Iterator::operator++() {
        ++mEntityIndex;

        if (mEntityIndex >= (*mChunkIt)->count) {
            mEntityIndex = 0;
            ++mChunkIt;
            AdvanceToValidChunk();
        }

        return *this;
    }

    template <TrivialComponent... Ts>
    bool World::QueryView<Ts...>::Iterator::operator==(const Iterator& Other) const {
        if (mArchIt != Other.mArchIt) {
            return false;
        }

        if (mArchIt == mArchEnd) {
            return true;
        }

        return mChunkIt == Other.mChunkIt && mEntityIndex == Other.mEntityIndex;
    }

    template <TrivialComponent... Ts>
    bool World::QueryView<Ts...>::Iterator::operator!=(const Iterator& Other) const {
        return !(*this == Other);
    }

    template <TrivialComponent... Ts>
    typename World::QueryView<Ts...>::Iterator::ReferenceTuple World::QueryView<Ts...>::Iterator::operator*() const {
        return std::apply([this](auto*... Pointers) { return std::forward_as_tuple(Pointers[mEntityIndex]...); }, mCurrentTargetArray);
    }

    template <TrivialComponent... Ts>
    void World::QueryView<Ts...>::Iterator::AdvanceToValidChunk() {
        while (mArchIt != mArchEnd) {
            const std::vector<Chunk*>& Chunks{ (*mArchIt)->GetChunks() };

            while (mChunkIt != Chunks.end()) {
                if ((*mChunkIt)->count > 0) {
                    UpdatePointers();
                    return;
                }

                ++mChunkIt;
            }

            ++mArchIt;
            mEntityIndex = 0;

            if (mArchIt != mArchEnd) {
                mChunkIt = (*mArchIt)->GetChunks().begin();
            }
        }
    }

    template <TrivialComponent... Ts>
    void World::QueryView<Ts...>::Iterator::UpdatePointers() {
        Archetype* Arch{ *mArchIt };
        Chunk* ChunkPtr{ *mChunkIt };

        mCurrentTargetArray = std::make_tuple(static_cast<Ts*>(Arch->GetBaseComponentArray(ChunkPtr, TypeInfo<Ts>::ID))...);
    }

    template <TrivialComponent... Ts>
    World::QueryView<Ts...>::QueryView(const std::vector<Archetype*>* Archetypes)
        : mArcheTypes{ Archetypes } {
    }

    template <TrivialComponent... Ts>
    typename World::QueryView<Ts...>::Iterator World::QueryView<Ts...>::begin() {
        if (mArcheTypes == nullptr) {
            return Iterator{ std::vector<Archetype*>::const_iterator{}, std::vector<Archetype*>::const_iterator{} };
        }

        return Iterator{ mArcheTypes->cbegin(), mArcheTypes->cend() };
    }

    template <TrivialComponent... Ts>
    typename World::QueryView<Ts...>::Iterator World::QueryView<Ts...>::end() {
        if (mArcheTypes == nullptr) {
            return Iterator{ std::vector<Archetype*>::const_iterator{}, std::vector<Archetype*>::const_iterator{} };
        }

        return Iterator{ mArcheTypes->cend(), mArcheTypes->cend() };
    }
}
