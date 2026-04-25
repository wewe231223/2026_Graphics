#include "MaterialGroupJsonSerializer.h"

#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

namespace asset {
    namespace {
        constexpr std::size_t LocalMaterialTypeCount{ static_cast<std::size_t>(asset::MaterialTypeCount) };

        std::array<std::string, LocalMaterialTypeCount> BuildMaterialTypeNames() {
            std::array<std::string, LocalMaterialTypeCount> MaterialTypeNames{};
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::ShadingModel)] = "Shading Model";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::TwoSided)] = "Two Sided";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::Wireframe)] = "Wireframe";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::BlendMode)] = "Blend Mode";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::Opacity)] = "Opacity";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::AlphaMode)] = "Alpha Mode";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::AlphaCutoff)] = "Alpha Cutoff";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::BaseColor)] = "Base Color";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::DiffuseColor)] = "Diffuse Color";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::AmbientColor)] = "Ambient Color";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::SpecularColor)] = "Specular Color";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::EmissiveColor)] = "Emissive Color";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::TransparentColor)] = "Transparent Color";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::ReflectiveColor)] = "Reflective Color";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::MetallicFactor)] = "Metallic Factor";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::RoughnessFactor)] = "Roughness Factor";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::NormalScale)] = "Normal Scale";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::OcclusionStrength)] = "Occlusion Strength";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::EmissiveStrength)] = "Emissive Strength";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::DiffuseTexture)] = "Diffuse Texture";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::SpecularTexture)] = "Specular Texture";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::AmbientTexture)] = "Ambient Texture";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::EmissiveTexture)] = "Emissive Texture";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::OpacityTexture)] = "Opacity Texture";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::ShininessTexture)] = "Shininess Texture";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::HeightBumpTexture)] = "Height / Bump Texture";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::NormalTexture)] = "Normal Texture";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::DisplacementTexture)] = "Displacement Texture";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::ReflectionTexture)] = "Reflection Texture";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::LightmapTexture)] = "Lightmap Texture";
            MaterialTypeNames[static_cast<std::size_t>(MaterialType::TerrainLayerCount)] = "Terrain Layer Count";

            for (std::size_t Index{ 0 }; Index < 4; ++Index) {
                MaterialTypeNames[static_cast<std::size_t>(MaterialType::TerrainSplatTexture0) + Index] = std::string{ "Terrain Splat Texture " } + std::to_string(Index);
                MaterialTypeNames[static_cast<std::size_t>(MaterialType::TerrainSplatUvTransform0) + Index] = std::string{ "Terrain Splat UV Transform " } + std::to_string(Index);
            }

            for (std::size_t Index{ 0 }; Index < 16; ++Index) {
                MaterialTypeNames[static_cast<std::size_t>(MaterialType::TerrainDiffuseTexture0) + Index] = std::string{ "Terrain Diffuse Texture " } + std::to_string(Index);
                MaterialTypeNames[static_cast<std::size_t>(MaterialType::TerrainDiffuseColor0) + Index] = std::string{ "Terrain Diffuse Color " } + std::to_string(Index);
                MaterialTypeNames[static_cast<std::size_t>(MaterialType::TerrainNormalTexture0) + Index] = std::string{ "Terrain Normal Texture " } + std::to_string(Index);
                MaterialTypeNames[static_cast<std::size_t>(MaterialType::TerrainNormalColor0) + Index] = std::string{ "Terrain Normal Color " } + std::to_string(Index);
                MaterialTypeNames[static_cast<std::size_t>(MaterialType::TerrainLayerUvTransform0) + Index] = std::string{ "Terrain Layer UV Transform " } + std::to_string(Index);
            }

            return MaterialTypeNames;
        }

        const std::array<std::string, LocalMaterialTypeCount>& GetMaterialTypeNames() {
            static const std::array<std::string, LocalMaterialTypeCount> MaterialTypeNames{ BuildMaterialTypeNames() };
            return MaterialTypeNames;
        }

        const char* MaterialMapKindToString(const MaterialMapKind Kind) {
            switch (Kind) {
            case MaterialMapKind::None:
                return "None";
            case MaterialMapKind::Real:
                return "Real";
            case MaterialMapKind::Vec2:
                return "Vec2";
            case MaterialMapKind::Vec3:
                return "Vec3";
            case MaterialMapKind::Vec4:
                return "Vec4";
            case MaterialMapKind::Int:
                return "Int";
            case MaterialMapKind::Bool:
                return "Bool";
            case MaterialMapKind::String:
                return "String";
            }

            return "None";
        }

        MaterialMapKind MaterialMapKindFromString(const std::string_view KindName) {
            if (KindName == "None") {
                return MaterialMapKind::None;
            }
            if (KindName == "Real") {
                return MaterialMapKind::Real;
            }
            if (KindName == "Vec2") {
                return MaterialMapKind::Vec2;
            }
            if (KindName == "Vec3") {
                return MaterialMapKind::Vec3;
            }
            if (KindName == "Vec4") {
                return MaterialMapKind::Vec4;
            }
            if (KindName == "Int") {
                return MaterialMapKind::Int;
            }
            if (KindName == "Bool") {
                return MaterialMapKind::Bool;
            }
            if (KindName == "String") {
                return MaterialMapKind::String;
            }

            return MaterialMapKind::None;
        }

        std::string_view MaterialTypeToString(const MaterialType Type) {
            const std::size_t Index{ static_cast<std::size_t>(Type) };
            const std::array<std::string, LocalMaterialTypeCount>& MaterialTypeNames{ GetMaterialTypeNames() };
            if (Index >= MaterialTypeNames.size()) {
                return std::string_view{};
            }

            return std::string_view{ MaterialTypeNames[Index] };
        }

        bool TryParseIndexedMaterialType(const std::string_view TypeName, const std::string_view Prefix, MaterialType BaseType, std::size_t MaxCount, MaterialType& Type) {
            if (TypeName.size() <= Prefix.size() || TypeName.substr(0, Prefix.size()) != Prefix) {
                return false;
            }

            std::size_t IndexValue{ 0 };
            for (std::size_t CharacterIndex{ Prefix.size() }; CharacterIndex < TypeName.size(); ++CharacterIndex) {
                const char Character{ TypeName[CharacterIndex] };
                if (Character < '0' || Character > '9') {
                    return false;
                }

                IndexValue = (IndexValue * 10) + static_cast<std::size_t>(Character - '0');
            }

            if (IndexValue >= MaxCount) {
                return false;
            }

            Type = static_cast<MaterialType>(static_cast<std::size_t>(BaseType) + IndexValue);
            return true;
        }

        bool TryParseMaterialType(const std::string_view TypeName, MaterialType& Type) {
            const std::array<std::string, LocalMaterialTypeCount>& MaterialTypeNames{ GetMaterialTypeNames() };
            if (TypeName == "HeightBumpTexture" || TypeName == "Height/Bump" || TypeName == "Height / Bump Texture") {
                Type = MaterialType::HeightBumpTexture;
                return true;
            }
            if (TryParseIndexedMaterialType(TypeName, "Terrain Splat Map ", MaterialType::TerrainSplatTexture0, 4, Type)) {
                return true;
            }
            if (TryParseIndexedMaterialType(TypeName, "Terrain Splat Map Texture ", MaterialType::TerrainSplatTexture0, 4, Type)) {
                return true;
            }
            for (std::size_t Index{ 0 }; Index < MaterialTypeNames.size(); ++Index) {
                if (std::string_view{ MaterialTypeNames[Index] } == TypeName) {
                    Type = static_cast<MaterialType>(Index);
                    return true;
                }
            }

            return false;
        }

        rapidjson::Value SerializeMaterialMap(const MaterialMap& MaterialMapData, rapidjson::Document::AllocatorType& Allocator) {
            rapidjson::Value MaterialMapObject{ rapidjson::kObjectType };
            rapidjson::Value KindValue{};
            const char* KindName{ MaterialMapKindToString(MaterialMapData.GetKind()) };
            KindValue.SetString(KindName, static_cast<rapidjson::SizeType>(std::char_traits<char>::length(KindName)), Allocator);
            MaterialMapObject.AddMember("Kind", KindValue, Allocator);

            switch (MaterialMapData.GetKind()) {
            case MaterialMapKind::None:
                break;
            case MaterialMapKind::Real:
                MaterialMapObject.AddMember("Real", MaterialMapData.GetReal(), Allocator);
                break;
            case MaterialMapKind::Vec2: {
                const Vec2 Value{ MaterialMapData.GetVec2() };
                rapidjson::Value Vec2Array{ rapidjson::kArrayType };
                Vec2Array.PushBack(Value.x, Allocator);
                Vec2Array.PushBack(Value.y, Allocator);
                MaterialMapObject.AddMember("Vec2", Vec2Array, Allocator);
                break;
            }
            case MaterialMapKind::Vec3: {
                const Vec3 Value{ MaterialMapData.GetVec3() };
                rapidjson::Value Vec3Array{ rapidjson::kArrayType };
                Vec3Array.PushBack(Value.x, Allocator);
                Vec3Array.PushBack(Value.y, Allocator);
                Vec3Array.PushBack(Value.z, Allocator);
                MaterialMapObject.AddMember("Vec3", Vec3Array, Allocator);
                break;
            }
            case MaterialMapKind::Vec4: {
                const Vec4 Value{ MaterialMapData.GetVec4() };
                rapidjson::Value Vec4Array{ rapidjson::kArrayType };
                Vec4Array.PushBack(Value.x, Allocator);
                Vec4Array.PushBack(Value.y, Allocator);
                Vec4Array.PushBack(Value.z, Allocator);
                Vec4Array.PushBack(Value.w, Allocator);
                MaterialMapObject.AddMember("Vec4", Vec4Array, Allocator);
                break;
            }
            case MaterialMapKind::Int:
                MaterialMapObject.AddMember("Int", MaterialMapData.GetInt(), Allocator);
                break;
            case MaterialMapKind::Bool:
                MaterialMapObject.AddMember("Bool", MaterialMapData.GetBool(), Allocator);
                break;
            case MaterialMapKind::String: {
                rapidjson::Value StringValue{};
                StringValue.SetString(MaterialMapData.GetString().c_str(), static_cast<rapidjson::SizeType>(MaterialMapData.GetString().size()), Allocator);
                MaterialMapObject.AddMember("String", StringValue, Allocator);
                break;
            }
            }

            return MaterialMapObject;
        }

        MaterialMap DeserializeMaterialMap(const rapidjson::Value& MaterialMapObject) {
            if (!MaterialMapObject.IsObject() || !MaterialMapObject.HasMember("Kind")) {
                return MaterialMap{};
            }

            MaterialMapKind Kind{ MaterialMapKind::None };
            if (MaterialMapObject["Kind"].IsString()) {
                Kind = MaterialMapKindFromString(std::string_view{ MaterialMapObject["Kind"].GetString() });
            } else if (MaterialMapObject["Kind"].IsUint()) {
                Kind = static_cast<MaterialMapKind>(MaterialMapObject["Kind"].GetUint());
            }

            switch (Kind) {
            case MaterialMapKind::None:
                return MaterialMap{};
            case MaterialMapKind::Real:
                return MaterialMap{ MaterialMapObject.HasMember("Real") && MaterialMapObject["Real"].IsNumber() ? MaterialMapObject["Real"].GetFloat() : 0.0f };
            case MaterialMapKind::Vec2: {
                if (MaterialMapObject.HasMember("Vec2") && MaterialMapObject["Vec2"].IsArray() && MaterialMapObject["Vec2"].Size() == 2) {
                    const rapidjson::Value& ArrayValue{ MaterialMapObject["Vec2"] };
                    return MaterialMap{ Vec2{ ArrayValue[0].GetFloat(), ArrayValue[1].GetFloat() } };
                }
                return MaterialMap{ Vec2{} };
            }
            case MaterialMapKind::Vec3: {
                if (MaterialMapObject.HasMember("Vec3") && MaterialMapObject["Vec3"].IsArray() && MaterialMapObject["Vec3"].Size() == 3) {
                    const rapidjson::Value& ArrayValue{ MaterialMapObject["Vec3"] };
                    return MaterialMap{ Vec3{ ArrayValue[0].GetFloat(), ArrayValue[1].GetFloat(), ArrayValue[2].GetFloat() } };
                }
                return MaterialMap{ Vec3{} };
            }
            case MaterialMapKind::Vec4: {
                if (MaterialMapObject.HasMember("Vec4") && MaterialMapObject["Vec4"].IsArray() && MaterialMapObject["Vec4"].Size() == 4) {
                    const rapidjson::Value& ArrayValue{ MaterialMapObject["Vec4"] };
                    return MaterialMap{ Vec4{ ArrayValue[0].GetFloat(), ArrayValue[1].GetFloat(), ArrayValue[2].GetFloat(), ArrayValue[3].GetFloat() } };
                }
                return MaterialMap{ Vec4{} };
            }
            case MaterialMapKind::Int:
                return MaterialMap{ MaterialMapObject.HasMember("Int") && MaterialMapObject["Int"].IsInt64() ? MaterialMapObject["Int"].GetInt64() : 0 };
            case MaterialMapKind::Bool:
                return MaterialMap{ MaterialMapObject.HasMember("Bool") && MaterialMapObject["Bool"].IsBool() ? MaterialMapObject["Bool"].GetBool() : false };
            case MaterialMapKind::String:
                return MaterialMap{ MaterialMapObject.HasMember("String") && MaterialMapObject["String"].IsString() ? std::string{ MaterialMapObject["String"].GetString() } : std::string{} };
            }

            return MaterialMap{};
        }

        MaterialMap DeserializeMaterialMapValue(const rapidjson::Value& MaterialMapValue) {
            if (MaterialMapValue.IsObject()) {
                return DeserializeMaterialMap(MaterialMapValue);
            }

            if (MaterialMapValue.IsString()) {
                return MaterialMap{ std::string{ MaterialMapValue.GetString(), MaterialMapValue.GetStringLength() } };
            }

            if (MaterialMapValue.IsBool()) {
                return MaterialMap{ MaterialMapValue.GetBool() };
            }

            if (MaterialMapValue.IsInt64()) {
                return MaterialMap{ MaterialMapValue.GetInt64() };
            }

            if (MaterialMapValue.IsNumber()) {
                return MaterialMap{ MaterialMapValue.GetFloat() };
            }

            if (MaterialMapValue.IsArray()) {
                const rapidjson::SizeType ValueCount{ MaterialMapValue.Size() };
                if (ValueCount == 2 && MaterialMapValue[0].IsNumber() && MaterialMapValue[1].IsNumber()) {
                    return MaterialMap{ Vec2{ MaterialMapValue[0].GetFloat(), MaterialMapValue[1].GetFloat() } };
                }

                if (ValueCount == 3 && MaterialMapValue[0].IsNumber() && MaterialMapValue[1].IsNumber() && MaterialMapValue[2].IsNumber()) {
                    return MaterialMap{ Vec3{ MaterialMapValue[0].GetFloat(), MaterialMapValue[1].GetFloat(), MaterialMapValue[2].GetFloat() } };
                }

                if (ValueCount >= 4 && MaterialMapValue[0].IsNumber() && MaterialMapValue[1].IsNumber() && MaterialMapValue[2].IsNumber() && MaterialMapValue[3].IsNumber()) {
                    return MaterialMap{ Vec4{ MaterialMapValue[0].GetFloat(), MaterialMapValue[1].GetFloat(), MaterialMapValue[2].GetFloat(), MaterialMapValue[3].GetFloat() } };
                }
            }

            return MaterialMap{};
        }

        void AppendMaterialProperty(Material& MaterialData, MaterialType TypeValue, MaterialMap MapData) {
            MaterialProperty MaterialPropertyData{};
            MaterialPropertyData.Type = TypeValue;
            MaterialPropertyData.Data = std::move(MapData);
            MaterialData.Properties.push_back(std::move(MaterialPropertyData));
        }

        bool TryAppendMaterialPropertyFromMember(Material& MaterialData, const rapidjson::Value& ObjectValue, const char* MemberName, MaterialType TypeValue) {
            if (!ObjectValue.IsObject() || !ObjectValue.HasMember(MemberName)) {
                return false;
            }

            AppendMaterialProperty(MaterialData, TypeValue, DeserializeMaterialMapValue(ObjectValue[MemberName]));
            return true;
        }

        void AppendMaterialPropertiesFromObject(const rapidjson::Value& MaterialObject, Material& MaterialData) {
            if (!MaterialObject.HasMember("Properties")) {
                return;
            }

            if (MaterialObject["Properties"].IsObject()) {
                for (rapidjson::Value::ConstMemberIterator PropertyIterator{ MaterialObject["Properties"].MemberBegin() }; PropertyIterator != MaterialObject["Properties"].MemberEnd(); ++PropertyIterator) {
                    MaterialType TypeValue{};
                    if (!TryParseMaterialType(std::string_view{ PropertyIterator->name.GetString(), PropertyIterator->name.GetStringLength() }, TypeValue)) {
                        continue;
                    }

                    if (!PropertyIterator->value.IsObject()) {
                        continue;
                    }

                    AppendMaterialProperty(MaterialData, TypeValue, DeserializeMaterialMap(PropertyIterator->value));
                }
            } else if (MaterialObject["Properties"].IsArray()) {
                for (const rapidjson::Value& PropertyObject : MaterialObject["Properties"].GetArray()) {
                    if (!PropertyObject.IsObject() || !PropertyObject.HasMember("Type") || !PropertyObject.HasMember("Data") || !PropertyObject["Data"].IsObject()) {
                        continue;
                    }

                    MaterialType TypeValue{};
                    bool IsTypeParsed{ false };
                    if (PropertyObject["Type"].IsString()) {
                        IsTypeParsed = TryParseMaterialType(std::string_view{ PropertyObject["Type"].GetString(), PropertyObject["Type"].GetStringLength() }, TypeValue);
                    } else if (PropertyObject["Type"].IsUint()) {
                        TypeValue = static_cast<MaterialType>(PropertyObject["Type"].GetUint());
                        IsTypeParsed = true;
                    }

                    if (!IsTypeParsed) {
                        continue;
                    }

                    AppendMaterialProperty(MaterialData, TypeValue, DeserializeMaterialMap(PropertyObject["Data"]));
                }
            }
        }

        void AppendTerrainSplatMaterialProperties(const rapidjson::Value& MaterialObject, Material& MaterialData) {
            if (!MaterialObject.HasMember("SplatMaps") || !MaterialObject["SplatMaps"].IsArray()) {
                return;
            }

            const rapidjson::Value& SplatMaps{ MaterialObject["SplatMaps"] };
            const rapidjson::SizeType SplatMapCount{ SplatMaps.Size() > 4 ? 4 : SplatMaps.Size() };
            for (rapidjson::SizeType SplatMapIndex{ 0 }; SplatMapIndex < SplatMapCount; ++SplatMapIndex) {
                const rapidjson::Value& SplatMapValue{ SplatMaps[SplatMapIndex] };
                const MaterialType SplatTextureType{ static_cast<MaterialType>(static_cast<std::uint32_t>(MaterialType::TerrainSplatTexture0) + SplatMapIndex) };
                const MaterialType SplatUvTransformType{ static_cast<MaterialType>(static_cast<std::uint32_t>(MaterialType::TerrainSplatUvTransform0) + SplatMapIndex) };

                if (SplatMapValue.IsObject() && !SplatMapValue.HasMember("Kind")) {
                    bool HasTexture{ TryAppendMaterialPropertyFromMember(MaterialData, SplatMapValue, "Texture", SplatTextureType) };
                    if (HasTexture == false) {
                        HasTexture = TryAppendMaterialPropertyFromMember(MaterialData, SplatMapValue, "SplatTexture", SplatTextureType);
                    }
                    if (HasTexture == false) {
                        TryAppendMaterialPropertyFromMember(MaterialData, SplatMapValue, "SplatMap", SplatTextureType);
                    }

                    bool HasUvTransform{ TryAppendMaterialPropertyFromMember(MaterialData, SplatMapValue, "UvTransform", SplatUvTransformType) };
                    if (HasUvTransform == false) {
                        HasUvTransform = TryAppendMaterialPropertyFromMember(MaterialData, SplatMapValue, "UVTransform", SplatUvTransformType);
                    }
                    if (HasUvTransform == false) {
                        TryAppendMaterialPropertyFromMember(MaterialData, SplatMapValue, "SplatUvTransform", SplatUvTransformType);
                    }
                } else {
                    AppendMaterialProperty(MaterialData, SplatTextureType, DeserializeMaterialMapValue(SplatMapValue));
                }
            }
        }

        std::size_t AppendTerrainLayerMaterialProperties(const rapidjson::Value& MaterialObject, Material& MaterialData) {
            if (!MaterialObject.HasMember("Layers") || !MaterialObject["Layers"].IsArray()) {
                return 0;
            }

            const rapidjson::Value& Layers{ MaterialObject["Layers"] };
            const rapidjson::SizeType LayerCount{ Layers.Size() > 16 ? 16 : Layers.Size() };
            for (rapidjson::SizeType LayerIndex{ 0 }; LayerIndex < LayerCount; ++LayerIndex) {
                const rapidjson::Value& LayerValue{ Layers[LayerIndex] };
                if (!LayerValue.IsObject()) {
                    continue;
                }

                const MaterialType DiffuseTextureType{ static_cast<MaterialType>(static_cast<std::uint32_t>(MaterialType::TerrainDiffuseTexture0) + LayerIndex) };
                const MaterialType DiffuseColorType{ static_cast<MaterialType>(static_cast<std::uint32_t>(MaterialType::TerrainDiffuseColor0) + LayerIndex) };
                const MaterialType NormalTextureType{ static_cast<MaterialType>(static_cast<std::uint32_t>(MaterialType::TerrainNormalTexture0) + LayerIndex) };
                const MaterialType NormalColorType{ static_cast<MaterialType>(static_cast<std::uint32_t>(MaterialType::TerrainNormalColor0) + LayerIndex) };
                const MaterialType UvTransformType{ static_cast<MaterialType>(static_cast<std::uint32_t>(MaterialType::TerrainLayerUvTransform0) + LayerIndex) };

                bool HasDiffuseTexture{ TryAppendMaterialPropertyFromMember(MaterialData, LayerValue, "DiffuseTexture", DiffuseTextureType) };
                if (HasDiffuseTexture == false) {
                    HasDiffuseTexture = TryAppendMaterialPropertyFromMember(MaterialData, LayerValue, "Diffuse Texture", DiffuseTextureType);
                }
                if (HasDiffuseTexture == false) {
                    TryAppendMaterialPropertyFromMember(MaterialData, LayerValue, "Texture", DiffuseTextureType);
                }

                bool HasDiffuseColor{ TryAppendMaterialPropertyFromMember(MaterialData, LayerValue, "DiffuseColor", DiffuseColorType) };
                if (HasDiffuseColor == false) {
                    HasDiffuseColor = TryAppendMaterialPropertyFromMember(MaterialData, LayerValue, "Diffuse Color", DiffuseColorType);
                }
                if (HasDiffuseColor == false) {
                    TryAppendMaterialPropertyFromMember(MaterialData, LayerValue, "Color", DiffuseColorType);
                }

                bool HasNormalTexture{ TryAppendMaterialPropertyFromMember(MaterialData, LayerValue, "NormalTexture", NormalTextureType) };
                if (HasNormalTexture == false) {
                    TryAppendMaterialPropertyFromMember(MaterialData, LayerValue, "Normal Texture", NormalTextureType);
                }

                bool HasNormalColor{ TryAppendMaterialPropertyFromMember(MaterialData, LayerValue, "NormalColor", NormalColorType) };
                if (HasNormalColor == false) {
                    TryAppendMaterialPropertyFromMember(MaterialData, LayerValue, "Normal Color", NormalColorType);
                }

                bool HasUvTransform{ TryAppendMaterialPropertyFromMember(MaterialData, LayerValue, "UvTransform", UvTransformType) };
                if (HasUvTransform == false) {
                    HasUvTransform = TryAppendMaterialPropertyFromMember(MaterialData, LayerValue, "UVTransform", UvTransformType);
                }
                if (HasUvTransform == false) {
                    TryAppendMaterialPropertyFromMember(MaterialData, LayerValue, "LayerUvTransform", UvTransformType);
                }
            }

            return static_cast<std::size_t>(LayerCount);
        }

        void AppendTerrainMaterialProperties(const rapidjson::Value& MaterialObject, Material& MaterialData) {
            AppendTerrainSplatMaterialProperties(MaterialObject, MaterialData);
            const std::size_t LayerCount{ AppendTerrainLayerMaterialProperties(MaterialObject, MaterialData) };
            if (TryAppendMaterialPropertyFromMember(MaterialData, MaterialObject, "LayerCount", MaterialType::TerrainLayerCount) == false && LayerCount > 0) {
                AppendMaterialProperty(MaterialData, MaterialType::TerrainLayerCount, MaterialMap{ static_cast<std::int64_t>(LayerCount) });
            }
        }

        Material DeserializeMaterialObject(const rapidjson::Value& MaterialObject, bool IsTerrainMaterial) {
            Material MaterialData{};
            if (MaterialObject.HasMember("Name") && MaterialObject["Name"].IsString()) {
                MaterialData.Name = MaterialObject["Name"].GetString();
            }
            if (MaterialObject.HasMember("PBR") && MaterialObject["PBR"].IsBool()) {
                MaterialData.PBR = MaterialObject["PBR"].GetBool();
            }

            AppendMaterialPropertiesFromObject(MaterialObject, MaterialData);
            if (IsTerrainMaterial == true) {
                AppendTerrainMaterialProperties(MaterialObject, MaterialData);
            }

            return MaterialData;
        }
    }

    MaterialGroupJsonSerializer::MaterialGroupJsonSerializer() {
    }

    bool MaterialGroupJsonSerializer::WriteToFile(const std::string& Path, const std::vector<MaterialGroup>& MaterialGroups) const {
        rapidjson::Document Document{};
        Document.SetObject();
        rapidjson::Document::AllocatorType& Allocator{ Document.GetAllocator() };

        rapidjson::Value MaterialGroupArray{ rapidjson::kArrayType };
        for (const MaterialGroup& MaterialGroupData : MaterialGroups) {
            rapidjson::Value MaterialGroupObject{ rapidjson::kObjectType };
            rapidjson::Value GroupNameValue{};
            GroupNameValue.SetString(MaterialGroupData.Name.c_str(), static_cast<rapidjson::SizeType>(MaterialGroupData.Name.size()), Allocator);
            MaterialGroupObject.AddMember("Name", GroupNameValue, Allocator);

            rapidjson::Value ItemArray{ rapidjson::kArrayType };
            for (const MaterialGroupItem& MaterialGroupItemData : MaterialGroupData.Items) {
                rapidjson::Value ItemObject{ rapidjson::kObjectType };
                rapidjson::Value PipelineNameValue{};
                PipelineNameValue.SetString(MaterialGroupItemData.PipelineName.c_str(), static_cast<rapidjson::SizeType>(MaterialGroupItemData.PipelineName.size()), Allocator);
                ItemObject.AddMember("PipelineName", PipelineNameValue, Allocator);

                const Material& MaterialData{ MaterialGroupItemData.MaterialData };
                if (MaterialData.Name.empty()) {
                    continue;
                }

                rapidjson::Value MaterialObject{ rapidjson::kObjectType };
                rapidjson::Value MaterialNameValue{};
                MaterialNameValue.SetString(MaterialData.Name.c_str(), static_cast<rapidjson::SizeType>(MaterialData.Name.size()), Allocator);
                MaterialObject.AddMember("Name", MaterialNameValue, Allocator);
                MaterialObject.AddMember("PBR", MaterialData.PBR, Allocator);

                std::array<MaterialMap, LocalMaterialTypeCount> PropertyValues{};
                for (const MaterialProperty& MaterialPropertyData : MaterialData.Properties) {
                    const std::size_t PropertyIndex{ static_cast<std::size_t>(MaterialPropertyData.Type) };
                    if (PropertyIndex < PropertyValues.size()) {
                        PropertyValues[PropertyIndex] = MaterialPropertyData.Data;
                    }
                }

                rapidjson::Value PropertyObject{ rapidjson::kObjectType };
                for (std::size_t TypeIndex{ 0 }; TypeIndex < LocalMaterialTypeCount; ++TypeIndex) {
                    const MaterialType TypeValue{ static_cast<MaterialType>(TypeIndex) };
                    const std::string_view TypeName{ MaterialTypeToString(TypeValue) };
                    if (TypeName.empty()) {
                        continue;
                    }

                    rapidjson::Value TypeNameValue{};
                    TypeNameValue.SetString(TypeName.data(), static_cast<rapidjson::SizeType>(TypeName.size()), Allocator);
                    PropertyObject.AddMember(TypeNameValue, SerializeMaterialMap(PropertyValues[TypeIndex], Allocator), Allocator);
                }

                MaterialObject.AddMember("Properties", PropertyObject, Allocator);
                ItemObject.AddMember("MaterialData", MaterialObject, Allocator);
                ItemArray.PushBack(ItemObject, Allocator);
            }

            MaterialGroupObject.AddMember("Items", ItemArray, Allocator);
            MaterialGroupArray.PushBack(MaterialGroupObject, Allocator);
        }

        Document.AddMember("MaterialGroups", MaterialGroupArray, Allocator);

        rapidjson::StringBuffer StringBuffer{};
        rapidjson::PrettyWriter<rapidjson::StringBuffer> Writer{ StringBuffer };
        Document.Accept(Writer);

        std::ofstream OutputStream{ Path, std::ios::out | std::ios::trunc };
        if (!OutputStream.is_open()) {
            return false;
        }

        OutputStream << StringBuffer.GetString();
        return static_cast<bool>(OutputStream);
    }

    bool MaterialGroupJsonSerializer::ReadFromFile(const std::string& Path, std::vector<MaterialGroup>& MaterialGroups) const {
        std::ifstream InputStream{ Path };
        if (!InputStream.is_open()) {
            return false;
        }

        std::stringstream Buffer{};
        Buffer << InputStream.rdbuf();

        rapidjson::Document Document{};
        Document.Parse(Buffer.str().c_str());
        if (Document.HasParseError() || !Document.IsObject() || !Document.HasMember("MaterialGroups") || !Document["MaterialGroups"].IsArray()) {
            return false;
        }

        MaterialGroups.clear();
        for (const rapidjson::Value& MaterialGroupObject : Document["MaterialGroups"].GetArray()) {
            if (!MaterialGroupObject.IsObject()) {
                continue;
            }

            MaterialGroup MaterialGroupData{};
            if (MaterialGroupObject.HasMember("Name") && MaterialGroupObject["Name"].IsString()) {
                MaterialGroupData.Name = MaterialGroupObject["Name"].GetString();
            }

            if (MaterialGroupObject.HasMember("Items") && MaterialGroupObject["Items"].IsArray()) {
                for (const rapidjson::Value& ItemObject : MaterialGroupObject["Items"].GetArray()) {
                    if (!ItemObject.IsObject()) {
                        continue;
                    }

                    const rapidjson::Value* MaterialObjectPointer{ nullptr };
                    bool IsTerrainMaterial{ false };
                    if (ItemObject.HasMember("TerrainMaterialData") && ItemObject["TerrainMaterialData"].IsObject()) {
                        MaterialObjectPointer = &ItemObject["TerrainMaterialData"];
                        IsTerrainMaterial = true;
                    } else if (ItemObject.HasMember("MaterialData") && ItemObject["MaterialData"].IsObject()) {
                        MaterialObjectPointer = &ItemObject["MaterialData"];
                    }

                    if (MaterialObjectPointer == nullptr) {
                        continue;
                    }

                    MaterialGroupItem MaterialGroupItemData{};
                    if (ItemObject.HasMember("PipelineName") && ItemObject["PipelineName"].IsString()) {
                        MaterialGroupItemData.PipelineName = ItemObject["PipelineName"].GetString();
                    }

                    MaterialGroupItemData.MaterialData = DeserializeMaterialObject(*MaterialObjectPointer, IsTerrainMaterial);

                    MaterialGroupData.Items.push_back(std::move(MaterialGroupItemData));
                }
            }

            MaterialGroups.push_back(std::move(MaterialGroupData));
        }

        return true;
    }
}
