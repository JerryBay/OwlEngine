#pragma once

#include <string_view>

namespace owl::foundation
{
enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
};

void InitializeLogging();
void ShutdownLogging();
void SetLogLevel(LogLevel level);
void LogMessage(LogLevel level, std::string_view category, std::string_view message);
} // namespace owl::foundation
