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

            float    dt = 0.0f;
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
        // 3) 드로우 인스턴스 레코드 (submesh 드로우를 위한 최소 단위)
        //    - 이 배열도 SRV로 올려서 셰이더가 인덱싱
        //    - materialIndex는 셰이더에서 재질 데이터(상수/텍스처 인덱스)에 접근하기 위한 인덱스
        // ------------------------------------------------------------
        struct DrawInstance {
            uint32_t objectIndex{ 0 };          // ModelContext 인덱스
            uint32_t materialIndex{ 0 };        // Material 데이터 배열(또는 bindless 테이블) 인덱스
            uint32_t flags{ 0 };                // (선택) alpha-test/transparent 등
            uint32_t pad0{ 0 };
        };

        // ------------------------------------------------------------
        // 4) 배치 = DrawIndexedInstanced 1회 호출 단위 (CPU가 소비)
        //    - 데이터를 갖는다: PSO*, Mesh*, submeshIndex
        //    - instanceOffset/count는 DrawInstance 배열을 참조
        // ------------------------------------------------------------
        struct DrawBatch {
            const Interface::IPipeline* pso{ nullptr };
            const Interface::IModelNode* mesh{ nullptr };
            uint32_t    submesh{ 0 };           // mesh->submeshes[submesh]로 indexStart/indexCount 획득
            uint32_t    pass{ 0 };              // optional (Opaque/Shadow/etc)

            uint32_t    instanceOffset{ 0 };    // DrawInstance 시작 인덱스
            uint32_t    instanceCount{ 0 };
        };

        // ------------------------------------------------------------
        // 5) Scene -> Renderer 프레임 패킷
        //    - Renderer는 batches로 커맨드 생성
        //    - modelContexts/drawInstances는 GPU에 업로드 후 셰이더가 인덱싱
        // ------------------------------------------------------------
        struct RenderFrameData {
            FrameGlobals globals{};

            std::vector<ModelContext> modelContexts{};  // SRV
            std::vector<DrawInstance> drawInstances{};  // SRV
            std::vector<DrawBatch>    batches{};        // CPU 

            // std::vector<DirectX::XMFLOAT4X4> bonePalette;
        };
    }
}
