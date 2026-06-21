#pragma once

#include <cstdint>

namespace RenderContract {
    class IPipeline;
    class IModelNode;

    struct EnvironmentDrawRecord final {
    public:
        const IPipeline* mPipeline{};
        const IModelNode* mMesh{};
        std::uint32_t mSubMesh{};
        std::uint32_t mPass{};
        std::uint32_t mInstanceOffset{};
        std::uint32_t mInstanceCount{};
        std::uint32_t mSegmentContextIndex{};
        std::uint32_t mMaterialIndex{};
        std::uint32_t mFlags{};
        bool mCastsShadow{ true };
    };
}
