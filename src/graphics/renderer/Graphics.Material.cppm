module;

#include <cstdint>
#include <cstddef>
#include <glm/glm.hpp>
#include <string>
#include <vector>

export module Extrinsic.Graphics.Material;

import Extrinsic.Core.StrongHandle;
import Extrinsic.Asset.Registry;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Types;

// ============================================================
// Graphics.Material — material type system data types.
//
// These are the CPU-side representations.  The GPU layout is
// GpuMaterialSlot (RHI.Types), a 128-byte std430 struct with
// 48 bytes of standard PBR fields, 16 bytes of material type/flags and
// padding, and 64 bytes of custom data.
//
// Separation of concerns:
//   Graphics.Material      — descriptors, params, handles (this file)
//   Graphics.MaterialSystem — the manager, GPU buffer, sync
// ============================================================

export namespace Extrinsic::Graphics
{
    // -----------------------------------------------------------------
    // Canonical material slot/layout contract
    // -----------------------------------------------------------------
    // Material slots are graphics-owned indices into the MaterialSystem SSBO.
    // Runtime may cache entity/asset -> slot sidecars, but canonical ECS
    // components store CPU material descriptions or asset IDs, never these
    // graphics-owned slot values.
    // -----------------------------------------------------------------
    inline constexpr std::uint32_t kDefaultMaterialSlotIndex = 0u;
    inline constexpr std::uint32_t kMaterialLayoutVersion = 1u;
    inline constexpr std::size_t   kMaterialSlotSizeBytes = sizeof(RHI::GpuMaterialSlot);
    inline constexpr std::uint32_t kMaterialCustomDataSlotCount = 4u;
    inline constexpr std::uint32_t kMaterialTextureBindingCount = 4u;

    enum class MaterialTextureSemantic : std::uint32_t
    {
        Albedo = 0,
        Normal = 1,
        MetallicRoughness = 2,
        Emissive = 3,
    };

    struct MaterialLayoutContract
    {
        std::uint32_t Version = kMaterialLayoutVersion;
        std::uint32_t DefaultSlot = kDefaultMaterialSlotIndex;
        std::uint32_t SlotSizeBytes = static_cast<std::uint32_t>(kMaterialSlotSizeBytes);
        std::uint32_t CustomVec4SlotCount = kMaterialCustomDataSlotCount;
        std::uint32_t TextureBindingCount = kMaterialTextureBindingCount;
    };

    [[nodiscard]] constexpr MaterialLayoutContract GetCanonicalMaterialLayoutContract() noexcept
    {
        return {};
    }

    enum class MaterialLayoutReloadDecision : std::uint32_t
    {
        Compatible = 0,
        VersionMismatch,
        DefaultSlotMismatch,
        SlotSizeMismatch,
        CustomSlotCountMismatch,
        TextureBindingCountMismatch,
    };

    [[nodiscard]] constexpr MaterialLayoutReloadDecision EvaluateMaterialLayoutReloadCompatibility(
        const MaterialLayoutContract& current,
        const MaterialLayoutContract& reloaded) noexcept
    {
        if (current.Version != reloaded.Version)
            return MaterialLayoutReloadDecision::VersionMismatch;
        if (current.DefaultSlot != reloaded.DefaultSlot)
            return MaterialLayoutReloadDecision::DefaultSlotMismatch;
        if (current.SlotSizeBytes != reloaded.SlotSizeBytes)
            return MaterialLayoutReloadDecision::SlotSizeMismatch;
        if (current.CustomVec4SlotCount != reloaded.CustomVec4SlotCount)
            return MaterialLayoutReloadDecision::CustomSlotCountMismatch;
        if (current.TextureBindingCount != reloaded.TextureBindingCount)
            return MaterialLayoutReloadDecision::TextureBindingCountMismatch;
        return MaterialLayoutReloadDecision::Compatible;
    }

    [[nodiscard]] constexpr bool IsMaterialLayoutReloadCompatible(
        const MaterialLayoutContract& current,
        const MaterialLayoutContract& reloaded) noexcept
    {
        return EvaluateMaterialLayoutReloadCompatibility(current, reloaded) ==
               MaterialLayoutReloadDecision::Compatible;
    }

    static_assert(kMaterialSlotSizeBytes == 128u, "GpuMaterialSlot layout version 1 is 128 bytes");

    // -----------------------------------------------------------------
    // Well-known material type IDs
    // -----------------------------------------------------------------
    // These are the registration order of built-in types; all three are
    // registered by MaterialSystem::Initialize() so the canonical TypeIDs
    // are reserved before any subsystem-specific registration runs.
    // Shaders branch on GpuMaterialSlot::MaterialTypeID.
    // -----------------------------------------------------------------
    inline constexpr std::uint32_t kMaterialTypeID_StandardPBR         = 0u;
    inline constexpr std::uint32_t kMaterialTypeID_SciVis              = 1u;
    inline constexpr std::uint32_t kMaterialTypeID_DefaultDebugSurface = 2u;

    // Built-in type registration names — referenced by MaterialSystem::Initialize()
    // and by subsystems that look up the registered handles (e.g. VisualizationSyncSystem).
    inline constexpr const char* kMaterialTypeName_StandardPBR         = "StandardPBR";
    inline constexpr const char* kMaterialTypeName_SciVis              = "SciVis";
    inline constexpr const char* kMaterialTypeName_DefaultDebugSurface = "Material.DefaultDebugSurface";

    // Slot 0 carries the canonical missing-material fallback so any
    // invalid MaterialHandle resolves to a visible purple debug surface.
    inline constexpr float kDefaultDebugSurfaceBaseColor[4]{0.55f, 0.20f, 0.85f, 1.0f};

    // -----------------------------------------------------------------
    // SciVis material — CustomData layout documentation
    // -----------------------------------------------------------------
    // When MaterialTypeID == kMaterialTypeID_SciVis, the four CustomData
    // vec4 slots are used as follows (values stored via std::bit_cast<float>
    // from uint32_t where noted):
    //
    //   CustomData[0]:
    //     [0] colormapBindlessIndex  -- uint32 via bit_cast
    //     [1] domain                  -- 0=vertex,1=edge,2=face via bit_cast
    //     [2] rangeMin                -- float
    //     [3] rangeMax                -- float
    //
    //   CustomData[1]:
    //     [0] isolineCount            -- uint32 via bit_cast (0 = none)
    //     [1] packedIsolineColor      -- RGBA8 uint32 via bit_cast
    //     [2] isolineWidth            -- float (screen-space pixels)
    //     [3] binCount                -- uint32 via bit_cast (0 = continuous)
    //
    //   CustomData[2]:
    //     Reserved for non-address visual constants.
    //     Per-entity attribute pointers and mode selection live in
    //     RHI::GpuEntityConfig (GpuWorld-owned), not in material slots.
    //
    //   CustomData[3]:             -- reserved
    //
    // -----------------------------------------------------------------

    // -----------------------------------------------------------------
    // Typed handles
    // -----------------------------------------------------------------
    struct MaterialTypeTag;
    using MaterialTypeHandle = Core::StrongHandle<MaterialTypeTag>;

    struct MaterialTag;
    using MaterialHandle = Core::StrongHandle<MaterialTag>;

    // -----------------------------------------------------------------
    // MaterialFlags — bitmask stored in GpuMaterialSlot::Flags
    // -----------------------------------------------------------------
    enum class MaterialFlags : std::uint32_t
    {
        None          = 0,
        AlphaMask     = 1 << 0,  // discard fragments below AlphaCutoff
        AlphaBlend    = 1 << 1,  // order-independent transparency
        DoubleSided   = 1 << 2,  // disable backface culling
        Unlit         = 1 << 3,  // skip lighting entirely
        EmissiveOnly  = 1 << 4,  // treat EmissiveID as the colour source
    };

    [[nodiscard]] constexpr MaterialFlags operator|(MaterialFlags a, MaterialFlags b) noexcept
    {
        return static_cast<MaterialFlags>(
            static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }

    [[nodiscard]] constexpr bool HasFlag(MaterialFlags flags, MaterialFlags bit) noexcept
    {
        return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(bit)) != 0;
    }

    // -----------------------------------------------------------------
    // CustomParamDesc
    // -----------------------------------------------------------------
    // Describes one of the four vec4 custom data slots for editor
    // introspection and serialization.  Pure metadata — no GPU impact.
    // -----------------------------------------------------------------
    struct CustomParamDesc
    {
        std::string Name;         // e.g. "TintColor", "UVScale", "WaveAmplitude"
        std::string Description;  // human-readable hint for editor tooltips
        glm::vec4   DefaultValue{0.f};
    };

    // -----------------------------------------------------------------
    // MaterialTypeDesc
    // -----------------------------------------------------------------
    // Registered once per material "kind".  Immutable after registration.
    //
    // Example — a custom water material:
    //
    //   MaterialTypeDesc waterDesc{
    //       .Name     = "Water",
    //       .CustomParams = {
    //           {"WaveAmplitude", "Controls wave height", {0.1f,0,0,0}},
    //           {"WaveFrequency", "Controls wave tiling", {8.f,8.f,0,0}},
    //       },
    //   };
    //   auto waterType = matSys.RegisterType(waterDesc);
    //
    // The shader selects the shading model by reading MaterialTypeID
    // from the SSBO slot and branching accordingly.
    // -----------------------------------------------------------------
    struct MaterialTypeDesc
    {
        std::string  Name;
        // Up to 4 named custom vec4 slots.  Leave empty for standard PBR.
        std::vector<CustomParamDesc> CustomParams;
    };

    // -----------------------------------------------------------------
    // MaterialParams
    // -----------------------------------------------------------------
    // Value bag for setting all material properties on an instance.
    // Texture slots take a BindlessIndex — resolve via:
    //   texMgr.GetBindlessIndex(lease.GetHandle())
    //
    // Unset fields keep their defaults (see GpuMaterialSlot).
    // -----------------------------------------------------------------
    struct MaterialParams
    {
        // Standard PBR
        glm::vec4     BaseColorFactor{1.f, 1.f, 1.f, 1.f};
        float         MetallicFactor   = 0.f;
        float         RoughnessFactor  = 0.5f;
        RHI::BindlessIndex AlbedoID            = RHI::kInvalidBindlessIndex;
        RHI::BindlessIndex NormalID            = RHI::kInvalidBindlessIndex;
        RHI::BindlessIndex MetallicRoughnessID = RHI::kInvalidBindlessIndex;
        RHI::BindlessIndex EmissiveID          = RHI::kInvalidBindlessIndex;
        MaterialFlags Flags = MaterialFlags::None;

        // Custom data — maps directly to GpuMaterialSlot::CustomData[0..3]
        glm::vec4 CustomData[4]{};
    };

    struct MaterialTextureAssetBindings
    {
        Assets::AssetId Albedo{};
        Assets::AssetId Normal{};
        Assets::AssetId MetallicRoughness{};
        Assets::AssetId Emissive{};
    };
}
