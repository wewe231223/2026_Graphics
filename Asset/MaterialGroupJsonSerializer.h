//{ "MaterialGroups": [...] }
//          │
//          ▼
//┌───────────────────────────────────────────────────────────────────────────┐
//│                            [Material Group Item]                          │
//├───────────────────────────────────────────────────────────────────────────┤
//│  "Name": "<GroupName>"                                                    │
//│  "Items" : [                                                              │
//│{                                                                          │
//│        "PipelineName": "<PipelineName>",                                  │
//│        "MaterialData" : {                                                 │
//│           "Name": "<MaterialName>",                                       │
//│           "PBR" : <bool>,                                                 │
//│           "Properties" : {                                                │
//│              " <MaterialTypeName> ": {   ──────────────────┐              │
//│                 "Kind": "None|Real|Vec2|...|String",       │              │
//│                 "Value" : (Based on Kind)  ────────────────┤              │
//│              }                                             │              │
//│           }                                                │              │
//│        }                                                   │              │
//│ }, ...                                                     │              │
//│  ]                                                         │              │
//└────────────────────────────────────────────────────────────┼──────────────┘
//                                                             │
//            ┌────────────────────────────────────────────────┘
//            │
//            ▼
//┌───────────────────────────────────────────────────────────────────────────┐
//│                        [Property Value Variants]                          │
//├────────────┬────────────────────────┬─────────────────────────────────────┤
//│    KIND    │       FIELD KEY        │             EXAMPLE VALUE           │
//├────────────┼────────────────────────┼─────────────────────────────────────┤
//│  Real      │  "Real"                │  1.0                                │
//│  Vec2      │  "Vec2"                │  [x, y]                             │
//│  Vec3      │  "Vec3"                │  [x, y, z]                          │
//│  Vec4      │  "Vec4"                │  [x, y, z, w]                       │
//│  Int       │  "Int"                 │  42                                 │
//│  Bool      │  "Bool"                │  true                               │
//│  String    │  "String"              │  "path/to/texture.dds"              │
//└────────────┴────────────────────────┴─────────────────────────────────────┤
//* 'Kind' 필드에 지정된 타입에 해당하는 하나의 필드만 유효값으로 처리됩니다.                   
//└───────────────────────────────────────────────────────────────────────────┘

#pragma once

#include <string>
#include <vector>
#include "Common.h"

namespace asset {
    class MaterialGroupJsonSerializer final {
    public:
        MaterialGroupJsonSerializer();
        ~MaterialGroupJsonSerializer() = default;

        MaterialGroupJsonSerializer(const MaterialGroupJsonSerializer& Other) = delete;
        MaterialGroupJsonSerializer& operator=(const MaterialGroupJsonSerializer& Other) = delete;
        MaterialGroupJsonSerializer(MaterialGroupJsonSerializer&& Other) noexcept = delete;
        MaterialGroupJsonSerializer& operator=(MaterialGroupJsonSerializer&& Other) noexcept = delete;

    public:
        bool WriteToFile(const std::string& Path, const std::vector<MaterialGroup>& MaterialGroups) const;
        bool ReadFromFile(const std::string& Path, std::vector<MaterialGroup>& MaterialGroups) const;
    };
}
