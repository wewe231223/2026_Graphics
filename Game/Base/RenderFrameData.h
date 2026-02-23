#pragma once
#include <cstdint>
#include <vector>
#include <DirectXTK12/SimpleMath.h>
#include "Game/Base/Common.h"

namespace SimpleMath = DirectX::SimpleMath;

namespace Game {
    namespace RFD {
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
        // 4) GPU 업로드용 드로우 레코드
        //    - 셰이더에서 인덱싱하는 페이로드만 보관
        // ------------------------------------------------------------
        struct DrawRecordGpu {
            uint32_t objectIndex{ 0 };        // ModelContext 인덱스
            uint32_t materialIndex{ 0 };      // Material 데이터 배열(또는 bindless 테이블) 인덱스
            uint32_t flags{ 0 };              // (선택) alpha-test/transparent 등
            uint32_t pad0{ 0 };
        };

        // ------------------------------------------------------------
        // 5) Scene -> Renderer 프레임 패킷
        //    - Renderer는 drawRecords를 정렬 후 contiguous run을 DrawIndexedInstanced로 소비
        //    - modelContexts/drawRecordsGpu는 GPU에 업로드 후 셰이더가 인덱싱
        // ------------------------------------------------------------
        struct RenderFrameData {
            FrameGlobals globals{};

            std::vector<ModelContext> modelContexts{};   // SRV
            std::vector<DrawRecord> drawRecords{};       // CPU
            std::vector<DrawRecordGpu> drawRecordsGpu{}; // SRV


            // Render Loop 참고 순서
            // 1) drawRecords를 pass, pso, mesh, submesh 키로 정렬한다.
            // 2) 정렬된 순서에 맞춰 drawRecordsGpu를 재구성하고 업로드한다.
            // 3) drawRecords를 앞에서부터 순회하며 동일 키 구간(run)을 찾는다.
            // 4) 각 run 시작 레코드에서 pso와 mesh/submesh를 꺼내 상태를 설정한다.
            // 5) DrawIndexedInstanced 호출 인자는 다음처럼 잡는다.
            //    - IndexCountPerInstance: run 시작 레코드의 mesh/submesh에서 얻은 index count
            //    - InstanceCount: run 길이
            //    - StartIndexLocation/BaseVertexLocation: run 시작 레코드의 mesh/submesh에서 얻은 값
            //    - StartInstanceLocation: 0
            // 6) run 시작 인덱스는 루트 셰이더 상수(예: DrawRecordBaseIndex)로 셰이더에 전달한다.
            // 7) 셰이더에서는 drawIndex = DrawRecordBaseIndex + SV_InstanceID로 drawRecordsGpu를 조회한다.
            // 8) 조회한 record.objectIndex로 modelContexts를, record.materialIndex로 머티리얼 테이블을 인덱싱한다.
            // std::vector<DirectX::XMFLOAT4X4> bonePalette;
        };
    }
}
