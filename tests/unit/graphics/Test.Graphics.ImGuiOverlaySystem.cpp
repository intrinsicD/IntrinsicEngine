#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.RHI.Bindless;

using namespace Extrinsic;

TEST(GraphicsImGuiOverlaySystem, SubmitFrameAggregatesValidDrawData)
{
    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();

    overlay.SubmitFrame(Graphics::ImGuiOverlayFrame{
        .Enabled = true,
        .DisplayWidth = 1280u,
        .DisplayHeight = 720u,
        .DrawLists = {
            Graphics::ImGuiOverlayDrawList{.CommandCount = 2u, .VertexCount = 20u, .IndexCount = 30u},
            Graphics::ImGuiOverlayDrawList{
                .CommandCount = 1u,
                .VertexCount = 8u,
                .IndexCount = 12u,
                .UsesUserTexture = true,
                .Commands = {
                    Graphics::ImGuiOverlayDrawCommand{
                        .IndexOffset = 0u,
                        .VertexOffset = 0u,
                        .IndexCount = 12u,
                        .TextureBindlessIndex = 42u,
                        .UsesUserTexture = true,
                    },
                },
            },
            Graphics::ImGuiOverlayDrawList{.CommandCount = 0u, .VertexCount = 8u, .IndexCount = 12u},
        },
    });

    EXPECT_TRUE(overlay.HasOverlayWork());
    const Graphics::ImGuiOverlayDiagnostics diagnostics = overlay.GetDiagnostics();
    EXPECT_EQ(diagnostics.SubmittedDrawListCount, 3u);
    EXPECT_EQ(diagnostics.AcceptedDrawListCount, 2u);
    EXPECT_EQ(diagnostics.RejectedDrawListCount, 1u);
    EXPECT_EQ(diagnostics.DrawCommandCount, 3u);
    EXPECT_EQ(diagnostics.VertexCount, 28u);
    EXPECT_EQ(diagnostics.IndexCount, 42u);
    EXPECT_TRUE(diagnostics.HasUserTextures);

    const Graphics::ImGuiOverlayPushConstants pc = overlay.BuildPushConstants();
    EXPECT_EQ(pc.IndexCount, 42u);
    EXPECT_EQ(pc.TextureBindlessIndex, Extrinsic::RHI::kInvalidBindlessIndex);
    EXPECT_EQ(pc.Flags, 0u);
    const Graphics::ImGuiOverlayPushConstants userPc =
        overlay.BuildPushConstants(0u, 0u, 12u, 42u, Graphics::kImGuiOverlayPushFlagUserTexture);
    EXPECT_EQ(userPc.IndexCount, 12u);
    EXPECT_EQ(userPc.TextureBindlessIndex, 42u);
    EXPECT_EQ(userPc.Flags & Graphics::kImGuiOverlayPushFlagUserTexture,
              Graphics::kImGuiOverlayPushFlagUserTexture);
    EXPECT_FLOAT_EQ(pc.Scale[0], 2.0f / 1280.0f);
    EXPECT_FLOAT_EQ(pc.Scale[1], 2.0f / 720.0f);
    EXPECT_FLOAT_EQ(pc.Translate[0], -1.0f);
    EXPECT_FLOAT_EQ(pc.Translate[1], -1.0f);
}

TEST(GraphicsImGuiOverlaySystem, DisabledOrInvalidFramesHaveNoOverlayWork)
{
    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();

    overlay.SubmitFrame(Graphics::ImGuiOverlayFrame{
        .Enabled = false,
        .DisplayWidth = 1280u,
        .DisplayHeight = 720u,
        .DrawLists = {Graphics::ImGuiOverlayDrawList{.CommandCount = 1u, .VertexCount = 3u, .IndexCount = 3u}},
    });
    EXPECT_FALSE(overlay.HasOverlayWork());
    EXPECT_FALSE(overlay.GetDiagnostics().InvalidDisplaySize);

    overlay.SubmitFrame(Graphics::ImGuiOverlayFrame{
        .Enabled = true,
        .DisplayWidth = 0u,
        .DisplayHeight = 720u,
        .DrawLists = {Graphics::ImGuiOverlayDrawList{.CommandCount = 1u, .VertexCount = 3u, .IndexCount = 3u}},
    });
    EXPECT_FALSE(overlay.HasOverlayWork());
    EXPECT_TRUE(overlay.GetDiagnostics().InvalidDisplaySize);

    overlay.ClearFrame();
    EXPECT_FALSE(overlay.HasOverlayWork());
    EXPECT_EQ(overlay.GetDiagnostics().DrawCommandCount, 0u);
}

TEST(GraphicsImGuiOverlaySystem, UnchangedFontAtlasPayloadIsRetained)
{
    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();

    Graphics::ImGuiOverlayFrame first{};
    first.Enabled = true;
    first.DisplayWidth = 64u;
    first.DisplayHeight = 64u;
    first.FontAtlas = Graphics::ImGuiOverlayFontAtlas{
        .Valid = true,
        .Width = 2u,
        .Height = 2u,
        .BytesPerPixel = 1u,
        .UseColors = false,
        .Dirty = true,
        .Revision = 1u,
        .Pixels = {
            std::byte{0x10},
            std::byte{0x20},
            std::byte{0x30},
            std::byte{0x40},
        },
    };
    const std::vector<std::byte> expectedPixels = first.FontAtlas.Pixels;
    overlay.SubmitFrame(std::move(first));
    EXPECT_TRUE(overlay.GetDiagnostics().FontAtlasAvailable);
    EXPECT_FALSE(overlay.GetDiagnostics().FontAtlasRetained);

    Graphics::ImGuiOverlayFrame second{};
    second.Enabled = true;
    second.DisplayWidth = 64u;
    second.DisplayHeight = 64u;
    second.FontAtlas = Graphics::ImGuiOverlayFontAtlas{
        .Valid = true,
        .Width = 2u,
        .Height = 2u,
        .BytesPerPixel = 1u,
        .UseColors = false,
        .Dirty = false,
        .Revision = 1u,
    };
    overlay.SubmitFrame(std::move(second));

    const Graphics::ImGuiOverlayDiagnostics diagnostics =
        overlay.GetDiagnostics();
    EXPECT_TRUE(diagnostics.FontAtlasAvailable);
    EXPECT_TRUE(diagnostics.FontAtlasRetained);
    EXPECT_EQ(diagnostics.FontAtlasRetainCount, 1u);
    EXPECT_EQ(diagnostics.FontAtlasRevision, 1u);

    const Graphics::ImGuiOverlayFrame* retained = overlay.GetCurrentFrame();
    ASSERT_NE(retained, nullptr);
    EXPECT_EQ(retained->FontAtlas.Pixels, expectedPixels);
}

TEST(GraphicsImGuiOverlaySystem, DiagnosticsFormattingIsDeterministic)
{
    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();
    overlay.SubmitFrame(Graphics::ImGuiOverlayFrame{
        .Enabled = true,
        .DisplayWidth = 32u,
        .DisplayHeight = 32u,
        .DrawLists = {Graphics::ImGuiOverlayDrawList{.CommandCount = 1u, .VertexCount = 4u, .IndexCount = 6u}},
    });

    EXPECT_EQ(overlay.FormatDiagnostics(),
              "imgui-overlay: enabled=true accepted_lists=1 rejected_lists=0 commands=1 vertices=4 indices=6 draws=0 invalid_display=false user_textures=false font_atlas=false font_atlas_gpu=false font_atlas_uploads=0");
}

TEST(GraphicsImGuiOverlaySystem, PresentFinalizationDiagnosticsRequireSourceBackbufferAndPass)
{
    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();

    Graphics::PresentFinalizationDiagnostics ok = overlay.ValidatePresentFinalization(Graphics::PresentFinalizationInputs{
        .PresentationSourceAvailable = true,
        .BackbufferImported = true,
        .PresentPassEnabled = true,
    });
    EXPECT_TRUE(ok.CanFinalize);

    Graphics::PresentFinalizationDiagnostics missing = overlay.ValidatePresentFinalization(Graphics::PresentFinalizationInputs{
        .PresentationSourceAvailable = false,
        .BackbufferImported = false,
        .PresentPassEnabled = false,
    });
    EXPECT_FALSE(missing.CanFinalize);
    EXPECT_TRUE(missing.MissingPresentationSource);
    EXPECT_TRUE(missing.MissingBackbuffer);
    EXPECT_TRUE(missing.PresentPassDisabled);
}
