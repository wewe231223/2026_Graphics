#pragma once

#include <cstdint>

#include "Core/DX/FsrParameter.h"
#include "Utility/DirectXInclude.h"

namespace Core {
    namespace DX {
        struct FsrResolution final {
        public:
            std::uint32_t mRenderWidth{};
            std::uint32_t mRenderHeight{};
            std::uint32_t mDisplayWidth{};
            std::uint32_t mDisplayHeight{};
        };

        struct FsrDispatchInput final {
        public:
            ID3D12GraphicsCommandList* mCommandList{};
            ID3D12Resource* mColor{};
            ID3D12Resource* mDepth{};
            ID3D12Resource* mMotionVectors{};
            ID3D12Resource* mOutput{};
            FsrResolution mResolution{};
            DirectX::SimpleMath::Vector2 mJitterOffset{};
            DirectX::SimpleMath::Vector2 mMotionVectorScale{ 1.0f, 1.0f };
            float mFrameTimeDeltaMs{};
            float mCameraNear{};
            float mCameraFar{};
            float mCameraFovAngleVertical{};
            bool mResetHistory{};
        };

        class FsrUpscaler final {
        public:
            FsrUpscaler();
            ~FsrUpscaler();

            FsrUpscaler(const FsrUpscaler& Other) = delete;
            FsrUpscaler& operator=(const FsrUpscaler& Other) = delete;

            FsrUpscaler(FsrUpscaler&& Other) = delete;
            FsrUpscaler& operator=(FsrUpscaler&& Other) = delete;

        public:
            bool Initialize(ID3D12Device* Device, const FsrParameter& Parameter, const FsrResolution& Resolution);
            void Shutdown();
            bool Dispatch(const FsrDispatchInput& Input);
            bool IsContextValid() const;
            const FsrResolution& GetResolution() const;

        private:
            bool LoadFunctions();
            void DestroyContext();
            std::uint32_t BuildCreateFlags(const FsrParameter& Parameter) const;

        private:
            void* mModule{};
            ID3D12Device* mDevice{};
            void* mContext{};
            FsrParameter mParameter{};
            FsrResolution mResolution{};
            bool mFunctionsLoaded{};
        };
    }
}
