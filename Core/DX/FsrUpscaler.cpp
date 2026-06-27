#include "Core/DX/FsrUpscaler.h"

#include <Windows.h>

#include "FidelityFX/api/include/ffx_api_loader.h"
#include "FidelityFX/api/include/dx12/ffx_api_dx12.h"
#include "FidelityFX/upscalers/include/ffx_upscale.h"
#include "Utility/ErrorHandler.h"

namespace {
    bool IsFunctionTableValid(const ffxFunctions& Functions) {
        return Functions.CreateContext != nullptr && Functions.DestroyContext != nullptr && Functions.Dispatch != nullptr && Functions.Query != nullptr && Functions.Configure != nullptr;
    }
}

namespace Core {
    namespace DX {
        FsrUpscaler::FsrUpscaler()
            : mModule{},
            mDevice{},
            mContext{},
            mParameter{},
            mResolution{},
            mFunctionsLoaded{} {
        }

        FsrUpscaler::~FsrUpscaler() {
            Shutdown();
        }

        bool FsrUpscaler::Initialize(ID3D12Device* Device, const FsrParameter& Parameter, const FsrResolution& Resolution) {
            Shutdown();
            mDevice = Device;
            mParameter = Parameter;
            mResolution = Resolution;

            if (Device == nullptr || Parameter.mEnabled == false) {
                return Parameter.mEnabled == false;
            }

            if (LoadFunctions() == false) {
                return false;
            }

            ffxCreateContextDescUpscale UpscaleDescription{};
            UpscaleDescription.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            UpscaleDescription.flags = BuildCreateFlags(Parameter);
            UpscaleDescription.maxRenderSize = FfxApiDimensions2D{ Resolution.mRenderWidth, Resolution.mRenderHeight };
            UpscaleDescription.maxUpscaleSize = FfxApiDimensions2D{ Resolution.mDisplayWidth, Resolution.mDisplayHeight };
            UpscaleDescription.fpMessage = nullptr;

            ffxCreateBackendDX12Desc BackendDescription{};
            BackendDescription.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
            BackendDescription.device = Device;

            ffxCreateContextDescUpscaleVersion VersionDescription{};
            VersionDescription.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
            VersionDescription.version = FFX_UPSCALER_VERSION;

            UpscaleDescription.header.pNext = &BackendDescription.header;
            BackendDescription.header.pNext = &VersionDescription.header;
            VersionDescription.header.pNext = nullptr;

            ffxReturnCode_t ReturnCode{ FFX_API_RETURN_ERROR };
            ffxFunctions FunctionTable{};
            ffxLoadFunctions(&FunctionTable, mModule);
            ReturnCode = FunctionTable.CreateContext(&mContext, &UpscaleDescription.header, nullptr);
            if (ReturnCode != FFX_API_RETURN_OK) {
                ErrorHandler::report("FsrUpscaler", "Failed to create FSR upscaler context.", ErrorHandler::Level::Warning);
                DestroyContext();
                return false;
            }

            return true;
        }

        void FsrUpscaler::Shutdown() {
            DestroyContext();

            if (mModule != nullptr) {
                FreeLibrary(static_cast<HMODULE>(mModule));
                mModule = nullptr;
            }

            mFunctionsLoaded = false;
            mDevice = nullptr;
            mParameter = FsrParameter{};
            mResolution = FsrResolution{};
        }

        bool FsrUpscaler::Dispatch(const FsrDispatchInput& Input) {
            if (IsContextValid() == false || Input.mCommandList == nullptr || Input.mColor == nullptr || Input.mDepth == nullptr || Input.mMotionVectors == nullptr || Input.mOutput == nullptr) {
                return false;
            }

            ffxDispatchDescUpscale DispatchDescription{};
            DispatchDescription.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
            DispatchDescription.commandList = Input.mCommandList;
            DispatchDescription.color = ffxApiGetResourceDX12(Input.mColor, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            DispatchDescription.depth = ffxApiGetResourceDX12(Input.mDepth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            DispatchDescription.motionVectors = ffxApiGetResourceDX12(Input.mMotionVectors, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            DispatchDescription.exposure = FfxApiResource{};
            DispatchDescription.reactive = FfxApiResource{};
            DispatchDescription.transparencyAndComposition = FfxApiResource{};
            DispatchDescription.output = ffxApiGetResourceDX12(Input.mOutput, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, FFX_API_RESOURCE_USAGE_UAV);
            DispatchDescription.jitterOffset = FfxApiFloatCoords2D{ Input.mJitterOffset.x, Input.mJitterOffset.y };
            DispatchDescription.motionVectorScale = FfxApiFloatCoords2D{ Input.mMotionVectorScale.x, Input.mMotionVectorScale.y };
            DispatchDescription.renderSize = FfxApiDimensions2D{ Input.mResolution.mRenderWidth, Input.mResolution.mRenderHeight };
            DispatchDescription.upscaleSize = FfxApiDimensions2D{ Input.mResolution.mDisplayWidth, Input.mResolution.mDisplayHeight };
            DispatchDescription.enableSharpening = mParameter.mSharpeningEnabled;
            DispatchDescription.sharpness = mParameter.mSharpness;
            DispatchDescription.frameTimeDelta = Input.mFrameTimeDeltaMs;
            DispatchDescription.preExposure = 1.0f;
            DispatchDescription.reset = Input.mResetHistory;
            DispatchDescription.cameraNear = Input.mCameraNear;
            DispatchDescription.cameraFar = Input.mCameraFar;
            DispatchDescription.cameraFovAngleVertical = Input.mCameraFovAngleVertical;
            DispatchDescription.viewSpaceToMetersFactor = 1.0f;
            DispatchDescription.flags = 0u;

            ffxFunctions FunctionTable{};
            ffxLoadFunctions(&FunctionTable, mModule);
            const ffxReturnCode_t ReturnCode{ FunctionTable.Dispatch(&mContext, &DispatchDescription.header) };
            if (ReturnCode != FFX_API_RETURN_OK) {
                ErrorHandler::report("FsrUpscaler", "Failed to dispatch FSR upscaler.", ErrorHandler::Level::Warning);
                return false;
            }

            return true;
        }

        bool FsrUpscaler::IsContextValid() const {
            return mContext != nullptr && mFunctionsLoaded == true;
        }

        const FsrResolution& FsrUpscaler::GetResolution() const {
            return mResolution;
        }

        bool FsrUpscaler::LoadFunctions() {
            if (mFunctionsLoaded == true) {
                return true;
            }

            mModule = LoadLibraryW(L"amd_fidelityfx_loader_dx12.dll");
            if (mModule == nullptr) {
                ErrorHandler::report("FsrUpscaler", "Failed to load amd_fidelityfx_loader_dx12.dll.", ErrorHandler::Level::Warning);
                return false;
            }

            ffxFunctions FunctionTable{};
            ffxLoadFunctions(&FunctionTable, mModule);
            if (IsFunctionTableValid(FunctionTable) == false) {
                ErrorHandler::report("FsrUpscaler", "Failed to resolve FidelityFX API functions.", ErrorHandler::Level::Warning);
                FreeLibrary(static_cast<HMODULE>(mModule));
                mModule = nullptr;
                return false;
            }

            mFunctionsLoaded = true;
            return true;
        }

        void FsrUpscaler::DestroyContext() {
            if (mContext == nullptr || mModule == nullptr) {
                mContext = nullptr;
                return;
            }

            ffxFunctions FunctionTable{};
            ffxLoadFunctions(&FunctionTable, mModule);
            if (FunctionTable.DestroyContext != nullptr) {
                FunctionTable.DestroyContext(&mContext, nullptr);
            }

            mContext = nullptr;
        }

        std::uint32_t FsrUpscaler::BuildCreateFlags(const FsrParameter& Parameter) const {
            std::uint32_t Flags{};
            Flags |= Parameter.mHighDynamicRange == true ? FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE : 0u;
            Flags |= Parameter.mMotionVectorsJitterCancellation == true ? FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION : 0u;
            Flags |= Parameter.mDepthInverted == true ? FFX_UPSCALE_ENABLE_DEPTH_INVERTED : 0u;
            Flags |= Parameter.mDepthInfinite == true ? FFX_UPSCALE_ENABLE_DEPTH_INFINITE : 0u;
            Flags |= Parameter.mAutoExposureEnabled == true ? FFX_UPSCALE_ENABLE_AUTO_EXPOSURE : 0u;
            Flags |= Parameter.mDebugChecking == true ? FFX_UPSCALE_ENABLE_DEBUG_CHECKING : 0u;
            Flags |= Parameter.mDebugVisualization == true ? FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION : 0u;
            return Flags;
        }
    }
}
