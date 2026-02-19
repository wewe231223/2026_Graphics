#pragma once
#include <vector>
#include <memory>
#include <algorithm>
#include <tuple>
#include <deque>
#include <array>
#include <span>
#include "Common.h"
#include "Archetype.h"

namespace Arche {
    class World {
    private:
        std::vector<EntityRecord> mEntityTable{};
        std::vector<std::uint32_t> mFreeIndices{};
        std::vector<std::unique_ptr<Archetype>> mArcheTypes{};
        struct QueryCache { std::vector<TypeID> signature{}; std::vector<Archetype*> archetypes{}; };
        std::deque<QueryCache> mQueryCaches{};

    private:
        
        Archetype* GetOrCreateArchetype(std::span<const TypeID> sortedIDs, std::span<const size_t> sizes, std::span<const size_t> aligns);        
        void GetArchetypeInfo(Archetype* arch, std::vector<TypeID>& outIds, std::vector<size_t>& outSizes, std::vector<size_t>& outAligns);

    public:
        World() = default;
        ~World() = default;

		World(const World&) = delete;
		World& operator=(const World&) = delete;

		World(World&&) noexcept = default;
		World& operator=(World&&) noexcept = default;

    public:
        template <typename... Ts>
        EntityID CreateEntity(Ts... args) {
            std::uint32_t idx;

            if (!mFreeIndices.empty()) { 
                idx = mFreeIndices.back();
                mFreeIndices.pop_back();
            }
            else { 
                idx = static_cast<std::uint32_t>(mEntityTable.size());
                mEntityTable.resize(idx + 1);
                mEntityTable[idx].generation = 0;
            }

            EntityRecord& record{ mEntityTable[idx] };
            record.active = true;
            EntityID id{ idx, record.generation }; 

            
            constexpr size_t Count = sizeof...(Ts);

            struct ComponentMeta { TypeID id; size_t size; size_t align; const void* ptr; };
            std::array<ComponentMeta, Count> meta = { {
                { TypeInfo<Ts>::ID, sizeof(Ts), alignof(Ts), &args }...
            } };

            std::sort(meta.begin(), meta.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

            std::array<TypeID, Count> ids;
            std::array<size_t, Count> sizes;
            std::array<size_t, Count> aligns;
            std::vector<std::pair<TypeID, const void*>> ptrs;
            ptrs.reserve(Count);

            for (size_t i = 0; i < Count; ++i) {
                ids[i] = meta[i].id;
                sizes[i] = meta[i].size;
                aligns[i] = meta[i].align;
                ptrs.emplace_back(meta[i].id, meta[i].ptr);
            }

			
            Archetype* arch{ GetOrCreateArchetype(ids, sizes, aligns) };

            
            arch->PushEntity(id, ptrs);
            record.archetype = arch;
            record.chunkIndex = static_cast<std::uint32_t>(arch->GetChunks().size() - 1);
            record.entityIndex = arch->GetChunks().back()->count - 1;

            return id;
        }

        template <typename... Ts>
        EntityID CreateEntity() {
            return CreateEntity(Ts{}...);
        }

        template <typename T>
        void AddComponent(EntityID id, T component) {
            if (id.index >= mEntityTable.size()) { 
                return;
            }

            EntityRecord& record = mEntityTable[id.index];
			if (!record.active or record.generation != id.generation) { 
                return;
            }

            Archetype* oldArch{ record.archetype };
			if (oldArch->HasType(TypeInfo<T>::ID)) { 
                return;
            }

            std::vector<TypeID> ids; 
            std::vector<size_t> sizes; 
            std::vector<size_t> aligns;
            GetArchetypeInfo(oldArch, ids, sizes, aligns); 

            
			ids.emplace_back(TypeInfo<T>::ID); 
            sizes.emplace_back(sizeof(T)); 
            aligns.emplace_back(alignof(T));

            
            struct Meta { TypeID id; size_t s; size_t a; };
            std::vector<Meta> combined;
            for (size_t i = 0; i < ids.size(); ++i) {
                combined.emplace_back(ids[i], sizes[i], aligns[i]);
            }
            std::sort(combined.begin(), combined.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

            ids.clear(); 
            sizes.clear(); 
            aligns.clear();

            for (const auto& c : combined) { 
                ids.emplace_back(c.id); 
                sizes.emplace_back(c.s); 
                aligns.emplace_back(c.a); 
            }

			
            Archetype* newArch{ GetOrCreateArchetype(ids, sizes, aligns) };

            
            std::vector<std::pair<TypeID, const void*>> moveData;
            Chunk* oldChunk = oldArch->GetChunks()[record.chunkIndex];

            
            for (TypeID tId : oldArch->GetSignature()) {
                moveData.emplace_back(tId, oldArch->GetComponentPtr(oldChunk, record.entityIndex, tId));
            }
            
            moveData.emplace_back(TypeInfo<T>::ID, &component);

            
            newArch->PushEntity(id, moveData);

			
            EntityID movedID = oldArch->PopEntity(record.chunkIndex, record.entityIndex);
            if (movedID != id) {
                EntityRecord& movedRecord = mEntityTable[movedID.index];
                movedRecord.chunkIndex = record.chunkIndex; movedRecord.entityIndex = record.entityIndex;
            }
            record.archetype = newArch;
            record.chunkIndex = static_cast<std::uint32_t>(newArch->GetChunks().size() - 1);
            record.entityIndex = newArch->GetChunks().back()->count - 1;
        }

        template <typename... Ts>
        std::vector<Archetype*>* GetTargetArchetypes() {
			
            std::vector<TypeID> querySig = { TypeInfo<Ts>::ID... }; 
            std::sort(querySig.begin(), querySig.end());

            
            for (auto& cache : mQueryCaches) {
                if (cache.signature == querySig) { 
                    return &cache.archetypes;
                }
            }

            
            QueryCache newCache;
            newCache.signature = querySig;
            for (auto& arch : mArcheTypes) {
                const auto& as = arch->GetSignature();
				
                if (std::includes(as.begin(), as.end(), querySig.begin(), querySig.end())) {
                    newCache.archetypes.emplace_back(arch.get());
                }
            }
            mQueryCaches.emplace_back(std::move(newCache));

            return &mQueryCaches.back().archetypes;
        }

        
        template <typename... Ts, typename Func>
        void ForEach(Func&& func) {
            for (Archetype* arch : *World::GetTargetArchetypes()) {
                for (Chunk* chunk : arch->GetChunks()) {
                    std::tuple<Ts*...> pointers = { static_cast<Ts*>(arch->GetBaseComponentArray(chunk, TypeInfo<Ts>::ID))... };
                    size_t count{ chunk->count };
                    for (size_t i = 0; i < count; ++i) {
                        std::apply([&](auto*... ptrs) { func(ptrs[i]...); }, pointers);
                    }
                }
            }
        }

        void DestroyEntity(EntityID id);

        template <typename T>
        T* GetComponent(EntityID id) {
			if (id.index >= mEntityTable.size()) { 
                return nullptr;
            }

            EntityRecord& r{ mEntityTable[id.index] };

			if (!r.active or r.generation != id.generation) { 
                return nullptr;
            }

            if (r.archetype == nullptr) {
                return nullptr;
            }

            const std::vector<Chunk*>& Chunks{ r.archetype->GetChunks() };
            if (r.chunkIndex >= Chunks.size()) {
                return nullptr;
            }

            Chunk* c{ Chunks[r.chunkIndex] };
            if (r.entityIndex >= c->count) {
                return nullptr;
            }

            return static_cast<T*>(r.archetype->GetComponentPtr(c, r.entityIndex, TypeInfo<T>::ID));
        }

        
        
        template <typename... Ts>
        class QueryView {
        public:
            
            struct Iterator {
                using pointer_tuple = std::tuple<Ts*...>;
                using reference_tuple = std::tuple<Ts&...>;

            private:
                std::vector<Archetype*>::iterator mArchIt{};
                std::vector<Archetype*>::iterator mArchEnd{};
                std::vector<Chunk*>::const_iterator mChunkIt{};
                std::uint32_t mEntityIndex{};
                pointer_tuple mCurrentTargetArray{};

            public:
                Iterator(std::vector<Archetype*>::iterator it, std::vector<Archetype*>::iterator end)
                    : mArchIt(it), mArchEnd(end), mEntityIndex(0) {

                    if (mArchIt != mArchEnd) {
                        mChunkIt = (*mArchIt)->GetChunks().begin();
                    }

                    AdvanceToValidChunk();
                }

                Iterator(const Iterator&) = default;
				Iterator& operator=(const Iterator&) = default;

				Iterator(Iterator&&) noexcept = default;
				Iterator& operator=(Iterator&&) noexcept = default;

                Iterator& operator++() {
                    mEntityIndex++;
                    
                    if (mEntityIndex >= (*mChunkIt)->count) {
                        mEntityIndex = 0;
                        ++mChunkIt; 
                        AdvanceToValidChunk(); 
                    }
                    return *this;
                }

                bool operator==(const Iterator& other) const {
                    if (mArchIt != other.mArchIt) {
                        return false;
                    }
                    if (mArchIt == mArchEnd) {
                        return true;
                    }
                    return mChunkIt == other.mChunkIt and mEntityIndex == other.mEntityIndex;
                }

                bool operator!=(const Iterator& other) const {
                    return !(*this == other);
                }

                reference_tuple operator*() const { 
                    return std::apply([this](auto*... ptrs) {
                        return std::forward_as_tuple(ptrs[mEntityIndex]...);
                        }, mCurrentTargetArray);
                }

            private:
                
                void AdvanceToValidChunk() {
                    while (mArchIt != mArchEnd) {
                        const auto& chunks = (*mArchIt)->GetChunks();

                        
                        while (mChunkIt != chunks.end()) { 
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

                void UpdatePointers() {
                    Archetype* arch{ *mArchIt };
                    Chunk* chunk{ *mChunkIt };

                    
                    mCurrentTargetArray = std::make_tuple(
                        static_cast<Ts*>(arch->GetBaseComponentArray(chunk, TypeInfo<Ts>::ID))...
                    );
                }
            };

        private:
            std::vector<Archetype*>* mArcheTypes{};

		public:
            QueryView(std::vector<Archetype*>* archs) : mArcheTypes(archs) {}
			~QueryView() = default;

			QueryView(const QueryView&) = default;
			QueryView& operator=(const QueryView&) = default;

			QueryView(QueryView&&) noexcept = default;
			QueryView& operator=(QueryView&&) noexcept = default;

        public:
            Iterator begin() { 
                return Iterator(mArcheTypes->begin(), mArcheTypes->end()); 
            }

            Iterator end() { 
                return Iterator(mArcheTypes->end(), mArcheTypes->end()); 
            }
        };


        template <typename... Ts>
        QueryView<Ts...> Query() {
            return QueryView<Ts...>(GetTargetArchetypes<Ts...>());
        }

    };
}
