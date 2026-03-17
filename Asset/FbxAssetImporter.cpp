#include "FbxAssetImporter.h"

#include "MaterialVisitor.h"
#include "MeshHierarchyBuilder.h"
#include "SkeletonVisitor.h"
#include "UfbxAssetLoader.h"

using namespace asset;

FbxAssetImporter::FbxAssetImporter(GraphicsAPI Api)
    : mApi{ Api } {
}

void FbxAssetImporter::LoadFromFile(std::string_view FilePath, ModelResult& OutModelData, std::vector<MaterialGroup>& OutMaterialGroups, bool IsUvFlipEnabled) {
    UfbxAssetLoader Loader{ mApi };
    MaterialVisitor MaterialCollector{};
    SkeletonVisitor SkeletonCollector{ OutModelData };
    OutModelData = ModelResult{};
    OutMaterialGroups.clear();
    ISceneNodeVisitor* FirstPassVisitors[]{ &MaterialCollector, &SkeletonCollector };
    Loader.LoadAndTraverse(FilePath, { FirstPassVisitors });

    const ufbx_scene* LoadedScene{ Loader.GetLoadedScene() };
    if (LoadedScene != nullptr) {
        SkeletonCollector.Finalize(*LoadedScene);
    }

    MeshHierarchyBuilder Builder{ OutModelData, &MaterialCollector.GetMaterialLookup(), IsUvFlipEnabled };
    ISceneNodeVisitor* SecondPassVisitors[]{ &Builder };
    Loader.LoadAndTraverse(FilePath, { SecondPassVisitors });
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
