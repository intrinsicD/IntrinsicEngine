// Window types of the UI host service: what a feature module or plugin needs to add a window
// and its menu entry without importing the editor (ARCH-022 R2).
module;

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

export module Extrinsic.Runtime.UiHostTypes;

namespace Extrinsic::Runtime {
export struct EditorWindowHandle {
  std::uint64_t Value{0};

  [[nodiscard]] bool IsValid() const noexcept { return Value != 0u; }
  [[nodiscard]] friend bool operator==(EditorWindowHandle,
                                       EditorWindowHandle) noexcept = default;
};

export struct EditorWindowDescriptor {
  std::string Id{};
  std::vector<std::string> MenuPath{};
  std::string Title{};
  bool OpenByDefault{false};
  std::function<void(bool &)> Draw{};
  std::function<void(bool)> OpenStateChanged{};
};

export struct EditorWindowMenuEntry {
  EditorWindowHandle Handle{};
  std::string Id{};
  std::vector<std::string> MenuPath{};
  std::string Title{};
  bool Open{false};
};
} // namespace Extrinsic::Runtime
