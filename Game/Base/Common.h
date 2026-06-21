#pragma once

#include <cstdint>

#include "DirectXTK12/SimpleMath.h"

#include "RenderContract/Common.h"
#include "RenderContract/Frame/FrameGlobals.h"

namespace Game {
    using ModelBoneInfo = RenderContract::ModelBoneInfo;
    using ModelSubMesh = RenderContract::ModelSubMesh;
    using PipelineOption = RenderContract::PipelineOption;
    using VertexAttributeKind = RenderContract::VertexAttributeKind;
    using VertexInputBinding = RenderContract::VertexInputBinding;

    struct RuntimeBoneInfo final {
        std::uint32_t mSkinArrayIndex{};
        std::uint32_t mJointArrayIndex{};
        DirectX::SimpleMath::Matrix mInverseBindMatrix{};
    };
}
