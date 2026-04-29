#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <array>
#include <cstdint>
#include <vector>
#include <memory>
#include "Utility/DirectXInclude.h"
#include "Common.h"

namespace Game {
    class SceneWorldSnapshot;
}

namespace Widget {
    class WidgetCore {
    public:
        static constexpr std::uint32_t ShadowMapPreviewCapacity{ 4 };

        WidgetCore() = default;
        ~WidgetCore();

        WidgetCore(const WidgetCore&) = delete;
        WidgetCore& operator=(const WidgetCore&) = delete;

        WidgetCore(WidgetCore&&) noexcept = default;
        WidgetCore& operator=(WidgetCore&&) noexcept = default;

    public:
        void Initialize(HWND hWnd, ID3D12Device* Device, IDXGIAdapter1* Adapter);
        void Render(ComPtr<ID3D12GraphicsCommandList>& commandList);
#pragma region TemporaryShadowMapPreview
        void SetShadowMapTextures(const std::array<ID3D12Resource*, ShadowMapPreviewCapacity>& Resources, std::uint32_t ShadowMapCount, std::uint32_t ShadowMapSize);
#pragma endregion
        void SetSceneWorldSnapshot(const Game::SceneWorldSnapshot* Snapshot);
        bool IsPostProcessEnabled() const;

        template<std::derived_from<IWidget> T, typename... Args>
        void MakeWidget(Args&&... args) {
            mWidgets.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
        }

    private:
        void BuildWidgets();
        void RenderRenderSettings();

    private:
        ComPtr<ID3D12DescriptorHeap> mSRVHeap{ nullptr };
        std::vector<std::unique_ptr<IWidget>> mWidgets{};
        const Game::SceneWorldSnapshot* mSceneWorldSnapshot{};
#pragma region TemporaryShadowMapPreview
        ID3D12Device* mDevice{ nullptr };
        std::uint32_t mDescriptorIncrementSize{};
        std::uint32_t mShadowMapCount{};
        std::uint32_t mShadowMapSize{};
        std::array<ID3D12Resource*, ShadowMapPreviewCapacity> mShadowMapResources{};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, ShadowMapPreviewCapacity> mShadowMapSrvCpuHandles{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, ShadowMapPreviewCapacity> mShadowMapSrvGpuHandles{};
#pragma endregion
        bool mIsPostProcessEnabled{ true };
        bool mIsInitialized{ false };
    };
}
