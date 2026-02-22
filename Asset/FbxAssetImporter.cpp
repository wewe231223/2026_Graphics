#include "FbxAssetImporter.h"

#include "MaterialVisitor.h"
#include "MeshHierarchyBuilder.h"
#include "UfbxAssetLoader.h"

using namespace asset;

FbxAssetImporter::FbxAssetImporter(GraphicsAPI Api)
    : mApi{ Api } {
}

AssetBundle FbxAssetImporter::LoadFromFile(std::string_view FilePath) {
    UfbxAssetLoader Loader{ mApi };
    MaterialVisitor MaterialCollector{};
    AssetBundle Bundle{};
    MeshHierarchyBuilder Builder{ Bundle.GetModelResult(), &MaterialCollector.GetMaterialLookup() };
    ISceneNodeVisitor* Visitors[]{ &MaterialCollector, &Builder };
    Loader.LoadAndTraverse(FilePath, { Visitors });
    MaterialGroup DefaultMaterialGroup{};
    DefaultMaterialGroup.Name = "Default";

    const std::vector<Material>& Materials{ MaterialCollector.GetMaterials() };
    DefaultMaterialGroup.Items.reserve(Materials.size());
    for (const Material& MaterialData : Materials) {
        MaterialGroupItem MaterialGroupItemData{};
        MaterialGroupItemData.MaterialData = MaterialData;
        MaterialGroupItemData.PipelineName = std::string{};
        DefaultMaterialGroup.Items.push_back(std::move(MaterialGroupItemData));
    }

    if (!DefaultMaterialGroup.Items.empty()) {
        Bundle.GetMaterialGroups().push_back(std::move(DefaultMaterialGroup));
    }
    return Bundle;
}
