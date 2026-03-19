#include "GltfAssetLoader.h"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>

namespace asset {
    namespace {
        std::string GetBase64Alphabet() {
            return std::string{ "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/" };
        }

        int DecodeBase64Value(char Value) {
            const std::string Alphabet{ GetBase64Alphabet() };
            const std::size_t Position{ Alphabet.find(Value) };
            if (Position == std::string::npos) {
                return -1;
            }

            return static_cast<int>(Position);
        }

        std::vector<std::uint8_t> DecodeBase64(std::string_view EncodedData) {
            std::vector<std::uint8_t> DecodedData{};
            int Accumulator{ 0 };
            int BitCount{ 0 };

            for (const char Character : EncodedData) {
                if (Character == '=') {
                    break;
                }

                if (std::isspace(static_cast<unsigned char>(Character)) != 0) {
                    continue;
                }

                const int DecodedValue{ DecodeBase64Value(Character) };
                if (DecodedValue < 0) {
                    throw AssetError{ "Failed to decode embedded base64 image" };
                }

                Accumulator = (Accumulator << 6) | DecodedValue;
                BitCount += 6;
                if (BitCount >= 8) {
                    BitCount -= 8;
                    DecodedData.push_back(static_cast<std::uint8_t>((Accumulator >> BitCount) & 0xFF));
                }
            }

            return DecodedData;
        }

        std::string ToString(const char* Value) {
            if (Value == nullptr) {
                return std::string{};
            }

            return std::string{ Value };
        }

        std::string GetImageExtension(const cgltf_image& Image) {
            const std::string MimeType{ ToString(Image.mime_type) };
            if (MimeType == "image/png") {
                return std::string{ ".png" };
            }

            if (MimeType == "image/jpeg") {
                return std::string{ ".jpg" };
            }

            if (MimeType == "image/webp") {
                return std::string{ ".webp" };
            }

            return std::string{};
        }

        std::string GetImageFileName(const cgltf_image& Image, std::size_t Index) {
            std::string FileName{ ToString(Image.uri) };
            if (FileName.empty()) {
                FileName = ToString(Image.name);
            }

            std::filesystem::path FileNamePath{ FileName };
            if (!FileNamePath.has_filename() || FileNamePath.filename().string().empty()) {
                FileNamePath = std::filesystem::path{ std::string{ "EmbeddedImage_" } + std::to_string(Index) + GetImageExtension(Image) };
            }
            else if (!FileNamePath.has_extension()) {
                FileNamePath.replace_extension(GetImageExtension(Image));
            }

            return FileNamePath.filename().string();
        }

        std::vector<std::uint8_t> LoadImageBytes(const cgltf_image& Image) {
            if (Image.buffer_view != nullptr) {
                const cgltf_buffer_view& BufferView{ *Image.buffer_view };
                if (BufferView.buffer == nullptr || BufferView.buffer->data == nullptr) {
                    return std::vector<std::uint8_t>{};
                }

                const std::uint8_t* const Begin{ static_cast<const std::uint8_t*>(BufferView.buffer->data) + BufferView.offset };
                return std::vector<std::uint8_t>{ Begin, Begin + BufferView.size };
            }

            const std::string Uri{ ToString(Image.uri) };
            const std::string Prefix{ "data:" };
            if (Uri.rfind(Prefix, 0) != 0) {
                return std::vector<std::uint8_t>{};
            }

            const std::size_t SeparatorIndex{ Uri.find(",") };
            if (SeparatorIndex == std::string::npos) {
                return std::vector<std::uint8_t>{};
            }

            const std::string Metadata{ Uri.substr(0, SeparatorIndex) };
            const std::string EncodedContent{ Uri.substr(SeparatorIndex + 1) };
            if (Metadata.find(";base64") != std::string::npos) {
                return DecodeBase64(EncodedContent);
            }

            return std::vector<std::uint8_t>{ EncodedContent.begin(), EncodedContent.end() };
        }
    }

    GltfAssetLoader::SceneHandle::SceneHandle()
        : mScene{ nullptr } {
    }

    GltfAssetLoader::SceneHandle::SceneHandle(cgltf_data* Scene)
        : mScene{ Scene } {
    }

    GltfAssetLoader::SceneHandle::~SceneHandle() {
        Reset();
    }

    GltfAssetLoader::SceneHandle::SceneHandle(SceneHandle&& Other) noexcept
        : mScene{ Other.mScene } {
        Other.mScene = nullptr;
    }

    GltfAssetLoader::SceneHandle& GltfAssetLoader::SceneHandle::operator=(SceneHandle&& Other) noexcept {
        if (this != &Other) {
            Reset();
            mScene = Other.mScene;
            Other.mScene = nullptr;
        }

        return *this;
    }

    cgltf_data* GltfAssetLoader::SceneHandle::GetScene() const {
        return mScene;
    }

    void GltfAssetLoader::SceneHandle::Reset() {
        if (mScene != nullptr) {
            cgltf_free(mScene);
            mScene = nullptr;
        }
    }

    GltfAssetLoader::GltfAssetLoader(GraphicsAPI Api)
        : mApi{ Api }
        , mScene{} {
    }

    Mat4 GltfAssetLoader::ToMat4(const cgltf_node& Node, GraphicsAPI Api) {
        cgltf_float MatrixValues[16]{ 0.0f };
        cgltf_node_transform_local(&Node, MatrixValues);

        Mat4 Out{ 1.0f };
        Out.mValue[0][0] = MatrixValues[0];
        Out.mValue[1][0] = MatrixValues[1];
        Out.mValue[2][0] = MatrixValues[2];
        Out.mValue[3][0] = 0.0f;
        Out.mValue[0][1] = MatrixValues[4];
        Out.mValue[1][1] = MatrixValues[5];
        Out.mValue[2][1] = MatrixValues[6];
        Out.mValue[3][1] = 0.0f;
        Out.mValue[0][2] = MatrixValues[8];
        Out.mValue[1][2] = MatrixValues[9];
        Out.mValue[2][2] = MatrixValues[10];
        Out.mValue[3][2] = 0.0f;
        Out.mValue[0][3] = MatrixValues[12];
        Out.mValue[1][3] = MatrixValues[13];
        Out.mValue[2][3] = MatrixValues[14];
        Out.mValue[3][3] = 1.0f;

        //if (Api == GraphicsAPI::DirectX) {
        //    Out.mValue[2][0] = -Out.mValue[2][0];
        //    Out.mValue[2][1] = -Out.mValue[2][1];
        //    Out.mValue[0][2] = -Out.mValue[0][2];
        //    Out.mValue[1][2] = -Out.mValue[1][2];
        //    Out.mValue[2][3] = -Out.mValue[2][3];
        //}

        return Out;
    }

    void GltfAssetLoader::ExportEmbeddedImages(std::string_view FilePath) {
        const std::filesystem::path SourcePath{ std::string{ FilePath } };
        const std::filesystem::path ImagesDirectoryPath{ SourcePath.parent_path().parent_path() / "Bin" / "Images" };
        std::error_code ErrorCode{};
        std::unordered_set<std::string> ExportedNames{};
        cgltf_data* const Scene{ mScene.GetScene() };

        if (Scene == nullptr) {
            return;
        }

        for (cgltf_size ImageIndex{ 0 }; ImageIndex < Scene->images_count; ++ImageIndex) {
            const cgltf_image& Image{ Scene->images[ImageIndex] };
            const std::vector<std::uint8_t> Content{ LoadImageBytes(Image) };
            if (Content.empty()) {
                continue;
            }

            std::filesystem::create_directories(ImagesDirectoryPath, ErrorCode);
            if (ErrorCode) {
                throw AssetError{ std::string{ "Failed to create image directory: " } + ImagesDirectoryPath.string() };
            }

            const std::filesystem::path FileNamePath{ GetImageFileName(Image, static_cast<std::size_t>(ImageIndex)) };
            std::filesystem::path OutputPath{ ImagesDirectoryPath / FileNamePath };
            std::uint32_t DuplicateIndex{ 0U };

            while (ExportedNames.find(OutputPath.filename().string()) != ExportedNames.end() || std::filesystem::exists(OutputPath, ErrorCode)) {
                if (ErrorCode) {
                    throw AssetError{ std::string{ "Failed to inspect embedded image output file: " } + OutputPath.string() };
                }

                ++DuplicateIndex;
                OutputPath = ImagesDirectoryPath / std::filesystem::path{ FileNamePath.stem().string() + "_" + std::to_string(DuplicateIndex) + FileNamePath.extension().string() };
            }

            std::ofstream OutputFile{ OutputPath, std::ios::binary };
            if (!OutputFile.is_open()) {
                throw AssetError{ std::string{ "Failed to open embedded image output file: " } + OutputPath.string() };
            }

            OutputFile.write(reinterpret_cast<const char*>(Content.data()), static_cast<std::streamsize>(Content.size()));
            if (!OutputFile.good()) {
                throw AssetError{ std::string{ "Failed to write embedded image output file: " } + OutputPath.string() };
            }

            ExportedNames.insert(OutputPath.filename().string());
        }
    }

    void GltfAssetLoader::LoadScene(std::string_view FilePath) {
        cgltf_options Options{};
        cgltf_data* Scene{ nullptr };
        const cgltf_result ParseResult{ cgltf_parse_file(&Options, std::string{ FilePath }.c_str(), &Scene) };
        if (ParseResult != cgltf_result_success || Scene == nullptr) {
            throw AssetError{ "cgltf_parse_file failed" };
        }

        const cgltf_result BufferResult{ cgltf_load_buffers(&Options, Scene, std::string{ FilePath }.c_str()) };
        if (BufferResult != cgltf_result_success) {
            cgltf_free(Scene);
            throw AssetError{ "cgltf_load_buffers failed" };
        }

        const cgltf_result ValidateResult{ cgltf_validate(Scene) };
        if (ValidateResult != cgltf_result_success) {
            cgltf_free(Scene);
            throw AssetError{ "cgltf_validate failed" };
        }

        mScene = SceneHandle{ Scene };
        ExportEmbeddedImages(FilePath);
    }

    void GltfAssetLoader::LoadAndTraverse(std::string_view FilePath, std::span<IGltfSceneNodeVisitor* const> Visitors) {
        if (mScene.GetScene() == nullptr) {
            LoadScene(FilePath);
        }

        cgltf_data* const Scene{ mScene.GetScene() };
        if (Scene == nullptr) {
            throw AssetError{ "Failed to load scene" };
        }

        cgltf_scene* RootScene{ Scene->scene != nullptr ? Scene->scene : (Scene->scenes_count > 0 ? &Scene->scenes[0] : nullptr) };
        if (RootScene != nullptr && RootScene->nodes_count > 0) {
            for (cgltf_size NodeIndex{ 0 }; NodeIndex < RootScene->nodes_count; ++NodeIndex) {
                TraverseNode(*Scene, *RootScene->nodes[NodeIndex], nullptr, Visitors);
            }

            return;
        }

        for (cgltf_size NodeIndex{ 0 }; NodeIndex < Scene->nodes_count; ++NodeIndex) {
            const cgltf_node& Node{ Scene->nodes[NodeIndex] };
            if (Node.parent == nullptr) {
                TraverseNode(*Scene, Node, nullptr, Visitors);
            }
        }
    }

    void GltfAssetLoader::ExportImages(std::string_view FilePath) {
        if (mScene.GetScene() == nullptr) {
            LoadScene(FilePath);
        }

        ExportEmbeddedImages(FilePath);
    }

    void GltfAssetLoader::TraverseNode(const cgltf_data& Scene, const cgltf_node& Node, const cgltf_node* Parent, std::span<IGltfSceneNodeVisitor* const> Visitors) {
        GltfNodeVisitContext Context{};
        Context.mParent = Parent;
        Context.mNodeToParent = ToMat4(Node, mApi);
        Context.mGeometryToNode = Mat4{ 1.0f };

        for (IGltfSceneNodeVisitor* const Visitor : Visitors) {
            if (Visitor != nullptr) {
                Visitor->OnNodeBegin(Scene, Node, Context);
            }
        }

        for (cgltf_size ChildIndex{ 0 }; ChildIndex < Node.children_count; ++ChildIndex) {
            TraverseNode(Scene, *Node.children[ChildIndex], &Node, Visitors);
        }

        for (IGltfSceneNodeVisitor* const Visitor : Visitors) {
            if (Visitor != nullptr) {
                Visitor->OnNodeEnd(Scene, Node);
            }
        }
    }
}
