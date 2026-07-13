module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.EditorWindowRegistry;

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

struct EditorWindowRecord {
  EditorWindowHandle Handle{};
  EditorWindowDescriptor Descriptor{};
  bool Open{false};
};

export class EditorWindowRegistry final {
public:
  [[nodiscard]] EditorWindowHandle Register(EditorWindowDescriptor descriptor);
  [[nodiscard]] bool Unregister(EditorWindowHandle handle);

  [[nodiscard]] bool SetOpen(EditorWindowHandle handle, bool open);
  [[nodiscard]] bool SetOpen(std::string_view id, bool open);
  [[nodiscard]] bool IsOpen(EditorWindowHandle handle) const noexcept;

  void SetVisible(bool visible) noexcept { m_Visible = visible; }
  void ToggleVisible() noexcept { m_Visible = !m_Visible; }
  [[nodiscard]] bool IsVisible() const noexcept { return m_Visible; }

  [[nodiscard]] std::vector<EditorWindowMenuEntry> BuildMenuModel() const;

  // Invokes only callbacks whose windows are open while the registry is
  // globally visible. The return value is the number of callbacks run.
  [[nodiscard]] std::size_t DrawOpenWindows();

  [[nodiscard]] std::size_t Size() const noexcept { return m_Windows.size(); }

private:
  [[nodiscard]] EditorWindowRecord *Find(EditorWindowHandle handle) noexcept;
  [[nodiscard]] const EditorWindowRecord *
  Find(EditorWindowHandle handle) const noexcept;
  [[nodiscard]] EditorWindowRecord *Find(std::string_view id) noexcept;
  void NotifyOpenStateChanged(EditorWindowRecord &record);

  std::vector<EditorWindowRecord> m_Windows{};
  std::uint64_t m_NextHandle{1u};
  bool m_Visible{true};
};
} // namespace Extrinsic::Runtime
