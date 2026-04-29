module;

#include <cstdint>
#include <memory>
#include <optional>

export module Extrinsic.Graphics.SelectionSystem;

export namespace Extrinsic::Graphics
{
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

		[[nodiscard]] bool IsInitialized() const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}

