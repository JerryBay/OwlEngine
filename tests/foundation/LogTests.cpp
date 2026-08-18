#include <owl/foundation/Log.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Logging initialization and shutdown are repeatable", "[logging]")
{
    using namespace owl::foundation;

    InitializeLogging();
    InitializeLogging();
    SetLogLevel(LogLevel::Trace);
    LogMessage(LogLevel::Info, "Test", "logging lifecycle");
    ShutdownLogging();
    ShutdownLogging();

    SUCCEED();
}
