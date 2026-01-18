#include "ErrorHandler.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <format>
#include <sstream>
#include "Utility/ExternalLinkers.h"
#include "cpptrace/cpptrace.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace ErrorHandler {
    namespace {
        std::ofstream g_logFile;

        void ensureLogFileOpen() {
            if (!g_logFile.is_open()) {
                auto now = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
                std::string filename = std::format("Log/log_{:%Y%m%d_%H%M%S}.txt", now);

                g_logFile.open(filename, std::ios::out | std::ios::app);
                if (g_logFile) {
                    g_logFile << std::format("=== Log Started at {:%Y-%m-%d %H:%M:%S} ===\n\n", now);
                }
            }
        }

        std::string getDetailedStacktrace() {
            std::stringstream ss;
            ss << "\n=== STACK TRACE ==\n";

            cpptrace::generate_trace().print(ss, false); 

            ss << "==============================\n";
            return ss.str();
        }
    }

    void report(std::string_view title, std::string_view message, Level level) {
        ensureLogFileOpen();

        auto now = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
        std::string_view levelStr = (level == Level::Critical) ? "CRITICAL" : "WARNING";

        std::string logContent = std::format(
            "----------------------------------------\n"
            "[{:%H:%M:%S}] [{}] {}\n"
            "Message: {}\n",
            now, levelStr, title, message
        );

        if (level == Level::Critical) {
            logContent += getDetailedStacktrace();
        }

        if (g_logFile.is_open()) {
            g_logFile << logContent;
            g_logFile.flush();
        }

        if (level == Level::Critical) {
#ifdef _WIN32
            MessageBoxA(NULL, message.data(), title.data(), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
#endif
            std::exit(EXIT_FAILURE);
        }
    }

    void report(bool condition, std::string_view title, std::string_view message, Level level) {
        if (condition) {
            report(title, message, level);
        }
    }

    void report(long hr, std::string_view title, std::string_view message, Level level) {
        if (hr < 0) {
            std::string formattedMessage = std::format("{} (HRESULT: 0x{:08X})", message, static_cast<unsigned long>(hr));
            report(title, formattedMessage, level);
        }
    }
}