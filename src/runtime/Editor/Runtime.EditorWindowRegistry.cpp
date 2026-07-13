module;

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Runtime.EditorWindowRegistry;

namespace Extrinsic::Runtime {
namespace {
[[nodiscard]] bool
IsValidDescriptor(const EditorWindowDescriptor &descriptor) noexcept {
  if (descriptor.Id.empty() || descriptor.Title.empty() ||
      descriptor.MenuPath.empty() || !descriptor.Draw) {
    return false;
  }

  return std::ranges::none_of(
      descriptor.MenuPath,
      [](const std::string &segment) { return segment.empty(); });
}
} // namespace

EditorWindowHandle
EditorWindowRegistry::Register(EditorWindowDescriptor descriptor) {
  if (!IsValidDescriptor(descriptor) || Find(descriptor.Id) != nullptr)
    return {};

  EditorWindowRecord record{
      .Handle = EditorWindowHandle{m_NextHandle++},
      .Descriptor = std::move(descriptor),
  };
  record.Open = record.Descriptor.OpenByDefault;
  m_Windows.push_back(std::move(record));
  return m_Windows.back().Handle;
}

bool EditorWindowRegistry::Unregister(const EditorWindowHandle handle) {
  const auto found =
      std::ranges::find(m_Windows, handle, &EditorWindowRecord::Handle);
  if (found == m_Windows.end())
    return false;

  const bool wasOpen = found->Open;
  std::function<void(bool)> stateChanged =
      std::move(found->Descriptor.OpenStateChanged);
  m_Windows.erase(found);
  if (wasOpen && stateChanged)
    stateChanged(false);
  return true;
}

bool EditorWindowRegistry::SetOpen(const EditorWindowHandle handle,
                                   const bool open) {
  EditorWindowRecord *record = Find(handle);
  if (record == nullptr)
    return false;
  if (record->Open == open)
    return true;

  record->Open = open;
  NotifyOpenStateChanged(*record);
  return true;
}

bool EditorWindowRegistry::SetOpen(const std::string_view id, const bool open) {
  EditorWindowRecord *record = Find(id);
  if (record == nullptr)
    return false;
  return SetOpen(record->Handle, open);
}

bool EditorWindowRegistry::IsOpen(
    const EditorWindowHandle handle) const noexcept {
  const EditorWindowRecord *record = Find(handle);
  return record != nullptr && record->Open;
}

std::vector<EditorWindowMenuEntry>
EditorWindowRegistry::BuildMenuModel() const {
  std::vector<EditorWindowMenuEntry> model;
  model.reserve(m_Windows.size());
  for (const EditorWindowRecord &record : m_Windows) {
    model.push_back(EditorWindowMenuEntry{
        .Handle = record.Handle,
        .Id = record.Descriptor.Id,
        .MenuPath = record.Descriptor.MenuPath,
        .Title = record.Descriptor.Title,
        .Open = record.Open,
    });
  }
  return model;
}

std::size_t EditorWindowRegistry::DrawOpenWindows() {
  if (!m_Visible)
    return 0u;

  std::vector<EditorWindowHandle> openHandles;
  openHandles.reserve(m_Windows.size());
  for (const EditorWindowRecord &record : m_Windows) {
    if (record.Open)
      openHandles.push_back(record.Handle);
  }

  std::size_t drawCount = 0u;
  for (const EditorWindowHandle handle : openHandles) {
    EditorWindowRecord *record = Find(handle);
    if (record == nullptr || !record->Open)
      continue;

    std::function<void(bool &)> draw = record->Descriptor.Draw;
    bool requestedOpen = true;
    draw(requestedOpen);
    ++drawCount;
    record = Find(handle);
    if (record != nullptr && requestedOpen != record->Open)
      (void)SetOpen(handle, requestedOpen);
  }
  return drawCount;
}

EditorWindowRecord *
EditorWindowRegistry::Find(const EditorWindowHandle handle) noexcept {
  const auto found =
      std::ranges::find(m_Windows, handle, &EditorWindowRecord::Handle);
  return found == m_Windows.end() ? nullptr : &*found;
}

const EditorWindowRecord *
EditorWindowRegistry::Find(const EditorWindowHandle handle) const noexcept {
  const auto found =
      std::ranges::find(m_Windows, handle, &EditorWindowRecord::Handle);
  return found == m_Windows.end() ? nullptr : &*found;
}

EditorWindowRecord *
EditorWindowRegistry::Find(const std::string_view id) noexcept {
  const auto found = std::ranges::find(
      m_Windows, id, [](const EditorWindowRecord &record) -> std::string_view {
        return record.Descriptor.Id;
      });
  return found == m_Windows.end() ? nullptr : &*found;
}

void EditorWindowRegistry::NotifyOpenStateChanged(EditorWindowRecord &record) {
  if (record.Descriptor.OpenStateChanged)
    record.Descriptor.OpenStateChanged(record.Open);
}
} // namespace Extrinsic::Runtime
