#pragma once

#include <cstdint>
#include <string>

#include "DirectXTK12/SimpleMath.h"

namespace RenderContract {
    struct ModelBoneInfo final {
    public:
        std::uint32_t mSkinArrayIndex{};
        std::uint32_t mJointArrayIndex{};
        std::string mBoneName{};
        DirectX::SimpleMath::Matrix mInverseBindMatrix{};
    };
}
