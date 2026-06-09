#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include "Game/Scene/Pipeline/PipelineSystem.h"

namespace Game {
    namespace Pipeline {
        class PipelineSystemRegistry final {
        public:
            using Factory = std::function<std::unique_ptr<IPipelineSystem>()>;

        public:
            PipelineSystemRegistry();
            ~PipelineSystemRegistry();

            PipelineSystemRegistry(const PipelineSystemRegistry& Other);
            PipelineSystemRegistry& operator=(const PipelineSystemRegistry& Other);

            PipelineSystemRegistry(PipelineSystemRegistry&& Other) noexcept;
            PipelineSystemRegistry& operator=(PipelineSystemRegistry&& Other) noexcept;

        public:
            bool RegisterFactory(const std::string& SystemName, Factory FactoryValue);
            std::unique_ptr<IPipelineSystem> CreateSystem(const std::string& SystemName) const;
            bool Contains(const std::string& SystemName) const;

        private:
            std::unordered_map<std::string, Factory> mFactoriesByName{};
        };
    }
}
