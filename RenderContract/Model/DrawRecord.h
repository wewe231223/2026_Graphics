#pragma once

#include <cstdint>

namespace RenderContract {
    class IPipeline;
    class IModelNode;

    struct DrawRecord final {
    public:
        const IPipeline* mPipeline{};
        const IModelNode* mMesh{};
        std::uint32_t mSubMesh{};
        std::uint32_t mPass{};
        std::uint32_t mObjectIndex{};
        std::uint32_t mMaterialIndex{};
        std::uint32_t mFlags{};
        std::uint32_t mTerrainPatchContextIndex{ 0xffffffffu };
        std::uint32_t mPadding0{};
    };
}
