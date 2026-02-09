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
    Bundle.GetMaterials() = MaterialCollector.GetMaterials();
    return Bundle;
}
