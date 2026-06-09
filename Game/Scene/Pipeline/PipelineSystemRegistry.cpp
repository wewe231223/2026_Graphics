#include "PipelineSystemRegistry.h"
#include <utility>

namespace Game {
    namespace Pipeline {
        PipelineSystemRegistry::PipelineSystemRegistry()
            : mFactoriesByName{} {
        }

        PipelineSystemRegistry::~PipelineSystemRegistry() {
        }

        PipelineSystemRegistry::PipelineSystemRegistry(const PipelineSystemRegistry& Other)
            : mFactoriesByName{ Other.mFactoriesByName } {
        }

        PipelineSystemRegistry& PipelineSystemRegistry::operator=(const PipelineSystemRegistry& Other) {
            if (this == &Other) {
                return *this;
            }

            mFactoriesByName = Other.mFactoriesByName;
            return *this;
        }

        PipelineSystemRegistry::PipelineSystemRegistry(PipelineSystemRegistry&& Other) noexcept
            : mFactoriesByName{ std::move(Other.mFactoriesByName) } {
        }

        PipelineSystemRegistry& PipelineSystemRegistry::operator=(PipelineSystemRegistry&& Other) noexcept {
            if (this == &Other) {
                return *this;
            }

            mFactoriesByName = std::move(Other.mFactoriesByName);
            return *this;
        }

        bool PipelineSystemRegistry::RegisterFactory(const std::string& SystemName, Factory FactoryValue) {
            if (SystemName.empty() == true) {
                return false;
            }

            if (static_cast<bool>(FactoryValue) == false) {
                return false;
            }

            if (Contains(SystemName) == true) {
                return false;
            }

            mFactoriesByName.emplace(SystemName, std::move(FactoryValue));
            return true;
        }

        std::unique_ptr<IPipelineSystem> PipelineSystemRegistry::CreateSystem(const std::string& SystemName) const {
            const std::unordered_map<std::string, Factory>::const_iterator FactoryIter{ mFactoriesByName.find(SystemName) };
            if (FactoryIter == mFactoriesByName.end()) {
                return nullptr;
            }

            return FactoryIter->second();
        }

        bool PipelineSystemRegistry::Contains(const std::string& SystemName) const {
            return mFactoriesByName.find(SystemName) != mFactoriesByName.end();
        }
    }
}
