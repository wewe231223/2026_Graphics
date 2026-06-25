#include "Game/Environment/EnvironmentRuntime.h"

#include <utility>

namespace Game {
    EnvironmentPhysicsAdapter::EnvironmentPhysicsAdapter() {
    }

    EnvironmentPhysicsAdapter::~EnvironmentPhysicsAdapter() {
    }

    EnvironmentRuntime::EnvironmentRuntime()
        : mDevice{},
        mAllocator{},
        mSrvHeap{},
        mCopyQueue{},
        mComputeQueue{},
        mPhysicsAdapter{},
        mRenderContext{},
        mLastGpuDispatchFuture{},
        mConfigPath{},
        mInitialized{},
        mGpuDrivenEnabled{} {
    }

    EnvironmentRuntime::~EnvironmentRuntime() {
    }

    EnvironmentRuntime::EnvironmentRuntime(EnvironmentRuntime&& Other) noexcept
        : mDevice{ Other.mDevice },
        mAllocator{ Other.mAllocator },
        mSrvHeap{ Other.mSrvHeap },
        mCopyQueue{ Other.mCopyQueue },
        mComputeQueue{ Other.mComputeQueue },
        mPhysicsAdapter{ Other.mPhysicsAdapter },
        mRenderContext{ std::move(Other.mRenderContext) },
        mLastGpuDispatchFuture{ std::move(Other.mLastGpuDispatchFuture) },
        mConfigPath{ std::move(Other.mConfigPath) },
        mInitialized{ Other.mInitialized },
        mGpuDrivenEnabled{ Other.mGpuDrivenEnabled } {
        Other.mDevice = nullptr;
        Other.mAllocator = nullptr;
        Other.mSrvHeap = nullptr;
        Other.mCopyQueue = nullptr;
        Other.mComputeQueue = nullptr;
        Other.mPhysicsAdapter = nullptr;
        Other.mInitialized = false;
        Other.mGpuDrivenEnabled = false;
    }

    EnvironmentRuntime& EnvironmentRuntime::operator=(EnvironmentRuntime&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mDevice = Other.mDevice;
        mAllocator = Other.mAllocator;
        mSrvHeap = Other.mSrvHeap;
        mCopyQueue = Other.mCopyQueue;
        mComputeQueue = Other.mComputeQueue;
        mPhysicsAdapter = Other.mPhysicsAdapter;
        mRenderContext = std::move(Other.mRenderContext);
        mLastGpuDispatchFuture = std::move(Other.mLastGpuDispatchFuture);
        mConfigPath = std::move(Other.mConfigPath);
        mInitialized = Other.mInitialized;
        mGpuDrivenEnabled = Other.mGpuDrivenEnabled;
        Other.mDevice = nullptr;
        Other.mAllocator = nullptr;
        Other.mSrvHeap = nullptr;
        Other.mCopyQueue = nullptr;
        Other.mComputeQueue = nullptr;
        Other.mPhysicsAdapter = nullptr;
        Other.mInitialized = false;
        Other.mGpuDrivenEnabled = false;
        return *this;
    }

    bool EnvironmentRuntime::Initialize(const EnvironmentRuntimeDesc& Desc) {
        mDevice = Desc.mDevice;
        mAllocator = Desc.mAllocator;
        mSrvHeap = Desc.mSrvHeap;
        mCopyQueue = Desc.mCopyQueue;
        mComputeQueue = Desc.mComputeQueue;
        mPhysicsAdapter = Desc.mPhysicsAdapter;
        mGpuDrivenEnabled = Desc.mGpuDrivenEnabled == true && Desc.mComputeQueue != nullptr;
        mInitialized = Desc.mDevice != nullptr && Desc.mAllocator != nullptr && Desc.mSrvHeap != nullptr && Desc.mCopyQueue != nullptr;
        return mInitialized;
    }

    void EnvironmentRuntime::Reset() {
        mDevice = nullptr;
        mAllocator = nullptr;
        mSrvHeap = nullptr;
        mCopyQueue = nullptr;
        mComputeQueue = nullptr;
        mPhysicsAdapter = nullptr;
        mRenderContext.Clear();
        mLastGpuDispatchFuture = RenderContract::Future{};
        mInitialized = false;
        mGpuDrivenEnabled = false;
    }

    void EnvironmentRuntime::SetConfigPath(const std::string& ConfigPath) {
        mConfigPath = ConfigPath;
    }

    const std::string& EnvironmentRuntime::GetConfigPath() const {
        return mConfigPath;
    }

    void EnvironmentRuntime::TickCpu(const EnvironmentFrameInput& Input) {
        static_cast<void>(Input);
    }

    RenderContract::Future EnvironmentRuntime::DispatchGpu(const EnvironmentFrameInput& Input) {
        static_cast<void>(Input);

        if (mInitialized == false || mGpuDrivenEnabled == false || mComputeQueue == nullptr) {
            mLastGpuDispatchFuture = RenderContract::Future{};
            return mLastGpuDispatchFuture;
        }

        mLastGpuDispatchFuture = RenderContract::Future{};
        return mLastGpuDispatchFuture;
    }

    void EnvironmentRuntime::Draw(ID3D12GraphicsCommandList* CommandList) {
        static_cast<void>(CommandList);
    }

    EnvironmentObjectRenderContext& EnvironmentRuntime::GetRenderContext() {
        return mRenderContext;
    }

    const EnvironmentObjectRenderContext& EnvironmentRuntime::GetRenderContext() const {
        return mRenderContext;
    }

    bool EnvironmentRuntime::IsInitialized() const {
        return mInitialized;
    }

    bool EnvironmentRuntime::IsGpuDrivenEnabled() const {
        return mGpuDrivenEnabled;
    }
}
