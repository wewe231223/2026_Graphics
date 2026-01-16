#include <string>
#include <string_view>
#include <unordered_map>
#include <charconv>      
#include <type_traits>   
#include "Utility/ErrorHandler.h"

class IConfig {
protected:
    virtual std::string GetValueInternal(const std::string& key) const = 0;

public:
    virtual ~IConfig() = default;

    template <typename T>
    T Get(const std::string& key) const {
        std::string val = GetValueInternal(key);

        if (val.empty()) {
            ErrorHandler::report("Config Missing", "Key '" + key + "' not found.", ErrorHandler::Level::Critical);
            return T{};
        }

        if constexpr (std::is_same_v<T, std::string>) {
            return val;
        }
        else if constexpr (std::is_same_v<T, bool>) {
            return (val == "true" || val == "1");
        }
        else if constexpr (std::is_arithmetic_v<T>) {
            T result{};
            auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), result);

            if (ec == std::errc{}) return result;

            ErrorHandler::report("Config Parse Error",
                "Key '" + key + "' has invalid value '" + val + "' for numeric type.",
                ErrorHandler::Level::Critical);
            return T{};
        }
        else {
            ErrorHandler::report("Config Type Error",
                "Requested type for key '" + key + "' is not supported by Config system.",
                ErrorHandler::Level::Critical);
            return T{};
        }
    }
};


class Config {
    static inline IConfig* instance = nullptr;
public:
    static void Init(IConfig* inst) { instance = inst; }
    static const IConfig& Query() { return *instance; }
};


class FileConfig : public IConfig {
public:
    explicit FileConfig(const std::string& filePath);

public:
    std::string GetValueInternal(const std::string& key) const override;
    bool Reload();

private:
    void LoadContents();

private:
    std::string mPath;
    std::unordered_map<std::string, std::string> mData;

};