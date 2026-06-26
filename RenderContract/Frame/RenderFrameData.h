#pragma once

#include <array>
#include <vector>

#include "DirectXTK12/SimpleMath.h"

#include "RenderContract/Environment/EnvironmentDrawRecord.h"
#include "RenderContract/Environment/EnvironmentDrawRecordGpu.h"
#include "RenderContract/Environment/EnvironmentGpuDrivenFrameData.h"
#include "RenderContract/Environment/EnvironmentInstanceContext.h"
#include "RenderContract/Environment/EnvironmentRenderRuntime.h"
#include "RenderContract/Environment/EnvironmentSegmentContext.h"
#include "RenderContract/Frame/CameraParameter.h"
#include "RenderContract/Frame/FrameGlobals.h"
#include "RenderContract/Frame/ShadowMappingParameter.h"
#include "RenderContract/Future/Future.h"
#include "RenderContract/Geometry/BoundingBoxContext.h"
#include "RenderContract/Geometry/DebugGeometryContext.h"
#include "RenderContract/Material/MaterialGpu.h"
#include "RenderContract/Material/MaterialTextureTableItemGpu.h"
#include "RenderContract/Model/DrawRecord.h"
#include "RenderContract/Model/ModelContext.h"
#include "RenderContract/Shadow/ShadowRenderContext.h"
#include "RenderContract/Terrain/TerrainPatchContext.h"

namespace RenderContract {
    struct RenderFrameData final {
    public:
        FrameGlobals mFrameGlobals{};
        CameraParameter mMainCamera{};
        ShadowMappingParameter mShadowMappingParameter{};
        std::vector<ModelContext> mModelContexts{};
        std::vector<BoundingBoxContext> mBoundingBoxContexts{};
        std::vector<DebugGeometryContext> mDebugGeometryContexts{};
        std::vector<TerrainPatchContext> mTerrainPatchContexts{};
        std::vector<DrawRecord> mDrawRecords{};
        std::vector<MaterialGpu> mMaterials{};
        std::vector<MaterialTextureTableItemGpu> mMaterialTextureTable{};
        std::vector<DirectX::SimpleMath::Matrix> mBonePalette{};
        std::vector<EnvironmentInstanceContext> mEnvironmentInstanceContexts{};
        std::vector<EnvironmentSegmentContext> mEnvironmentSegmentContexts{};
        std::vector<EnvironmentDrawRecord> mEnvironmentDrawRecords{};
        EnvironmentGpuDrivenFrameData mEnvironmentGpuDrivenFrame{};
        IEnvironmentRenderRuntime* mEnvironmentRuntime{};
        Future mTerrainUploadFuture{};
        bool mHasTerrainUploadFuture{};
        std::array<ShadowRenderContext, ShadowCascadeMaxCount> mShadowRenderContexts{};
    };
}
