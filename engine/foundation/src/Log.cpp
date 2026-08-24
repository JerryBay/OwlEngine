#include <owl/foundation/Log.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <mutex>
#include <string>

namespace owl::foundation
{
    namespace
    {
        std::mutex loggerMutex;
        std::shared_ptr<spdlog::logger> logger;

        spdlog::level::level_enum ToSpdlogLevel(const LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Trace:
                return spdlog::level::trace;
            case LogLevel::Debug:
                return spdlog::level::debug;
            case LogLevel::Info:
                return spdlog::level::info;
            case LogLevel::Warning:
                return spdlog::level::warn;
            case LogLevel::Error:
                return spdlog::level::err;
            case LogLevel::Critical:
                return spdlog::level::critical;
            }

            return spdlog::level::info;
        }

        std::shared_ptr<spdlog::logger> GetOrCreateLogger()
        {
            std::scoped_lock lock(loggerMutex);
            if (!logger)
            {
                logger = spdlog::get("Owl");
                if (!logger)
                {
                    logger = spdlog::stdout_color_mt("Owl");
                }
                logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
            }
            return logger;
        }
    } // namespace

    void InitializeLogging()
    {
        static_cast<void>(GetOrCreateLogger());
    }

    void ShutdownLogging()
    {
        std::scoped_lock lock(loggerMutex);
        if (logger)
        {
            logger->flush();
            logger.reset();
            spdlog::drop("Owl");
        }
    }

    void SetLogLevel(const LogLevel level)
    {
        GetOrCreateLogger()->set_level(ToSpdlogLevel(level));
    }

    void LogMessage(const LogLevel level, const std::string_view category,
                    const std::string_view message)
    {
        const std::string formatted = "[" + std::string(category) + "] " + std::string(message);
        GetOrCreateLogger()->log(ToSpdlogLevel(level), formatted);
    }
} // namespace owl::foundation
