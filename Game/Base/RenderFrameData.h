#pragma once
#include <cstdint>
#include <vector>
#include <DirectXTK12/SimpleMath.h>
#include "Game/Base/Common.h"

namespace SimpleMath = DirectX::SimpleMath;

namespace Game {
    namespace RFD {
        constexpr std::uint32_t FrameGlobalFlagDrawBoundingBoxes{ 0x1u };

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
            static constexpr std::uint32_t FieldCount{ 30 };
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

        // ------------------------------------------------------------
        // 2) 오브젝트 공통 컨텍스트 (오브젝트당 1개)
        //    - 이 배열은 SRV(StructuredBuffer)로 올려서 셰이더가 인덱싱
        // ------------------------------------------------------------
        struct alignas(16) ModelContext {
            SimpleMath::Matrix world {};
            SimpleMath::Matrix prevWorld {};

            SimpleMath::Vector4 bbCenter {};
            SimpleMath::Vector4 bbExtents {};

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
        struct DrawRecord {
            const Interface::IPipeline* pso{ nullptr };
            const Interface::IModelNode* mesh{ nullptr };
            uint32_t submesh{ 0 };            // mesh->submeshes[submesh]로 indexStart/indexCount 획득
            uint32_t pass{ 0 };               // optional (Opaque/Shadow/etc)

            uint32_t objectIndex{ 0 };
            uint32_t materialIndex{ 0 };
            uint32_t flags{ 0 };
            uint32_t pad0{ 0 };
        };

        // ------------------------------------------------------------
        // 5) Scene -> Renderer 프레임 패킷
        //    - Renderer는 drawRecords를 정렬 후 contiguous run을 DrawIndexedInstanced로 소비
        //    - modelContexts는 GPU에 업로드 후 셰이더가 인덱싱
        // ------------------------------------------------------------
        struct RenderFrameData {
            FrameGlobals globals{};

            std::vector<ModelContext> modelContexts{};   // SRV
            std::vector<DrawRecord> drawRecords{};       // CPU
            std::vector<MaterialGpu> materials{};
            std::vector<MaterialTextureTableItemGpu> materialTextureTable{};
            std::vector<SimpleMath::Matrix> bonePalette{};


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
