#pragma once
#include <string_view>

namespace ErrorHandler {
    enum class Level { Info, Warning, Critical };


    void report(std::string_view title, std::string_view message, Level level = Level::Warning);
}