#pragma once

#include <string_view>

namespace owl::foundation
{
[[noreturn]] void ReportAssertion(
    std::string_view expression,
    std::string_view message,
    std::string_view file,
    int line);
}

#if defined(OWL_ENABLE_ASSERTS)
#define OWL_ASSERT(expression, message)                                      \
    ((expression)                                                            \
         ? static_cast<void>(0)                                              \
         : ::owl::foundation::ReportAssertion(                               \
               #expression, (message), __FILE__, __LINE__))
#else
#define OWL_ASSERT(expression, message)                                      \
    do                                                                       \
    {                                                                        \
        static_cast<void>(sizeof(expression));                               \
        static_cast<void>(sizeof(message));                                  \
    } while (false)
#endif
