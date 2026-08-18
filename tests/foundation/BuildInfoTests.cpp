#include <owl/foundation/BuildInfo.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Build information contains the configured identity", "[build-info]")
{
    const owl::foundation::BuildInfo info = owl::foundation::GetBuildInfo();

    CHECK(info.projectVersion == "0.0.1");
    CHECK_FALSE(info.gitRevision.empty());
    CHECK_FALSE(info.buildConfiguration.empty());
    CHECK_FALSE(info.compiler.empty());
    CHECK(info.operatingSystem == "Windows");
    CHECK(info.architecture == "x64");
}
