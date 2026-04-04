module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.Grid;

import Geometry.Properties;

export namespace Geometry::Grid
{
    // =========================================================================
    // GridDimensions — shared spatial layout for dense and sparse grids
    // =========================================================================
    //
    // Defines a regular 3D grid with NX x NY x NZ cells and
    // (NX+1) x (NY+1) x (NZ+1) vertices. Origin is the minimum corner
    // in world space; Spacing is the cell size along each axis.

    struct GridDimensions
    {
        std::size_t NX{0};
        std::size_t NY{0};
        std::size_t NZ{0};

        glm::vec3 Origin{0.0f};
        glm::vec3 Spacing{1.0f};

        // Total number of grid vertices: (NX+1) * (NY+1) * (NZ+1).
        [[nodiscard]] std::size_t VertexCount() const noexcept
        {
            return (NX + 1) * (NY + 1) * (NZ + 1);
        }

        // Total number of grid cells: NX * NY * NZ.
        [[nodiscard]] std::size_t CellCount() const noexcept
        {
            return NX * NY * NZ;
        }

        // Linearized index for grid vertex (x, y, z).
        // Layout: z * (NY+1) * (NX+1) + y * (NX+1) + x
        [[nodiscard]] std::size_t LinearIndex(std::size_t x, std::size_t y, std::size_t z) const noexcept
        {
            return z * (NY + 1) * (NX + 1) + y * (NX + 1) + x;
        }

        // Recover (x, y, z) grid coordinates from a linear index.
        [[nodiscard]] glm::ivec3 GridCoord(std::size_t linearIndex) const noexcept
        {
            const std::size_t stride_y = NX + 1;
            const std::size_t stride_z = (NX + 1) * (NY + 1);
            const auto z = static_cast<int>(linearIndex / stride_z);
            const std::size_t rem = linearIndex % stride_z;
            const auto y = static_cast<int>(rem / stride_y);
            const auto x = static_cast<int>(rem % stride_y);
            return {x, y, z};
        }

        // World-space position of grid vertex (x, y, z).
        [[nodiscard]] glm::vec3 WorldPosition(std::size_t x, std::size_t y, std::size_t z) const noexcept
        {
            return Origin + Spacing * glm::vec3(
                static_cast<float>(x),
                static_cast<float>(y),
                static_cast<float>(z));
        }

        // World-space center of cell (cx, cy, cz).
        [[nodiscard]] glm::vec3 CellCenter(std::size_t cx, std::size_t cy, std::size_t cz) const noexcept
        {
            return Origin + Spacing * glm::vec3(
                static_cast<float>(cx) + 0.5f,
                static_cast<float>(cy) + 0.5f,
                static_cast<float>(cz) + 0.5f);
        }

        // Check whether integer coordinates are within the vertex grid.
        [[nodiscard]] bool InBounds(int x, int y, int z) const noexcept
        {
            return x >= 0 && y >= 0 && z >= 0
                && static_cast<std::size_t>(x) <= NX
                && static_cast<std::size_t>(y) <= NY
                && static_cast<std::size_t>(z) <= NZ;
        }

        // Check whether dimensions are non-zero.
        [[nodiscard]] bool IsValid() const noexcept
        {
            return NX > 0 && NY > 0 && NZ > 0;
        }

        auto operator<=>(const GridDimensions&) const = default;
    };

    // =========================================================================
    // DenseGrid — PropertySet-backed regular 3D grid
    // =========================================================================
    //
    // Every grid vertex has a slot in the PropertySet. Indices map directly
    // via the linearization formula. This is the general-purpose replacement
    // for a single-purpose scalar grid. Any number
    // of typed properties can be attached (scalar fields, gradients,
    // material IDs, etc.).
    //
    // Consistent with Mesh/Graph/PointCloud which also use PropertySet
    // as their per-element data store.

    class DenseGrid
    {
    public:
        DenseGrid() = default;

        // Construct with given dimensions. Allocates the PropertySet to
        // VertexCount() elements.
        explicit DenseGrid(const GridDimensions& dims)
            : m_Dims(dims)
        {
            m_Cells.Resize(dims.VertexCount());
        }

        // Reinitialize with new dimensions. Clears all existing properties.
        void Reset(const GridDimensions& dims)
        {
            m_Dims = dims;
            m_Cells.Clear();
            m_Cells.Resize(dims.VertexCount());
        }

        // --- Dimensions ---

        [[nodiscard]] const GridDimensions& Dimensions() const noexcept { return m_Dims; }

        [[nodiscard]] std::size_t VertexCount() const noexcept { return m_Dims.VertexCount(); }

        // --- Direct index access (no indirection) ---

        [[nodiscard]] std::size_t VertexIndex(std::size_t x, std::size_t y, std::size_t z) const noexcept
        {
            return m_Dims.LinearIndex(x, y, z);
        }

        // --- Property management ---

        [[nodiscard]] PropertySet& Cells() noexcept { return m_Cells; }
        [[nodiscard]] const PropertySet& Cells() const noexcept { return m_Cells; }

        template <class T>
        [[nodiscard]] Property<T> AddProperty(std::string name, T defaultValue = T{})
        {
            return m_Cells.Add<T>(std::move(name), std::move(defaultValue));
        }

        template <class T>
        [[nodiscard]] Property<T> GetProperty(std::string_view name)
        {
            return m_Cells.Get<T>(name);
        }

        template <class T>
        [[nodiscard]] Property<T> GetProperty(std::string_view name) const
        {
            return m_Cells.Get<T>(name);
        }

        template <class T>
        [[nodiscard]] Property<T> GetOrAddProperty(std::string name, T defaultValue = T{})
        {
            return m_Cells.GetOrAdd<T>(std::move(name), std::move(defaultValue));
        }

        [[nodiscard]] bool HasProperty(std::string_view name) const
        {
            return m_Cells.Exists(name);
        }

        // --- Convenience: scalar field access (common case) ---

        // Get scalar value at vertex (x, y, z) from a named float property.
        [[nodiscard]] float At(const Property<float>& prop, std::size_t x, std::size_t y, std::size_t z) const
        {
            return prop[m_Dims.LinearIndex(x, y, z)];
        }

        // Set scalar value at vertex (x, y, z) on a named float property.
        void Set(Property<float>& prop, std::size_t x, std::size_t y, std::size_t z, float value)
        {
            prop[m_Dims.LinearIndex(x, y, z)] = value;
        }

        // --- World-space queries (forwarded from GridDimensions) ---

        [[nodiscard]] glm::vec3 WorldPosition(std::size_t x, std::size_t y, std::size_t z) const noexcept
        {
            return m_Dims.WorldPosition(x, y, z);
        }

        [[nodiscard]] glm::vec3 CellCenter(std::size_t cx, std::size_t cy, std::size_t cz) const noexcept
        {
            return m_Dims.CellCenter(cx, cy, cz);
        }

        [[nodiscard]] bool InBounds(int x, int y, int z) const noexcept
        {
            return m_Dims.InBounds(x, y, z);
        }

    private:
        GridDimensions m_Dims;
        PropertySet m_Cells;
    };

    // =========================================================================
    // SparseGrid — block-sparse PropertySet-backed 3D grid
    // =========================================================================
    //
    // Divides the grid into fixed-size blocks (default 8^3 = 512 vertices).
    // Only blocks containing active data are allocated. Within each block,
    // vertices are densely packed in a contiguous chunk of the PropertySet.
    //
    // This gives:
    //   - Spatial locality within blocks (stencil ops stay in cache)
    //   - O(1) random access (one hash probe + direct indexing)
    //   - No memory for empty regions
    //   - PropertySet compatibility (contiguous chunks for GPU upload)

    class SparseGrid
    {
    public:
        // Block size is 2^BlockBits per axis.
        static constexpr std::size_t BlockBits = 3;
        static constexpr std::size_t BlockSize = 1u << BlockBits;
        static constexpr std::size_t BlockMask = BlockSize - 1;
        static constexpr std::size_t BlockVolume = BlockSize * BlockSize * BlockSize; // 512

        SparseGrid() = default;

        explicit SparseGrid(const GridDimensions& dims)
            : m_Dims(dims)
        {
        }

        // Reinitialize with new dimensions. Clears all allocated blocks.
        void Reset(const GridDimensions& dims)
        {
            m_Dims = dims;
            m_Cells.Clear();
            m_BlockTable.clear();
            m_FreeBlocks.clear();
            m_AllocatedBlockCount = 0;
        }

        // --- Dimensions ---

        [[nodiscard]] const GridDimensions& Dimensions() const noexcept { return m_Dims; }

        // Number of allocated blocks.
        [[nodiscard]] std::size_t AllocatedBlockCount() const noexcept { return m_AllocatedBlockCount; }

        // Total number of vertex slots in the PropertySet (allocated blocks * BlockVolume).
        [[nodiscard]] std::size_t AllocatedVertexCount() const noexcept { return m_Cells.Size(); }

        // --- Cell access ---

        // Returns the PropertySet index for vertex (x, y, z), allocating the
        // containing block if it doesn't exist yet.
        [[nodiscard]] std::size_t TouchVertex(std::size_t x, std::size_t y, std::size_t z)
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

        // Returns the PropertySet index for vertex (x, y, z), or nullopt if
        // the containing block is not allocated.
        [[nodiscard]] std::optional<std::size_t> VertexIndex(std::size_t x, std::size_t y, std::size_t z) const
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

        // Check if the block containing (x, y, z) is allocated.
        [[nodiscard]] bool IsAllocated(std::size_t x, std::size_t y, std::size_t z) const
        {
            const uint64_t key = PackBlockKey(x >> BlockBits, y >> BlockBits, z >> BlockBits);
            return m_BlockTable.contains(key);
        }

        // --- Block-level operations ---

        // Allocate the block containing (x, y, z) if not already present.
        // Returns the base PropertySet index of the block.
        std::size_t TouchBlock(std::size_t bx, std::size_t by, std::size_t bz)
        {
            const uint64_t key = PackBlockKey(bx, by, bz);
            auto it = m_BlockTable.find(key);
            if (it != m_BlockTable.end())
                return it->second;

            auto base = AllocateBlock();
            m_BlockTable.emplace(key, base);
            return base;
        }

        // --- Iteration ---

        // Call fn(blockX, blockY, blockZ, basePropertySetIndex) for each allocated block.
        template <class F>
        void ForEachBlock(F&& fn) const
        {
            for (const auto& [key, base] : m_BlockTable)
            {
                auto [bx, by, bz] = UnpackBlockKey(key);
                fn(bx, by, bz, base);
            }
        }

        // Call fn(x, y, z, propertySetIndex) for every vertex in every allocated block.
        // Note: this includes vertices at the block boundary that may exceed
        // the grid's logical dimensions.
        template <class F>
        void ForEachAllocatedVertex(F&& fn) const
        {
            for (const auto& [key, base] : m_BlockTable)
            {
                auto [bx, by, bz] = UnpackBlockKey(key);
                const std::size_t ox = bx * BlockSize;
                const std::size_t oy = by * BlockSize;
                const std::size_t oz = bz * BlockSize;

                for (std::size_t lz = 0; lz < BlockSize; ++lz)
                    for (std::size_t ly = 0; ly < BlockSize; ++ly)
                        for (std::size_t lx = 0; lx < BlockSize; ++lx)
                        {
                            const std::size_t gx = ox + lx;
                            const std::size_t gy = oy + ly;
                            const std::size_t gz = oz + lz;
                            if (m_Dims.InBounds(
                                    static_cast<int>(gx),
                                    static_cast<int>(gy),
                                    static_cast<int>(gz)))
                            {
                                fn(gx, gy, gz, base + LocalIndex(lx, ly, lz));
                            }
                        }
            }
        }

        // --- Property management ---

        [[nodiscard]] PropertySet& Cells() noexcept { return m_Cells; }
        [[nodiscard]] const PropertySet& Cells() const noexcept { return m_Cells; }

        template <class T>
        [[nodiscard]] Property<T> AddProperty(std::string name, T defaultValue = T{})
        {
            return m_Cells.Add<T>(std::move(name), std::move(defaultValue));
        }

        template <class T>
        [[nodiscard]] Property<T> GetProperty(std::string_view name)
        {
            return m_Cells.Get<T>(name);
        }

        template <class T>
        [[nodiscard]] Property<T> GetProperty(std::string_view name) const
        {
            return m_Cells.Get<T>(name);
        }

        template <class T>
        [[nodiscard]] Property<T> GetOrAddProperty(std::string name, T defaultValue = T{})
        {
            return m_Cells.GetOrAdd<T>(std::move(name), std::move(defaultValue));
        }

        [[nodiscard]] bool HasProperty(std::string_view name) const
        {
            return m_Cells.Exists(name);
        }

        // --- World-space queries (forwarded) ---

        [[nodiscard]] glm::vec3 WorldPosition(std::size_t x, std::size_t y, std::size_t z) const noexcept
        {
            return m_Dims.WorldPosition(x, y, z);
        }

        [[nodiscard]] bool InBounds(int x, int y, int z) const noexcept
        {
            return m_Dims.InBounds(x, y, z);
        }

    private:
        // Pack block coordinates into a single 64-bit key.
        // 21 bits each for bx, by, bz — supports grids up to 2^24 vertices per axis.
        [[nodiscard]] static uint64_t PackBlockKey(std::size_t bx, std::size_t by, std::size_t bz) noexcept
        {
            return (static_cast<uint64_t>(bz) << 42)
                 | (static_cast<uint64_t>(by) << 21)
                 | static_cast<uint64_t>(bx);
        }

        struct BlockCoord { std::size_t bx, by, bz; };

        [[nodiscard]] static BlockCoord UnpackBlockKey(uint64_t key) noexcept
        {
            constexpr uint64_t mask21 = (1ULL << 21) - 1;
            return {
                static_cast<std::size_t>(key & mask21),
                static_cast<std::size_t>((key >> 21) & mask21),
                static_cast<std::size_t>((key >> 42) & mask21)
            };
        }

        // Local linear index within a block.
        [[nodiscard]] static std::size_t LocalIndex(std::size_t lx, std::size_t ly, std::size_t lz) noexcept
        {
            return lz * BlockSize * BlockSize + ly * BlockSize + lx;
        }

        // Allocate a new block, reusing freed slots if available.
        [[nodiscard]] std::size_t AllocateBlock()
        {
            if (!m_FreeBlocks.empty())
            {
                auto base = m_FreeBlocks.back();
                m_FreeBlocks.pop_back();
                ++m_AllocatedBlockCount;
                return base;
            }

            // Grow the PropertySet by one block.
            const std::size_t base = m_Cells.Size();
            for (std::size_t i = 0; i < BlockVolume; ++i)
                m_Cells.PushBack();
            ++m_AllocatedBlockCount;
            return base;
        }

        GridDimensions m_Dims;
        PropertySet m_Cells;
        std::unordered_map<uint64_t, std::size_t> m_BlockTable;
        std::vector<std::size_t> m_FreeBlocks;
        std::size_t m_AllocatedBlockCount{0};
    };

} // namespace Geometry::Grid
