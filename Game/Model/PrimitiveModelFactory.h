#pragma once

#include <string>
#include "Asset/ModelResult.h"

namespace Game {
    class PrimitiveModelFactory final {
    public:
        PrimitiveModelFactory() = delete;
        ~PrimitiveModelFactory() = delete;
        PrimitiveModelFactory(const PrimitiveModelFactory& Other) = delete;
        PrimitiveModelFactory& operator=(const PrimitiveModelFactory& Other) = delete;
        PrimitiveModelFactory(PrimitiveModelFactory&& Other) = delete;
        PrimitiveModelFactory& operator=(PrimitiveModelFactory&& Other) = delete;

    public:
        static bool TryCreateModelResult(const std::string& Selector, asset::ModelResult& OutModelData);
    };
}
