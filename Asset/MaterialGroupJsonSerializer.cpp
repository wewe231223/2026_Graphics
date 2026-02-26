#include "MaterialGroupJsonSerializer.h"

#include <array>
#include <fstream>
#include <sstream>
#include <string_view>
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

namespace asset {
    namespace {
        constexpr std::size_t MaterialTypeCount{ 152 };

        const std::array<std::string_view, MaterialTypeCount>& GetMaterialTypeNames() {
            static const std::array<std::string_view, MaterialTypeCount> MaterialTypeNames{
                "DiffuseFactor", "DiffuseFactorMap", "DiffuseColor", "DiffuseColorMap", "SpecularFactor", "SpecularFactorMap", "SpecularColor", "SpecularColorMap", "SpecularExponent", "SpecularExponentMap", "ReflectionFactor", "ReflectionFactorMap", "ReflectionColor", "ReflectionColorMap", "TransparencyFactor", "TransparencyFactorMap", "TransparencyColor", "TransparencyColorMap", "EmissionFactor", "EmissionFactorMap", "EmissionColor", "EmissionColorMap", "AmbientFactor", "AmbientFactorMap", "AmbientColor", "AmbientColorMap", "NormalMap", "NormalMapMap", "Bump", "BumpMap", "BumpFactor", "BumpFactorMap", "DisplacementFactor", "DisplacementFactorMap", "Displacement", "DisplacementMap", "VectorDisplacementFactor", "VectorDisplacementFactorMap", "VectorDisplacement", "VectorDisplacementMap", "BaseFactor", "BaseFactorMap", "BaseColor", "BaseColorMap", "Roughness", "RoughnessMap", "Metalness", "MetalnessMap", "DiffuseRoughness", "DiffuseRoughnessMap", "SpecularFactorPbr", "SpecularFactorPbrMap", "SpecularColorPbr", "SpecularColorPbrMap", "SpecularIor", "SpecularIorMap", "SpecularAnisotropy", "SpecularAnisotropyMap", "SpecularRotation", "SpecularRotationMap", "TransmissionFactor", "TransmissionFactorMap", "TransmissionColor", "TransmissionColorMap", "TransmissionDepth", "TransmissionDepthMap", "TransmissionScatter", "TransmissionScatterMap", "TransmissionScatterAnisotropy", "TransmissionScatterAnisotropyMap", "TransmissionDispersion", "TransmissionDispersionMap", "TransmissionRoughness", "TransmissionRoughnessMap", "TransmissionExtraRoughness", "TransmissionExtraRoughnessMap", "TransmissionPriority", "TransmissionPriorityMap", "TransmissionEnableInAov", "TransmissionEnableInAovMap", "SubsurfaceFactor", "SubsurfaceFactorMap", "SubsurfaceColor", "SubsurfaceColorMap", "SubsurfaceRadius", "SubsurfaceRadiusMap", "SubsurfaceScale", "SubsurfaceScaleMap", "SubsurfaceAnisotropy", "SubsurfaceAnisotropyMap", "SubsurfaceTintColor", "SubsurfaceTintColorMap", "SubsurfaceType", "SubsurfaceTypeMap", "SheenFactor", "SheenFactorMap", "SheenColor", "SheenColorMap", "SheenRoughness", "SheenRoughnessMap", "CoatFactor", "CoatFactorMap", "CoatColor", "CoatColorMap", "CoatRoughness", "CoatRoughnessMap", "CoatIor", "CoatIorMap", "CoatAnisotropy", "CoatAnisotropyMap", "CoatRotation", "CoatRotationMap", "CoatNormal", "CoatNormalMap", "CoatAffectBaseColor", "CoatAffectBaseColorMap", "CoatAffectBaseRoughness", "CoatAffectBaseRoughnessMap", "ThinFilmFactor", "ThinFilmFactorMap", "ThinFilmThickness", "ThinFilmThicknessMap", "ThinFilmIor", "ThinFilmIorMap", "EmissionFactorPbr", "EmissionFactorPbrMap", "EmissionColorPbr", "EmissionColorPbrMap", "Opacity", "OpacityMap", "IndirectDiffuse", "IndirectDiffuseMap", "IndirectSpecular", "IndirectSpecularMap", "NormalMapPbr", "NormalMapPbrMap", "TangentMap", "TangentMapMap", "DisplacementMapPbr", "DisplacementMapPbrMap", "MatteFactor", "MatteFactorMap", "MatteColor", "MatteColorMap", "AmbientOcclusion", "AmbientOcclusionMap", "Glossiness", "GlossinessMap", "CoatGlossiness", "CoatGlossinessMap", "TransmissionGlossiness", "TransmissionGlossinessMap"
            };

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
            const std::array<std::string_view, MaterialTypeCount>& MaterialTypeNames{ GetMaterialTypeNames() };
            if (Index >= MaterialTypeNames.size()) {
                return std::string_view{};
            }

            return MaterialTypeNames[Index];
        }

        bool TryParseMaterialType(const std::string_view TypeName, MaterialType& Type) {
            const std::array<std::string_view, MaterialTypeCount>& MaterialTypeNames{ GetMaterialTypeNames() };
            for (std::size_t Index{ 0 }; Index < MaterialTypeNames.size(); ++Index) {
                if (MaterialTypeNames[Index] == TypeName) {
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
                Vec2Array.PushBack(Value.mX, Allocator);
                Vec2Array.PushBack(Value.mY, Allocator);
                MaterialMapObject.AddMember("Vec2", Vec2Array, Allocator);
                break;
            }
            case MaterialMapKind::Vec3: {
                const Vec3 Value{ MaterialMapData.GetVec3() };
                rapidjson::Value Vec3Array{ rapidjson::kArrayType };
                Vec3Array.PushBack(Value.mX, Allocator);
                Vec3Array.PushBack(Value.mY, Allocator);
                Vec3Array.PushBack(Value.mZ, Allocator);
                MaterialMapObject.AddMember("Vec3", Vec3Array, Allocator);
                break;
            }
            case MaterialMapKind::Vec4: {
                const Vec4 Value{ MaterialMapData.GetVec4() };
                rapidjson::Value Vec4Array{ rapidjson::kArrayType };
                Vec4Array.PushBack(Value.mX, Allocator);
                Vec4Array.PushBack(Value.mY, Allocator);
                Vec4Array.PushBack(Value.mZ, Allocator);
                Vec4Array.PushBack(Value.mW, Allocator);
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
                rapidjson::Value MaterialObject{ rapidjson::kObjectType };
                rapidjson::Value MaterialNameValue{};
                MaterialNameValue.SetString(MaterialData.Name.c_str(), static_cast<rapidjson::SizeType>(MaterialData.Name.size()), Allocator);
                MaterialObject.AddMember("Name", MaterialNameValue, Allocator);
                MaterialObject.AddMember("PBR", MaterialData.PBR, Allocator);

                std::array<MaterialMap, MaterialTypeCount> PropertyValues{};
                for (const MaterialProperty& MaterialPropertyData : MaterialData.Properties) {
                    const std::size_t PropertyIndex{ static_cast<std::size_t>(MaterialPropertyData.Type) };
                    if (PropertyIndex < PropertyValues.size()) {
                        PropertyValues[PropertyIndex] = MaterialPropertyData.Data;
                    }
                }

                rapidjson::Value PropertyObject{ rapidjson::kObjectType };
                for (std::size_t TypeIndex{ 0 }; TypeIndex < MaterialTypeCount; ++TypeIndex) {
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
                    if (!ItemObject.IsObject() || !ItemObject.HasMember("MaterialData") || !ItemObject["MaterialData"].IsObject()) {
                        continue;
                    }

                    MaterialGroupItem MaterialGroupItemData{};
                    if (ItemObject.HasMember("PipelineName") && ItemObject["PipelineName"].IsString()) {
                        MaterialGroupItemData.PipelineName = ItemObject["PipelineName"].GetString();
                    }

                    const rapidjson::Value& MaterialObject{ ItemObject["MaterialData"] };
                    if (MaterialObject.HasMember("Name") && MaterialObject["Name"].IsString()) {
                        MaterialGroupItemData.MaterialData.Name = MaterialObject["Name"].GetString();
                    }
                    if (MaterialObject.HasMember("PBR") && MaterialObject["PBR"].IsBool()) {
                        MaterialGroupItemData.MaterialData.PBR = MaterialObject["PBR"].GetBool();
                    }

                    if (MaterialObject.HasMember("Properties") && MaterialObject["Properties"].IsObject()) {
                        for (rapidjson::Value::ConstMemberIterator PropertyIterator{ MaterialObject["Properties"].MemberBegin() }; PropertyIterator != MaterialObject["Properties"].MemberEnd(); ++PropertyIterator) {
                            MaterialType TypeValue{};
                            if (!TryParseMaterialType(std::string_view{ PropertyIterator->name.GetString(), PropertyIterator->name.GetStringLength() }, TypeValue)) {
                                continue;
                            }

                            if (!PropertyIterator->value.IsObject()) {
                                continue;
                            }

                            MaterialProperty MaterialPropertyData{};
                            MaterialPropertyData.Type = TypeValue;
                            MaterialPropertyData.Data = DeserializeMaterialMap(PropertyIterator->value);
                            MaterialGroupItemData.MaterialData.Properties.push_back(std::move(MaterialPropertyData));
                        }
                    } else if (MaterialObject.HasMember("Properties") && MaterialObject["Properties"].IsArray()) {
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

                            MaterialProperty MaterialPropertyData{};
                            MaterialPropertyData.Type = TypeValue;
                            MaterialPropertyData.Data = DeserializeMaterialMap(PropertyObject["Data"]);
                            MaterialGroupItemData.MaterialData.Properties.push_back(std::move(MaterialPropertyData));
                        }
                    }

                    MaterialGroupData.Items.push_back(std::move(MaterialGroupItemData));
                }
            }

            MaterialGroups.push_back(std::move(MaterialGroupData));
        }

        return true;
    }
}
