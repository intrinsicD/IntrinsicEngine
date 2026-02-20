#pragma once

// =============================================================================
// ExportUtils — shared serialization helpers for asset exporters.
//
// This header is intended for inclusion in exporter .cpp global module
// fragments only — it is not part of any exported module interface.
// =============================================================================

// (Intentionally no standard-library includes here; they must live in the
// including translation unit's global module fragment.)

namespace Graphics::ExportUtils
{
    inline void AppendString(std::vector<std::byte>& out, const std::string& s)
    {
        const auto* ptr = reinterpret_cast<const std::byte*>(s.data());
        out.insert(out.end(), ptr, ptr + s.size());
    }

    inline void AppendBytes(std::vector<std::byte>& out, const void* data, std::size_t size)
    {
        const auto* ptr = reinterpret_cast<const std::byte*>(data);
        out.insert(out.end(), ptr, ptr + size);
    }

    template <typename T>
    void AppendValue(std::vector<std::byte>& out, T value)
    {
        AppendBytes(out, &value, sizeof(T));
    }
}
