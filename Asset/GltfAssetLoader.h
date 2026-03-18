#pragma once

#include "Common.h"
#include "GltfSceneVisitor.h"
#include "cgltf.h"

namespace asset {
    class GltfAssetLoader final {
    public:
        struct SceneHandle final {
        public:
            SceneHandle();
            explicit SceneHandle(cgltf_data* Scene);
            ~SceneHandle();

            SceneHandle(const SceneHandle& Other) = delete;
            SceneHandle& operator=(const SceneHandle& Other) = delete;
            SceneHandle(SceneHandle&& Other) noexcept;
            SceneHandle& operator=(SceneHandle&& Other) noexcept;

        public:
            cgltf_data* GetScene() const;
            void Reset();

        private:
            cgltf_data* mScene{ nullptr };
        };

    public:
        explicit GltfAssetLoader(GraphicsAPI Api);
        ~GltfAssetLoader() = default;

        GltfAssetLoader(const GltfAssetLoader& Other) = delete;
        GltfAssetLoader& operator=(const GltfAssetLoader& Other) = delete;
        GltfAssetLoader(GltfAssetLoader&& Other) noexcept = default;
        GltfAssetLoader& operator=(GltfAssetLoader&& Other) noexcept = default;

    public:
        void LoadScene(std::string_view FilePath);
        void LoadAndTraverse(std::string_view FilePath, std::span<IGltfSceneNodeVisitor* const> Visitors);
        void ExportImages(std::string_view FilePath);

    private:
        static Mat4 ToMat4(const cgltf_node& Node, GraphicsAPI Api);
        void ExportEmbeddedImages(std::string_view FilePath);
        void TraverseNode(const cgltf_data& Scene, const cgltf_node& Node, const cgltf_node* Parent, std::span<IGltfSceneNodeVisitor* const> Visitors);

    private:
        GraphicsAPI mApi{ GraphicsAPI::DirectX };
        SceneHandle mScene{};
    };
}
