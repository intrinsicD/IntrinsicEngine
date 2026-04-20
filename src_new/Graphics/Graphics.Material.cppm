module;

#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

export module Extrinsic.Graphics.Material;

import Extrinsic.Core.StrongHandle;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Types;

// ============================================================
// Graphics.Material — material type system data types.
//
// These are the CPU-side representations.  The GPU layout is
// GpuMaterialSlot (RHI.Types), a 128-byte std430 struct with
// 64 bytes of standard PBR fields and 64 bytes of custom data.
//
// Separation of concerns:
//   Graphics.Material      — descriptors, params, handles (this file)
//   Graphics.MaterialSystem — the manager, GPU buffer, sync
// ============================================================

export namespace Extrinsic::Graphics
{
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
}

