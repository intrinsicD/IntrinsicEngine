module;

#include <cstdint>
#include <memory>
#include <optional>

export module Extrinsic.Graphics.SelectionSystem;

export namespace Extrinsic::Graphics
{
	enum class SelectionPrimitiveDomain : std::uint8_t
	{
		None = 0,
		Entity = 1,
		Face = 2,
		Edge = 3,
		Point = 4,
	};

	struct EncodedSelectionId
	{
		static constexpr std::uint32_t DomainShift = 28u;
		static constexpr std::uint32_t DomainMask = 0xFu << DomainShift;
		static constexpr std::uint32_t PayloadMask = ~DomainMask;

		std::uint32_t Value{0u};

		[[nodiscard]] constexpr bool IsHit() const noexcept { return Value != 0u; }
		[[nodiscard]] constexpr SelectionPrimitiveDomain Domain() const noexcept
		{
			return static_cast<SelectionPrimitiveDomain>((Value & DomainMask) >> DomainShift);
		}
		[[nodiscard]] constexpr std::uint32_t Payload() const noexcept { return Value & PayloadMask; }
	};

	[[nodiscard]] constexpr EncodedSelectionId EncodeSelectionId(
		SelectionPrimitiveDomain domain,
		std::uint32_t payload) noexcept
	{
		return EncodedSelectionId{
			.Value = (static_cast<std::uint32_t>(domain) << EncodedSelectionId::DomainShift) |
				(payload & EncodedSelectionId::PayloadMask),
		};
	}

	struct PickRequest
	{
		std::uint32_t PixelX{0};
		std::uint32_t PixelY{0};
	};

	struct PickReadbackResult
	{
		EncodedSelectionId EncodedId{};
		std::uint32_t StableEntityId{0u};
		bool Hit{false};
		// RUNTIME-089 — correlation token copied from the issuing pick's
		// PickPixelRequest::Sequence so the runtime resolves the exact in-flight
		// request. 0 means "uncorrelated".
		std::uint64_t Sequence{0u};
		// BUG-026 — scene-depth sample at the pick pixel, in the depth buffer's
		// native [0 (near) .. 1 (far)] range, plus the request pixel it was
		// sampled at. The runtime unprojects (PixelX, PixelY, Depth) through the
		// issuing frame's inverse view-projection to recover the world-space
		// cursor position. 1.0 (the depth clear) means "no geometry under the
		// cursor"; HasDepth is false when the readback predates the depth copy
		// or the depth target was unavailable.
		bool HasDepth{false};
		float Depth{1.0f};
		std::uint32_t PixelX{0u};
		std::uint32_t PixelY{0u};
	};

	struct SelectionSystemDiagnostics
	{
		std::uint32_t PickRequestCount{0u};
		std::uint32_t PickConsumeCount{0u};
		std::uint32_t PickHitCount{0u};
		std::uint32_t PickNoHitCount{0u};
	};

	struct PointSelectionRequest
	{
		std::uint32_t PixelX{0};
		std::uint32_t PixelY{0};
	};

	struct PointSelectionResult
	{
		std::uint32_t PointID{0};
		std::uint32_t EntityID{0};
	};

	class SelectionSystem
	{
	public:
		SelectionSystem();
		~SelectionSystem();

		SelectionSystem(const SelectionSystem&)            = delete;
		SelectionSystem& operator=(const SelectionSystem&) = delete;

		void Initialize();
		void Shutdown();

		void RequestPointIdPick(PointSelectionRequest request) noexcept;
		[[nodiscard]] bool HasPendingPointIdPick() const noexcept;
		[[nodiscard]] std::optional<PointSelectionRequest> ConsumePointIdPick() noexcept;

		void PublishPointIdResult(PointSelectionResult result) noexcept;
		[[nodiscard]] std::optional<PointSelectionResult> GetLastPointIdResult() const noexcept;
		void ClearLastPointIdResult() noexcept;

		void RequestPick(PickRequest request) noexcept;
		[[nodiscard]] bool HasPendingPick() const noexcept;
		[[nodiscard]] std::optional<PickRequest> ConsumePick() noexcept;

		void PublishPickResult(PickReadbackResult result) noexcept;
		void PublishNoHit() noexcept;
		// Completed readbacks are held in a FIFO queue, not a single slot: the
		// renderer's BeginFrame drain can publish several completed picking
		// slots in one call, and the prior single-holder model silently dropped
		// all but the newest (losing per-pick correlation). PopPickResult drains
		// the oldest queued result so the runtime can apply every readback;
		// GetLastPickResult is a non-destructive peek at the most recent result
		// (used by diagnostics and the legacy point-id wrappers), and
		// ClearLastPickResult discards all queued results.
		[[nodiscard]] std::optional<PickReadbackResult> PopPickResult() noexcept;
		[[nodiscard]] std::optional<PickReadbackResult> GetLastPickResult() const noexcept;
		void ClearLastPickResult() noexcept;
		[[nodiscard]] SelectionSystemDiagnostics GetDiagnostics() const noexcept;

		[[nodiscard]] bool IsInitialized() const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

