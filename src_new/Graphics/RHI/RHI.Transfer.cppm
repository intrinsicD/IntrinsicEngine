module;

#include <cstdint>
#include <compare>

export module Extrinsic.RHI.Transfer;

// ============================================================
// RHI.Transfer — API-agnostic async GPU upload abstraction.
//
// Callers submit CPU data for GPU upload and receive a
// TransferToken.  The token is polled / waited on to determine
// when the GPU has finished consuming the staging data and the
// destination resource is ready for use in shaders.
//
// Invariant (matches src/ async-upload guarantee):
//   No caller thread ever blocks on GPU fence completion inside
//   UploadBuffer / UploadTexture.  The fence wait happens on
//   CollectCompletedTransfers(), which is called once per frame
//   on the main thread (or render thread) after submission.
// ============================================================

export namespace Extrinsic::RHI
{
    // ----------------------------------------------------------
    // TransferToken — lightweight, copyable completion token.
    // Value == 0 is always invalid (no upload).
    // Values are monotonically increasing within a device's lifetime.
    // ----------------------------------------------------------
    struct TransferToken
    {
        std::uint64_t Value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }

        auto operator<=>(const TransferToken&) const = default;
    };
}

