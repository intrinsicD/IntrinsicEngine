// JSON side of the shared point-sampling block (RUNTIME-289): writes and reads the prefixed
// fields of a PointSamplingConfig into a section object. Include after <nlohmann/json.hpp>
// and `import Extrinsic.Runtime.PointSamplingConfig;`.
#pragma once

extern "C++"
{
    namespace Extrinsic::Runtime::ConfigDetail
    {
        void EncodePointSampling(nlohmann::json& object, std::string_view prefix, const PointSamplingConfig& config);
        // Reads a merged (validated) object; missing keys keep the config's values.
        void DecodePointSampling(const nlohmann::json& object, std::string_view prefix, PointSamplingConfig& config);
    }
}
