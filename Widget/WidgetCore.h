#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
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
        void SetShadowMapTexture(ID3D12Resource* Resource, std::uint32_t ShadowMapSize);
#pragma endregion
        void SetSceneWorldSnapshot(const Game::SceneWorldSnapshot* Snapshot);

        template<std::derived_from<IWidget> T, typename... Args>
        void MakeWidget(Args&&... args) {
            mWidgets.emplace_back(std::make_unique<T>(std::forward<Args>(args)...));
        }

    private:
        void BuildWidgets();

    private:
        ComPtr<ID3D12DescriptorHeap> mSRVHeap{ nullptr };
        std::vector<std::unique_ptr<IWidget>> mWidgets{};
        const Game::SceneWorldSnapshot* mSceneWorldSnapshot{};
#pragma region TemporaryShadowMapPreview
        ID3D12Device* mDevice{ nullptr };
        ID3D12Resource* mShadowMapResource{ nullptr };
        std::uint32_t mDescriptorIncrementSize{};
        std::uint32_t mShadowMapSize{};
        D3D12_CPU_DESCRIPTOR_HANDLE mShadowMapSrvCpuHandle{ 0 };
        D3D12_GPU_DESCRIPTOR_HANDLE mShadowMapSrvGpuHandle{ 0 };
#pragma endregion
        bool mIsInitialized{ false };
    };
}
