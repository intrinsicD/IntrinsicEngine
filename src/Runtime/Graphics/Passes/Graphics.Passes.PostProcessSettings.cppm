module;

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
        float LiftR = 0.0f, LiftG = 0.0f, LiftB = 0.0f;   // Shadow tint (additive, range ~[-0.5, 0.5])
        float GammaR = 1.0f, GammaG = 1.0f, GammaB = 1.0f; // Midtone power (< 1 = brighter, > 1 = darker)
        float GainR = 1.0f, GainG = 1.0f, GainB = 1.0f;     // Highlight multiplier
        float ColorTempOffset = 0.0f; // Kelvin-style offset: negative = cooler (blue), positive = warmer (orange). Range [-1, 1].
        float TintOffset      = 0.0f; // Green-magenta tint. Range [-1, 1].

        // Bloom
        bool  BloomEnabled      = true;
        float BloomThreshold    = 1.0f;   // Brightness threshold for bloom extraction
        float BloomIntensity    = 0.04f;  // Bloom contribution strength
        float BloomFilterRadius = 1.0f;   // Upsample tent filter radius

        // FXAA
        bool  FXAAEnabled          = true;
        float FXAAContrastThreshold  = 0.0312f;
        float FXAARelativeThreshold  = 0.063f;
        float FXAASubpixelBlending   = 0.75f;
    };
}
