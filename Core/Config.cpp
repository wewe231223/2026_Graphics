#include "Config.h"
#include "Utility/ErrorHandler.h"
#include <fstream>
#include <sstream>
#include <algorithm>

FileConfig::FileConfig(const std::string& filePath) : mPath(filePath) {
    LoadContents();
}

void FileConfig::LoadContents() {
    std::ifstream file(mPath);

    // 1. 파일 오픈 실패 핸들링
    if (!file.is_open()) {
        ErrorHandler::report("File Load Error",
            "Could not open config file: " + mPath,
            ErrorHandler::Level::Critical); 
        return;
    }

    mData.clear();
    std::string line;
    int lineCount = 0;

    while (std::getline(file, line)) {
        lineCount++;

        // 공백 제거 및 주석(#) 처리
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        if (line.empty() || line[0] == '#') continue;

        // '=' 구분자로 키와 값 분리
        size_t delimiterPos = line.find('=');
        if (delimiterPos == std::string::npos) {
            ErrorHandler::report("Config Syntax Warning",
                "Invalid format at " + mPath + ":" + std::to_string(lineCount),
                ErrorHandler::Level::Warning);
            continue;
        }

        std::string key = line.substr(0, delimiterPos);
        std::string value = line.substr(delimiterPos + 1);

        auto trim = [](std::string& s) {
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            };
        trim(key);
        trim(value);

        if (!key.empty()) {
            mData[key] = value;
        }
    }
}

std::string FileConfig::GetValueInternal(const std::string& key) const {
    auto it = mData.find(key);
    return (it != mData.end()) ? it->second : "";
}

bool FileConfig::Reload() {
    LoadContents();
    return !mData.empty();
}
