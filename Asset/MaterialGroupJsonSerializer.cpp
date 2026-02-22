#include "MaterialGroupJsonSerializer.h"

#include <fstream>
#include <sstream>
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"

namespace asset {
    namespace {
        rapidjson::Value SerializeMaterialMap(const MaterialMap& MaterialMapData, rapidjson::Document::AllocatorType& Allocator) {
            rapidjson::Value MaterialMapObject{ rapidjson::kObjectType };
            MaterialMapObject.AddMember("Kind", static_cast<std::uint32_t>(MaterialMapData.GetKind()), Allocator);

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
            if (!MaterialMapObject.IsObject() || !MaterialMapObject.HasMember("Kind") || !MaterialMapObject["Kind"].IsUint()) {
                return MaterialMap{};
            }

            const MaterialMapKind Kind{ static_cast<MaterialMapKind>(MaterialMapObject["Kind"].GetUint()) };
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

                rapidjson::Value PropertyArray{ rapidjson::kArrayType };
                for (const MaterialProperty& MaterialPropertyData : MaterialData.Properties) {
                    rapidjson::Value PropertyObject{ rapidjson::kObjectType };
                    PropertyObject.AddMember("Type", static_cast<std::uint32_t>(MaterialPropertyData.Type), Allocator);
                    PropertyObject.AddMember("Data", SerializeMaterialMap(MaterialPropertyData.Data, Allocator), Allocator);
                    PropertyArray.PushBack(PropertyObject, Allocator);
                }
                MaterialObject.AddMember("Properties", PropertyArray, Allocator);
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

                    if (MaterialObject.HasMember("Properties") && MaterialObject["Properties"].IsArray()) {
                        for (const rapidjson::Value& PropertyObject : MaterialObject["Properties"].GetArray()) {
                            if (!PropertyObject.IsObject() || !PropertyObject.HasMember("Type") || !PropertyObject["Type"].IsUint() || !PropertyObject.HasMember("Data") || !PropertyObject["Data"].IsObject()) {
                                continue;
                            }

                            MaterialProperty MaterialPropertyData{};
                            MaterialPropertyData.Type = static_cast<MaterialType>(PropertyObject["Type"].GetUint());
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
