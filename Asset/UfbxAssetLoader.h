#pragma once

#include "Common.h"
#include "SceneVisitor.h"

namespace asset {
    class UfbxAssetLoader final {
    private:
        struct SceneHandle final {
        public:
            SceneHandle();
            explicit SceneHandle(ufbx_scene* Scene);

            SceneHandle(const SceneHandle& Other) = delete;
            SceneHandle& operator=(const SceneHandle& Other) = delete;
            SceneHandle(SceneHandle&& Other) noexcept;
            SceneHandle& operator=(SceneHandle&& Other) noexcept;

            ~SceneHandle();

        public:
            ufbx_scene* GetScene() const;
            void Reset();

        private:
            ufbx_scene* mScene{ nullptr };
        };

    public:
        explicit UfbxAssetLoader(GraphicsAPI Api);
        ~UfbxAssetLoader() = default;

        UfbxAssetLoader(const UfbxAssetLoader& Other) = delete;
        UfbxAssetLoader& operator=(const UfbxAssetLoader& Other) = delete;
        UfbxAssetLoader(UfbxAssetLoader&& Other) noexcept = default;
        UfbxAssetLoader& operator=(UfbxAssetLoader&& Other) noexcept = default;

    public:
        void LoadAndTraverse(std::string_view FilePath, std::span<ISceneNodeVisitor* const> Visitors);

    private:
        static Mat4 ToMat4(const ufbx_matrix& Matrix);
        void TraverseNode(const ufbx_scene& Scene, const ufbx_node& Node, const ufbx_node* Parent, std::span<ISceneNodeVisitor* const> Visitors);

    private:
        GraphicsAPI mApi{ GraphicsAPI::DirectX };
    };
}
