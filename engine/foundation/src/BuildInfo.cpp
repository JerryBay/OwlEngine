#include <owl/foundation/BuildInfo.h>

#include <owl/foundation/generated/BuildInfoConfig.h>

namespace owl::foundation
{
BuildInfo GetBuildInfo() noexcept
{
    return BuildInfo{
        .projectVersion = OWL_PROJECT_VERSION,
        .gitRevision = OWL_GIT_REVISION,
        .buildConfiguration = OWL_BUILD_CONFIGURATION,
        .compiler = OWL_COMPILER,
        .operatingSystem = OWL_TARGET_SYSTEM,
        .architecture = OWL_TARGET_ARCHITECTURE,
    };
}
} // namespace owl::foundation
