#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <DirectXCollision.h>
#include <DirectXTK12/SimpleMath.h>
#include "Asset/Common.h"
#include "Game/Base/Common.h"

namespace SimpleMath = DirectX::SimpleMath;

namespace Game {
    namespace RFD {
        constexpr std::uint32_t FrameGlobalFlagDrawBoundingBoxes{ 0x1u };
        constexpr std::uint32_t FrameGlobalFlagDrawDebugGeometry{ 0x2u };
        constexpr std::uint32_t ShadowCascadeMaxCount{ 4u };
        constexpr std::uint32_t DebugGeometryTypeLine{ 0u };
        constexpr std::uint32_t DebugGeometryTypeWireCube{ 1u };

        struct alignas(16) MaterialFieldGpu final {
            std::uint32_t Type{ 0 };
            std::uint32_t Padding0{ 0 };
            std::uint32_t Padding1{ 0 };
            std::uint32_t Padding2{ 0 };
            SimpleMath::Vector4 FloatValue{};
            std::int64_t IntValue{ -1 };
            std::uint64_t Padding3{ 0 };
        };

        struct alignas(16) MaterialGpu final {
            static constexpr std::uint32_t FieldCount{ asset::MaterialTypeCount };
            MaterialFieldGpu Fields[FieldCount]{};
        };

        struct alignas(16) MaterialTextureTableItemGpu final {
            std::uint32_t TextureSrvDescriptorIndex{ 0 };
            std::uint32_t Padding0{ 0 };
            std::uint32_t Padding1{ 0 };
            std::uint32_t Padding2{ 0 };
        };

        // ------------------------------------------------------------
        // 1) 프레임 공통
        // ------------------------------------------------------------
        struct alignas(16) FrameGlobals {
            SimpleMath::Matrix view = {};
            SimpleMath::Matrix proj = {};
            SimpleMath::Matrix viewProj = {};
            SimpleMath::Matrix prevViewProj = {};

            float dt = 0.0f;
            uint32_t frameIndex = 0;
            uint32_t flags = 0;
            uint32_t _pad0 = 0;
        };

        struct alignas(16) CameraParameter final {
            SimpleMath::Matrix view{};
            SimpleMath::Matrix proj{};
            SimpleMath::Matrix viewProj{};
            SimpleMath::Vector4 position{};
            float nearPlane{ 0.0f };
            float farPlane{ 0.0f };
            float aspectRatio{ 0.0f };
            float fovRadians{ 0.0f };
        };

        struct alignas(16) ShadowMappingParameter final {
            CameraParameter shadowCameras[ShadowCascadeMaxCount]{};
            SimpleMath::Vector4 lightDirection{};
            SimpleMath::Vector4 cascadeSplitDistances{};
            float shadowBiases[ShadowCascadeMaxCount]{ 0.0010f, 0.0010f, 0.0010f, 0.0010f };
            float shadowStrengths[ShadowCascadeMaxCount]{ 0.6f, 0.6f, 0.6f, 0.6f };
            float shadowMapSizes[ShadowCascadeMaxCount]{ 4096.0f, 2048.0f, 2048.0f, 2048.0f };
            float rasterDepthBiases[ShadowCascadeMaxCount]{ 1.0f, 1.0f, 1.0f, 1.0f };
            float rasterSlopeScaledDepthBiases[ShadowCascadeMaxCount]{ 1.25f, 1.25f, 1.25f, 1.25f };
            std::uint32_t cascadeCount{ ShadowCascadeMaxCount };
            float padding0{ 0.0f };
            float padding1{ 0.0f };
            float padding2{ 0.0f };
        };

        static_assert(sizeof(ShadowMappingParameter) == 1024);

        // ------------------------------------------------------------
        // 2) 오브젝트 공통 컨텍스트 (오브젝트당 1개)
        //    - 이 배열은 SRV(StructuredBuffer)로 올려서 셰이더가 인덱싱
        // ------------------------------------------------------------
        struct alignas(16) ModelContext {
            SimpleMath::Matrix world {};
            SimpleMath::Matrix prevWorld {};

            uint32_t flags{ 0 }; // skinned, castsShadow, etc
            uint32_t boneIndexStart{ 0 };
            uint32_t objectID{ 0 };
            uint32_t pad0{ 0 };

            SimpleMath::Vector4 custom0 {};
            SimpleMath::Vector4 custom1 {};
        };

        // ------------------------------------------------------------
        // 3) CPU 드로우 레코드
        //    - 렌더 루프에서 상태 설정에 필요한 핸들/포인터 보관
        // ------------------------------------------------------------
        /*
        MaterialGroup -> DrawRecord 매핑 체계

        Material(MaterialGroupIndex)
            └─ RegisteredMaterialGroup = MaterialGroups[MaterialGroupIndex]
                   └─ RegisteredGroupItem = RegisteredMaterialGroup.Items[SubMesh.MaterialGroupItemIndex]
                          ├─ Pipeline      -> DrawRecord.pso
                          └─ MaterialIndex -> DrawRecord.materialIndex

        핵심: Model/SubMesh 는 MaterialGroup 내부 인덱스만 보관하고,
        DrawRecord 생성 시점에 전역 Material 인덱스로 매핑한다.
        */

        struct alignas(16) BoundingBoxContext final {
            SimpleMath::Vector4 center{};
            SimpleMath::Vector4 extents{};
            SimpleMath::Vector4 orientation{};
        };

        struct alignas(16) DebugGeometryContext final {
            SimpleMath::Vector4 parameter0{};
            SimpleMath::Vector4 parameter1{};
            SimpleMath::Vector4 parameter2{};
            SimpleMath::Vector4 color{};
            std::uint32_t type{ DebugGeometryTypeLine };
            float lineThickness{ 0.0025f };
            std::uint32_t padding0{ 0u };
            std::uint32_t padding1{ 0u };

            void SetLine(const SimpleMath::Vector3& StartPosition, const SimpleMath::Vector3& EndPosition, const SimpleMath::Vector4& ColorValue, float LineThicknessValue);
            void SetDirection(const SimpleMath::Vector3& Origin, const SimpleMath::Vector3& Direction, float Length, const SimpleMath::Vector4& ColorValue, float LineThicknessValue);
            void SetWireCube(const SimpleMath::Vector3& CenterValue, const SimpleMath::Vector3& ExtentsValue, const SimpleMath::Quaternion& OrientationValue, const SimpleMath::Vector4& ColorValue, float LineThicknessValue);

            static DebugGeometryContext CreateLine(const SimpleMath::Vector3& StartPosition, const SimpleMath::Vector3& EndPosition, const SimpleMath::Vector4& ColorValue, float LineThicknessValue);
            static DebugGeometryContext CreateDirection(const SimpleMath::Vector3& Origin, const SimpleMath::Vector3& Direction, float Length, const SimpleMath::Vector4& ColorValue, float LineThicknessValue);
            static DebugGeometryContext CreateWireCube(const SimpleMath::Vector3& CenterValue, const SimpleMath::Vector3& ExtentsValue, const SimpleMath::Quaternion& OrientationValue, const SimpleMath::Vector4& ColorValue, float LineThicknessValue);
        };

        struct alignas(16) TerrainPatchContext final {
            SimpleMath::Vector4 OuterTessFactors{};
            SimpleMath::Vector4 InsideTessFactors{};
            SimpleMath::Vector4 TileGrid{};
            SimpleMath::Vector4 HeightFieldParameters{};
            SimpleMath::Vector4 TerrainParameters{};
            SimpleMath::Vector4 mTerrainUvParameters{};
            std::uint32_t HeightFieldSrvDescriptorIndex{ 0xffffffffu };
            std::uint32_t SplatMapSrvDescriptorIndex{ 0xffffffffu };
            std::uint32_t SplatMapWidth{ 0u };
            std::uint32_t SplatMapHeight{ 0u };
        };

        struct DrawRecord {
            const Interface::IPipeline* pso{ nullptr };
            const Interface::IModelNode* mesh{ nullptr };
            uint32_t submesh{ 0 };            // mesh->submeshes[submesh]로 indexStart/indexCount 획득
            uint32_t pass{ 0 };               // optional (Opaque/Shadow/etc)

            uint32_t objectIndex{ 0 };
            uint32_t materialIndex{ 0 };
            uint32_t flags{ 0 };
            uint32_t TerrainPatchContextIndex{ 0xffffffffu };

            uint32_t pad0{ 0 };
        };

        struct ShadowRenderContext final {
            std::vector<ModelContext> ModelContexts{};
            std::vector<TerrainPatchContext> TerrainPatchContexts{};
            std::vector<DrawRecord> DrawRecords{};
        };

        std::uint32_t ResolveShadowCascadeCount(const ShadowMappingParameter& ShadowMappingParameter);
        std::array<DirectX::BoundingOrientedBox, ShadowCascadeMaxCount> BuildShadowCullingBoxes(const ShadowMappingParameter& ShadowMappingParameter);

        // ------------------------------------------------------------
        // 5) Scene -> Renderer 프레임 패킷
        //    - Renderer는 drawRecords를 정렬 후 contiguous run을 DrawIndexedInstanced로 소비
        //    - modelContexts는 GPU에 업로드 후 셰이더가 인덱싱
        // ------------------------------------------------------------
        struct RenderFrameData {
            FrameGlobals globals{};
            CameraParameter mainCamera{};
            ShadowMappingParameter shadowMapping{};

            std::vector<ModelContext> modelContexts{};   // SRV
            std::vector<BoundingBoxContext> boundingBoxContexts{};
            std::vector<DebugGeometryContext> debugGeometryContexts{};
            std::vector<TerrainPatchContext> TerrainPatchContexts{};
            std::vector<DrawRecord> drawRecords{};       // CPU
            std::vector<MaterialGpu> materials{};
            std::vector<MaterialTextureTableItemGpu> materialTextureTable{};
            std::vector<SimpleMath::Matrix> bonePalette{};
            std::array<ShadowRenderContext, ShadowCascadeMaxCount> ShadowRenderContexts{};


            // Render Loop 참고 순서
            // 1) drawRecords를 pass, pso, mesh, submesh 키로 정렬한다.
            // 2) 정렬된 순서에 맞춰 GPU 드로우 레코드를 재구성하고 업로드한다.
            // 3) drawRecords를 앞에서부터 순회하며 동일 키 구간(run)을 찾는다.
            // 4) 각 run 시작 레코드에서 pso와 mesh/submesh를 꺼내 상태를 설정한다.
            // 5) DrawIndexedInstanced 호출 인자는 다음처럼 잡는다.
            //    - IndexCountPerInstance: run 시작 레코드의 mesh/submesh에서 얻은 index count
            //    - InstanceCount: run 길이
            //    - StartIndexLocation/BaseVertexLocation: run 시작 레코드의 mesh/submesh에서 얻은 값
            //    - StartInstanceLocation: 0
            // 6) run 시작 인덱스는 루트 셰이더 상수(예: DrawRecordBaseIndex)로 셰이더에 전달한다.
            // 7) 셰이더에서는 drawIndex = DrawRecordBaseIndex + SV_InstanceID로 GPU 드로우 레코드를 조회한다.
            // 8) 조회한 record.objectIndex로 modelContexts를, record.materialIndex로 머티리얼 테이블을 인덱싱한다.
        };
    }
}
