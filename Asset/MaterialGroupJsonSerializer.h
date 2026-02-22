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
