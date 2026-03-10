module;

#include <cstdint>
#include <glm/glm.hpp>

export module Graphics:Passes.PostProcessSettings;

export namespace Graphics::Passes
{
    // -----------------------------------------------------------------
    // Tone-mapping operator selection (push constant enum).
    // -----------------------------------------------------------------
    enum class ToneMapOperator : int
    {
        ACES       = 0,
        Reinhard   = 1,
        Uncharted2 = 2,
    };

    // -----------------------------------------------------------------
    // Anti-aliasing mode selection.
    // -----------------------------------------------------------------
    enum class AAMode : int
    {
        None = 0,
        FXAA = 1,
        SMAA = 2,
    };

    // -----------------------------------------------------------------
    // Post-processing settings exposed to the editor UI.
    // -----------------------------------------------------------------
    struct PostProcessSettings
    {
        // Tone mapping
        float            Exposure     = 1.0f;
        ToneMapOperator  ToneOperator = ToneMapOperator::ACES;

        // Color grading (applied in linear space after tone mapping, before gamma)
        bool  ColorGradingEnabled = false;
        float Saturation          = 1.0f;   // 0 = grayscale, 1 = neutral, 2 = oversaturated
        float Contrast            = 1.0f;   // Midtone contrast (0.5 = flat, 1 = neutral, 2 = punchy)
        glm::vec3 Lift  = glm::vec3(0.0f);  // Shadow tint (additive, range ~[-0.5, 0.5])
        glm::vec3 Gamma = glm::vec3(1.0f);  // Midtone power (< 1 = brighter, > 1 = darker)
        glm::vec3 Gain  = glm::vec3(1.0f);  // Highlight multiplier
        float ColorTempOffset = 0.0f; // Kelvin-style offset: negative = cooler (blue), positive = warmer (orange). Range [-1, 1].
        float TintOffset      = 0.0f; // Green-magenta tint. Range [-1, 1].

        // Bloom
        bool  BloomEnabled      = true;
        float BloomThreshold    = 1.0f;   // Brightness threshold for bloom extraction
        float BloomIntensity    = 0.04f;  // Bloom contribution strength
        float BloomFilterRadius = 1.0f;   // Upsample tent filter radius

        // Anti-aliasing
        AAMode AntiAliasingMode = AAMode::SMAA; // Default to SMAA (higher quality)

        // FXAA settings (used when AntiAliasingMode == FXAA)
        float FXAAContrastThreshold  = 0.0312f;
        float FXAARelativeThreshold  = 0.063f;
        float FXAASubpixelBlending   = 0.75f;

        // SMAA settings (used when AntiAliasingMode == SMAA)
        float SMAAEdgeThreshold      = 0.1f;   // Luma edge detection threshold
        int   SMAAMaxSearchSteps     = 16;     // Max orthogonal search distance
        int   SMAAMaxSearchStepsDiag = 8;      // Max diagonal search distance

        [[nodiscard]] bool FXAAEnabled() const { return AntiAliasingMode == AAMode::FXAA; }
        [[nodiscard]] bool SMAAEnabled() const { return AntiAliasingMode == AAMode::SMAA; }

        // Luminance Histogram (debug exposure tool)
        bool  HistogramEnabled     = false;
        float HistogramMinEV       = -10.0f;  // log2(min luminance) for binning range
        float HistogramMaxEV       =  10.0f;  // log2(max luminance) for binning range
    };

    // CPU-side histogram readback data for UI display.
    inline constexpr uint32_t kHistogramBinCount = 256;

    struct HistogramReadback
    {
        uint32_t Bins[kHistogramBinCount] = {};
        float    AverageLuminance = 0.0f;
        bool     Valid            = false;
    };
}
