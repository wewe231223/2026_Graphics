#include "GltfAssetImporter.h"

#include "GltfAssetLoader.h"
#include "GltfMaterialVisitor.h"
#include "GltfMeshHierarchyBuilder.h"

namespace asset {
    GltfAssetImporter::GltfAssetImporter(GraphicsAPI Api)
        : mApi{ Api } {
    }

    void GltfAssetImporter::LoadFromFile(std::string_view FilePath, ModelResult& OutModelData, std::vector<MaterialGroup>& OutMaterialGroups, bool IsUvFlipEnabled) {
        GltfAssetLoader Loader{ mApi };
        GltfMaterialVisitor MaterialCollector{};
        OutModelData = ModelResult{};
        OutMaterialGroups.clear();

        GltfMeshHierarchyBuilder Builder{ OutModelData, &MaterialCollector.GetMaterialLookup(), mApi, IsUvFlipEnabled };
        IGltfSceneNodeVisitor* Visitors[]{ &MaterialCollector, &Builder };
        Loader.LoadAndTraverse(FilePath, { Visitors });
        Builder.FinalizeSkinBindings();

        MaterialGroup DefaultMaterialGroup{};
        DefaultMaterialGroup.Name = std::string{ FilePath };

        const std::vector<Material>& Materials{ MaterialCollector.GetMaterials() };
        DefaultMaterialGroup.Items.reserve(Materials.size());
        for (const Material& MaterialData : Materials) {
            MaterialGroupItem MaterialGroupItemData{};
            MaterialGroupItemData.MaterialData = MaterialData;
            MaterialGroupItemData.PipelineName = std::string{};
            DefaultMaterialGroup.Items.push_back(std::move(MaterialGroupItemData));
        }

        if (!DefaultMaterialGroup.Items.empty()) {
            OutMaterialGroups.push_back(std::move(DefaultMaterialGroup));
        }
    }
}
