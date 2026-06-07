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
        : m_Dims(dims)
    {
    }

    void SparseGrid::Reset(const GridDimensions& dims)
    {
        m_Dims = dims;
        m_Cells.Clear();
        m_BlockTable.clear();
        m_FreeBlocks.clear();
        m_AllocatedBlockCount = 0;
    }

    std::size_t SparseGrid::TouchVertex(std::size_t x, std::size_t y, std::size_t z)
    {
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
        const uint64_t key = PackBlockKey(x >> BlockBits, y >> BlockBits, z >> BlockBits);
        return m_BlockTable.contains(key);
    }

    std::size_t SparseGrid::TouchBlock(std::size_t bx, std::size_t by, std::size_t bz)
    {
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
        if (!m_FreeBlocks.empty())
        {
            auto base = m_FreeBlocks.back();
            m_FreeBlocks.pop_back();
            ++m_AllocatedBlockCount;
            return base;
        }

        const std::size_t base = m_Cells.Size();
        for (std::size_t i = 0; i < BlockVolume; ++i)
            m_Cells.PushBack();
        ++m_AllocatedBlockCount;
        return base;
    }
}
