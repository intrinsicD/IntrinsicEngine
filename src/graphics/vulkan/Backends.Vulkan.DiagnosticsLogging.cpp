#include "Vulkan.hpp"

import Extrinsic.Core.Logging;

namespace Extrinsic::Backends::Vulkan
{
    void ReportVkCheckFailure(const VkCheckSeverity severity,
                              const char* expression,
                              const VkResult result,
                              const char* file,
                              const int line)
    {
        const char* safeExpression = expression ? expression : "<unknown expression>";
        const char* safeFile = file ? file : "<unknown file>";
        const int resultCode = static_cast<int>(result);

        switch (severity)
        {
        case VkCheckSeverity::Warning:
            Core::Log::Warn("[Vulkan WARN] {} = {} at {}:{}",
                            safeExpression,
                            resultCode,
                            safeFile,
                            line);
            break;
        case VkCheckSeverity::Error:
            Core::Log::Error("[Vulkan ERROR] {} = {} at {}:{}",
                             safeExpression,
                             resultCode,
                             safeFile,
                             line);
            break;
        case VkCheckSeverity::Fatal:
            Core::Log::Error("[Vulkan FATAL] {} = {} at {}:{}",
                             safeExpression,
                             resultCode,
                             safeFile,
                             line);
            break;
        }
    }
}

