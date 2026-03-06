#include "UfbxAssetLoader.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>

using namespace asset;

namespace {
    std::string ToString(const ufbx_string& String) {
        if (String.data == nullptr) {
            return std::string{};
        }
        return std::string{ String.data, String.length };
    }
}

UfbxAssetLoader::SceneHandle::SceneHandle()
    : mScene{ nullptr } {
}

UfbxAssetLoader::SceneHandle::SceneHandle(ufbx_scene* Scene)
    : mScene{ Scene } {
}

UfbxAssetLoader::SceneHandle::SceneHandle(SceneHandle&& Other) noexcept
    : mScene{ Other.mScene } {
    Other.mScene = nullptr;
}

UfbxAssetLoader::SceneHandle& UfbxAssetLoader::SceneHandle::operator=(SceneHandle&& Other) noexcept {
    if (this != &Other) {
        Reset();
        mScene = Other.mScene;
        Other.mScene = nullptr;
    }
    return *this;
}

UfbxAssetLoader::SceneHandle::~SceneHandle() {
    Reset();
}

ufbx_scene* UfbxAssetLoader::SceneHandle::GetScene() const {
    return mScene;
}

void UfbxAssetLoader::SceneHandle::Reset() {
    if (mScene != nullptr) {
        ufbx_free_scene(mScene);
        mScene = nullptr;
    }
}

UfbxAssetLoader::UfbxAssetLoader(GraphicsAPI Api)
    : mApi{ Api } {
}

Mat4 UfbxAssetLoader::ToMat4(const ufbx_matrix& Matrix) {
    Mat4 Out{ 1.0f };
    Out.mValue[0][0] = static_cast<float>(Matrix.cols[0].x);
    Out.mValue[1][0] = static_cast<float>(Matrix.cols[0].y);
    Out.mValue[2][0] = static_cast<float>(Matrix.cols[0].z);
    Out.mValue[3][0] = 0.0f;
    Out.mValue[0][1] = static_cast<float>(Matrix.cols[1].x);
    Out.mValue[1][1] = static_cast<float>(Matrix.cols[1].y);
    Out.mValue[2][1] = static_cast<float>(Matrix.cols[1].z);
    Out.mValue[3][1] = 0.0f;
    Out.mValue[0][2] = static_cast<float>(Matrix.cols[2].x);
    Out.mValue[1][2] = static_cast<float>(Matrix.cols[2].y);
    Out.mValue[2][2] = static_cast<float>(Matrix.cols[2].z);
    Out.mValue[3][2] = 0.0f;
    Out.mValue[0][3] = static_cast<float>(Matrix.cols[3].x);
    Out.mValue[1][3] = static_cast<float>(Matrix.cols[3].y);
    Out.mValue[2][3] = static_cast<float>(Matrix.cols[3].z);
    Out.mValue[3][3] = 1.0f;
    return Out;
}


void UfbxAssetLoader::ExportEmbeddedImages(std::string_view FilePath) {
    const std::filesystem::path SourcePath{ std::string{ FilePath } };
    const std::filesystem::path ImagesDirectoryPath{ SourcePath.parent_path().parent_path() / "Bin" / "Images" };
    std::error_code ErrorCode{};
    std::uint32_t ExportedImageCount{ 0 };
    std::unordered_set<std::uintptr_t> ExportedContentAddresses{};

    const auto WriteEmbeddedImage = [&ImagesDirectoryPath, &ErrorCode, &ExportedImageCount, &ExportedContentAddresses](const void* ContentData, std::size_t ContentSize, const std::string& SourceName, std::size_t Index) {
        const std::uintptr_t ContentAddress{ reinterpret_cast<std::uintptr_t>(ContentData) };
        if (ContentAddress != 0 && ExportedContentAddresses.find(ContentAddress) != ExportedContentAddresses.end()) {
            return;
        }

        if (ContentData == nullptr || ContentSize == 0) {
            return;
        }

        if (ExportedImageCount == 0) {
            std::filesystem::create_directories(ImagesDirectoryPath, ErrorCode);
            if (ErrorCode) {
                throw AssetError{ std::string{ "Failed to create image directory: " } + ImagesDirectoryPath.string() };
            }
        }

        std::filesystem::path FileNamePath{ std::filesystem::path{ SourceName }.filename() };
        if (!FileNamePath.has_filename() || FileNamePath.filename().string().empty()) {
            FileNamePath = std::filesystem::path{ std::string{ "EmbeddedImage_" } + std::to_string(Index) };
        }

        std::filesystem::path OutputPath{ ImagesDirectoryPath / FileNamePath };
        std::uint32_t DuplicateIndex{ 0 };

        while (std::filesystem::exists(OutputPath, ErrorCode)) {
            if (ErrorCode) {
                throw AssetError{ std::string{ "Failed to inspect embedded image output file: " } + OutputPath.string() };
            }

            ++DuplicateIndex;
            const std::string Stem{ FileNamePath.stem().string() };
            const std::string Extension{ FileNamePath.extension().string() };
            OutputPath = ImagesDirectoryPath / std::filesystem::path{ Stem + "_" + std::to_string(DuplicateIndex) + Extension };
        }

        std::ofstream OutputFile{ OutputPath, std::ios::binary };
        if (!OutputFile.is_open()) {
            throw AssetError{ std::string{ "Failed to open embedded image output file: " } + OutputPath.string() };
        }

        OutputFile.write(static_cast<const char*>(ContentData), static_cast<std::streamsize>(ContentSize));
        if (!OutputFile.good()) {
            throw AssetError{ std::string{ "Failed to write embedded image output file: " } + OutputPath.string() };
        }

        ++ExportedImageCount;
        if (ContentAddress != 0) {
            ExportedContentAddresses.insert(ContentAddress);
        }
    };

    auto& scene = *mScene.GetScene(); 

    for (std::size_t Index{ 0 }; Index < scene.texture_files.count; ++Index) {
        const ufbx_texture_file& TextureFile{ scene.texture_files.data[Index] };
        WriteEmbeddedImage(TextureFile.content.data, TextureFile.content.size, ToString(TextureFile.filename), Index);
    }

    for (std::size_t Index{ 0 }; Index < scene.textures.count; ++Index) {
        const ufbx_texture* Texture{ scene.textures.data[Index] };
        if (Texture == nullptr) {
            continue;
        }

        WriteEmbeddedImage(Texture->content.data, Texture->content.size, ToString(Texture->filename), scene.texture_files.count + Index);
    }
}

void asset::UfbxAssetLoader::LoadScene(std::string_view FilePath){
    ufbx_load_opts Opts{};
    if (mApi == GraphicsAPI::DirectX) {
        Opts.target_axes = ufbx_axes_left_handed_y_up;
    }
    else {
        Opts.target_axes = ufbx_axes_right_handed_y_up;
    }
    Opts.target_unit_meters = 1.0;
    ufbx_error Error{};
    ufbx_scene* Scene{ ufbx_load_file(std::string{ FilePath }.c_str(), &Opts, &Error) };
    if (Scene == nullptr) {
        std::string Description{ ToString(Error.description) };
        if (!Description.empty()) {
            throw AssetError{ std::string{ "ufbx_load_file failed: " } + Description };
        }
        throw AssetError{ "ufbx_load_file failed: unknown error" };
    }

	mScene = SceneHandle{ Scene };
	ExportEmbeddedImages(FilePath);
}

void UfbxAssetLoader::LoadAndTraverse(std::string_view FilePath, std::span<ISceneNodeVisitor* const> Visitors) {
    if (mScene.GetScene() == nullptr) {
        LoadScene(FilePath);
    }

    ufbx_scene* const Scene{ mScene.GetScene() };
    if (Scene == nullptr) {
        throw AssetError{ "Failed to load scene" };
    }

    if (Scene->root_node != nullptr) {
        TraverseNode(*Scene, *Scene->root_node, nullptr, Visitors);
        return;
    }

    for (std::size_t Index{ 0 }; Index < Scene->nodes.count; ++Index) {
        const ufbx_node* Node{ Scene->nodes.data[Index] };
        if (Node != nullptr) {
            TraverseNode(*Scene, *Node, Node->parent, Visitors);
        }
    }
}

void asset::UfbxAssetLoader::ExportImages(std::string_view FilePath) {

}

void UfbxAssetLoader::TraverseNode(const ufbx_scene& Scene, const ufbx_node& Node, const ufbx_node* Parent, std::span<ISceneNodeVisitor* const> Visitors) {
    NodeVisitContext Context{};
    Context.mParent = Parent;
    Context.mNodeToParent = ToMat4(Node.node_to_parent);
    if (Node.has_geometry_transform) {
        Context.mGeometryToNode = ToMat4(Node.geometry_to_node);
    }
    else {
        Context.mGeometryToNode = Mat4{ 1.0f };
    }
    for (ISceneNodeVisitor* Visitor : Visitors) {
        if (Visitor != nullptr) {
            Visitor->OnNodeBegin(Scene, Node, Context);
        }
    }
    for (std::size_t Index{ 0 }; Index < Node.children.count; ++Index) {
        const ufbx_node* Child{ Node.children.data[Index] };
        if (Child != nullptr) {
            TraverseNode(Scene, *Child, &Node, Visitors);
        }
    }
    for (ISceneNodeVisitor* Visitor : Visitors) {
        if (Visitor != nullptr) {
            Visitor->OnNodeEnd(Scene, Node);
        }
    }
}
