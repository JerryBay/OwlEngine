#include <owl/foundation/Assert.h>

#include <owl/foundation/Log.h>

#include <cstdlib>
#include <string>

namespace owl::foundation
{
[[noreturn]] void ReportAssertion(
    const std::string_view expression,
    const std::string_view message,
    const std::string_view file,
    const int line)
{
    const std::string diagnostic =
        std::string(expression) + " | " + std::string(message) + " | " +
        std::string(file) + ":" + std::to_string(line);
    LogMessage(LogLevel::Critical, "Assert", diagnostic);
    std::abort();
}
}
