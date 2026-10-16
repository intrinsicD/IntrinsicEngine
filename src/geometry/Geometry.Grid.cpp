module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

module Geometry.Grid;

namespace Geometry::Grid
{
    DenseGrid::DenseGrid(const GridDimensions& dims)
        : m_Dims(dims)
    {
        m_Cells.Resize(dims.VertexCount());
    }

    void DenseGrid::Reset(const GridDimensions& dims)
    {
        m_Dims = dims;
        m_Cells.Clear();
        m_Cells.Resize(dims.VertexCount());
    }

    bool DenseGrid::HasProperty(std::string_view name) const
    {
        return m_Cells.Exists(name);
    }

    float DenseGrid::At(const Property<float>& prop, std::size_t x, std::size_t y, std::size_t z) const
    {
        return prop[m_Dims.LinearIndex(x, y, z)];
    }

    void DenseGrid::Set(Property<float>& prop, std::size_t x, std::size_t y, std::size_t z, float value)
    {
        prop[m_Dims.LinearIndex(x, y, z)] = value;
    }

    SparseGrid::SparseGrid(const GridDimensions& dims)
    {
        static_cast<void>(Reset(dims));
    }

    bool SparseGrid::Reset(const GridDimensions& dims)
    {
        constexpr std::size_t coordinateLimit = std::size_t{1} << 24;
        if (!dims.IsValid() || dims.NX >= coordinateLimit || dims.NY >= coordinateLimit ||
            dims.NZ >= coordinateLimit) return false;
        m_Dims = dims;
        m_Cells.Clear();
        m_BlockTable.clear();
        m_AllocatedBlockCount = 0;
        return true;
    }

    bool SparseGrid::ContainsVertex(std::size_t x, std::size_t y, std::size_t z) const noexcept
    {
        return m_Dims.IsValid() && x <= m_Dims.NX && y <= m_Dims.NY && z <= m_Dims.NZ;
    }

    bool SparseGrid::ContainsBlock(std::size_t x, std::size_t y, std::size_t z) const noexcept
    {
        return m_Dims.IsValid() && x <= (m_Dims.NX >> BlockBits) &&
            y <= (m_Dims.NY >> BlockBits) && z <= (m_Dims.NZ >> BlockBits);
    }

    std::optional<std::size_t> SparseGrid::TouchVertex(std::size_t x, std::size_t y, std::size_t z)
    {
        if (!ContainsVertex(x, y, z)) return std::nullopt;
        const auto bx = x >> BlockBits;
        const auto by = y >> BlockBits;
        const auto bz = z >> BlockBits;
        const uint64_t key = PackBlockKey(bx, by, bz);

        auto it = m_BlockTable.find(key);
        if (it == m_BlockTable.end())
        {
            auto base = AllocateBlock();
            it = m_BlockTable.emplace(key, base).first;
        }

        return it->second + LocalIndex(x & BlockMask, y & BlockMask, z & BlockMask);
    }

    std::optional<std::size_t> SparseGrid::VertexIndex(std::size_t x, std::size_t y, std::size_t z) const
    {
        if (!ContainsVertex(x, y, z)) return std::nullopt;
        const auto bx = x >> BlockBits;
        const auto by = y >> BlockBits;
        const auto bz = z >> BlockBits;
        const uint64_t key = PackBlockKey(bx, by, bz);

        auto it = m_BlockTable.find(key);
        if (it == m_BlockTable.end())
            return std::nullopt;

        return it->second + LocalIndex(x & BlockMask, y & BlockMask, z & BlockMask);
    }

    bool SparseGrid::IsAllocated(std::size_t x, std::size_t y, std::size_t z) const
    {
        if (!ContainsVertex(x, y, z)) return false;
        const uint64_t key = PackBlockKey(x >> BlockBits, y >> BlockBits, z >> BlockBits);
        return m_BlockTable.contains(key);
    }

    std::optional<std::size_t> SparseGrid::TouchBlock(std::size_t bx, std::size_t by, std::size_t bz)
    {
        if (!ContainsBlock(bx, by, bz)) return std::nullopt;
        const uint64_t key = PackBlockKey(bx, by, bz);
        auto it = m_BlockTable.find(key);
        if (it != m_BlockTable.end())
            return it->second;

        auto base = AllocateBlock();
        m_BlockTable.emplace(key, base);
        return base;
    }

    std::size_t SparseGrid::AllocateBlock()
    {
        const std::size_t base = m_Cells.Size();
        for (std::size_t i = 0; i < BlockVolume; ++i)
            m_Cells.PushBack();
        ++m_AllocatedBlockCount;
        return base;
    }
}
