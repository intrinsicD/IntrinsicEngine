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
