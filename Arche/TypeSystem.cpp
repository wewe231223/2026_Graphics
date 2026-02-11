#include <unordered_map>
#include <mutex>
#include "TypeSystem.h"

namespace Arche {
    class TypeRegistry {
    private:
        std::unordered_map<std::size_t, TypeID> hashToIndexMap;
        std::size_t nextIndex{ 0 };
        std::mutex mtx;

    public:
        static TypeRegistry& Instance() {
            static TypeRegistry instance;
            return instance;
        }

        TypeID GetID(std::size_t typeHash) {
            std::lock_guard<std::mutex> lock(mtx);

            auto it = hashToIndexMap.find(typeHash);
            if (it != hashToIndexMap.end()) {
                return it->second;
            }

            TypeID newID = nextIndex++;
            hashToIndexMap[typeHash] = newID;
            return newID;
        }
    };

    TypeID GetRuntimeTypeID(std::size_t typeHash) {
        return TypeRegistry::Instance().GetID(typeHash);
    }

} // namespace Arche
