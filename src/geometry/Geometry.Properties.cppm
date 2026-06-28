module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <limits>

#include <glm/glm.hpp>

export module Geometry.Properties;

import Geometry.Linalg;

export namespace Geometry
{
    /// Stable identifier for a property storage inside a registry.
    using PropertyId = size_t;

    class PropertyRegistry;

    namespace Internal
    {
        /// Type tag used for runtime type checks across erased property storages.
        using TypeID = std::uintptr_t;

        template <typename T>
        struct TypeInfo
        {
            /// Returns a per-type unique identifier without RTTI.
            static TypeID ID()
            {
                // The address of this static variable is unique per type T
                static char s_ID;
                return reinterpret_cast<std::uintptr_t>(&s_ID);
            }
        };
    } // namespace Internal

    enum class PropertyValueKind : std::uint8_t
    {
        Unknown,
        Bool,
        Int32,
        UInt32,
        UInt64,
        Float,
        Double,
        Vec2,
        Vec3,
        Vec4
    };

    struct PropertyDescriptor
    {
        PropertyId Id{static_cast<PropertyId>(-1)};
        std::string Name{};
        Internal::TypeID Type{0};
        PropertyValueKind ValueKind{PropertyValueKind::Unknown};
        std::size_t ElementCount{0};
        bool Mutable{false};
        bool SupportsContiguousSpan{false};
        bool SupportsRawData{false};
    };

    namespace Internal
    {
        template <class T>
        [[nodiscard]] constexpr PropertyValueKind ValueKindOf() noexcept
        {
            if constexpr (std::is_same_v<T, bool>) return PropertyValueKind::Bool;
            else if constexpr (std::is_same_v<T, std::int32_t>) return PropertyValueKind::Int32;
            else if constexpr (std::is_same_v<T, std::uint32_t>) return PropertyValueKind::UInt32;
            else if constexpr (std::is_same_v<T, std::uint64_t>) return PropertyValueKind::UInt64;
            else if constexpr (std::is_same_v<T, float>) return PropertyValueKind::Float;
            else if constexpr (std::is_same_v<T, double>) return PropertyValueKind::Double;
            else if constexpr (std::is_same_v<T, glm::vec2>) return PropertyValueKind::Vec2;
            else if constexpr (std::is_same_v<T, glm::vec3>) return PropertyValueKind::Vec3;
            else if constexpr (std::is_same_v<T, glm::vec4>) return PropertyValueKind::Vec4;
            else return PropertyValueKind::Unknown;
        }

        /// Erased storage interface for property arrays.
        class PropertyStorageBase
        {
        public:
            PropertyStorageBase() = default;
            virtual ~PropertyStorageBase() = default;

            PropertyStorageBase(const PropertyStorageBase&) = delete;
            PropertyStorageBase& operator=(const PropertyStorageBase&) = delete;

            /// Clones the concrete storage and its data.
            [[nodiscard]] virtual std::unique_ptr<PropertyStorageBase> Clone() const = 0;

            /// Property name used for lookup and debugging.
            [[nodiscard]] virtual std::string_view Name() const = 0;
            /// Reserve capacity for n elements.
            virtual void Reserve(size_t n) = 0;
            /// Resize to n elements, filling with the default value.
            virtual void Resize(size_t n) = 0;
            /// Reduce capacity to fit current size.
            virtual void ShrinkToFit() = 0;
            /// Append one element initialized to the default value.
            virtual void PushBack() = 0;
            /// Swap two elements in the storage.
            virtual void Swap(size_t i0, size_t i1) = 0;

            /// Returns the erased runtime type id for this storage.
            [[nodiscard]] virtual TypeID Type() const noexcept = 0;
            /// Returns descriptor metadata that does not expose typed storage.
            [[nodiscard]] virtual PropertyDescriptor Describe(PropertyId id, bool isMutable) const = 0;
        };

        template <class T>
        class PropertyStorage final : public PropertyStorageBase
        {
        public:
            /// Creates typed storage with a name and a default element value.
            PropertyStorage(std::string name, T defaultValue)
                : PropertyStorageBase(), m_Name(std::move(name)), m_Data(), m_Default(std::move(defaultValue))
            {
            }

            PropertyStorage(const PropertyStorage& other)
                : PropertyStorageBase(), m_Name(other.m_Name), m_Data(other.m_Data), m_Default(other.m_Default)
            {
            }

            PropertyStorage& operator=(const PropertyStorage& other)
            {
                if (this != &other)
                {
                    m_Name = other.m_Name;
                    m_Data = other.m_Data;
                    m_Default = other.m_Default;
                }
                return *this;
            }

            [[nodiscard]] std::unique_ptr<PropertyStorageBase> Clone() const override
            {
                return std::make_unique<PropertyStorage<T>>(*this);
            }

            [[nodiscard]] std::string_view Name() const override { return m_Name; }
            void Reserve(size_t n) override { m_Data.reserve(n); }
            void Resize(size_t n) override { m_Data.resize(n, m_Default); }
            void ShrinkToFit() override { m_Data.shrink_to_fit(); }
            void PushBack() override { m_Data.push_back(m_Default); }

            void Swap(size_t i0, size_t i1) override
            {
                using std::swap;
                swap(m_Data[i0], m_Data[i1]);
            }

            [[nodiscard]] TypeID Type() const noexcept override
            {
                return TypeInfo<T>::ID();
            }

            [[nodiscard]] PropertyDescriptor Describe(PropertyId id, bool isMutable) const override
            {
                return PropertyDescriptor{
                    .Id = id,
                    .Name = m_Name,
                    .Type = TypeInfo<T>::ID(),
                    .ValueKind = ValueKindOf<T>(),
                    .ElementCount = m_Data.size(),
                    .Mutable = isMutable,
                    .SupportsContiguousSpan = !std::is_same_v<T, bool>,
                    .SupportsRawData = !std::is_same_v<T, bool>};
            }

            [[nodiscard]] std::vector<T>& Data() noexcept { return m_Data; }
            [[nodiscard]] const std::vector<T>& Data() const noexcept { return m_Data; }
            [[nodiscard]] const T& DefaultValue() const noexcept { return m_Default; }

        private:
            std::string m_Name;
            std::vector<T> m_Data;
            T m_Default;
        };
    } // namespace Internal

    template <class T>
    class PropertyBuffer;

    template <class T>
    class ConstPropertyBuffer;

    /// Registry of named, typed property arrays sharing a common element count.
    class PropertyRegistry
    {
    public:
        PropertyRegistry() = default;
        ~PropertyRegistry() = default;

        PropertyRegistry(const PropertyRegistry& other);
        PropertyRegistry(PropertyRegistry&&) noexcept = default;
        PropertyRegistry& operator=(const PropertyRegistry& other);
        PropertyRegistry& operator=(PropertyRegistry&&) noexcept = default;

        /// Number of elements in each property array.
        [[nodiscard]] size_t Size() const noexcept { return m_Size; }
        /// Count of property storages (including different types).
        [[nodiscard]] size_t PropertyCount() const noexcept { return m_Storages.size(); }

        /// Returns the names of all properties in insertion order.
        [[nodiscard]] std::vector<std::string> PropertyNames() const;
        /// Returns erased descriptors for all live properties in insertion order.
        [[nodiscard]] std::vector<PropertyDescriptor> Descriptors(bool isMutable) const;

        /// Clears all properties and their data.
        void Clear();
        /// Reserves storage slots for property arrays.
        void Reserve(size_t n);

        /// Resizes all properties to n elements, filling with defaults.
        void Resize(size_t n);

        /// Shrinks all property storages to fit.
        void ShrinkToFit();
        /// Appends one element to each property storage.
        void PushBack();

        /// Swaps element i0 and i1 across all property arrays.
        void Swap(size_t i0, size_t i1);

        /// Returns true if a property with this name exists.
        [[nodiscard]] bool Contains(std::string_view name) const;

        /// Finds a property by name; returns nullopt if not found.
        /// O(1) average via internal hash map.
        [[nodiscard]] std::optional<PropertyId> Find(std::string_view name) const;

        /// Adds a new property; returns nullopt if the name already exists.
        template <class T>
        [[nodiscard]] std::optional<PropertyBuffer<T>> Add(std::string name, T m_Defaultvalue = T());

        /// Gets a mutable property by name; nullopt if name/type mismatch.
        template <class T>
        [[nodiscard]] std::optional<PropertyBuffer<T>> Get(std::string_view name);

        /// Gets a const property by name; nullopt if name/type mismatch.
        template <class T>
        [[nodiscard]] std::optional<ConstPropertyBuffer<T>> Get(std::string_view name) const;

        /// Gets a mutable property by id; nullopt if id/type mismatch.
        template <class T>
        [[nodiscard]] std::optional<PropertyBuffer<T>> Get(PropertyId id);

        /// Gets a const property by id; nullopt if id/type mismatch.
        template <class T>
        [[nodiscard]] std::optional<ConstPropertyBuffer<T>> Get(PropertyId id) const;

        /// Gets or creates a property by name.
        template <class T>
        [[nodiscard]] PropertyBuffer<T> GetOrAdd(std::string name, T m_Defaultvalue = T());

        /// Removes a property using its buffer handle.
        template <class T>
        bool Remove(PropertyBuffer<T>& handle);

        /// Removes a property by id.
        bool Remove(PropertyId id);

    private:
        [[nodiscard]] bool IsValidId(PropertyId id) const noexcept;

        [[nodiscard]] Internal::PropertyStorageBase* Storage(PropertyId id) noexcept;

        [[nodiscard]] const Internal::PropertyStorageBase* Storage(PropertyId id) const noexcept;

        template <class T>
        [[nodiscard]] Internal::PropertyStorage<T>* Storage(PropertyId id) noexcept;

        template <class T>
        [[nodiscard]] const Internal::PropertyStorage<T>* Storage(PropertyId id) const noexcept;

        /// Transparent hasher for string_view lookups into string-keyed maps.
        struct StringHash
        {
            using is_transparent = void;
            [[nodiscard]] size_t operator()(std::string_view sv) const noexcept
            {
                return std::hash<std::string_view>{}(sv);
            }
            [[nodiscard]] size_t operator()(const std::string& s) const noexcept
            {
                return std::hash<std::string_view>{}(std::string_view(s));
            }
        };

        std::vector<std::unique_ptr<Internal::PropertyStorageBase>> m_Storages;
        std::unordered_map<std::string, PropertyId, StringHash, std::equal_to<>> m_NameIndex;
        size_t m_Size{0};
    };

    /// Mutable view onto a typed property storage.
    template <class T>
    class PropertyBuffer
    {
    public:
        PropertyBuffer() = default;

        /// Returns the underlying property id.
        [[nodiscard]] PropertyId Id() const noexcept { return m_Id; }

        /// Returns the property name; asserts if invalid.
        [[nodiscard]] std::string_view Name() const noexcept
        {
            assert(m_Storage != nullptr);
            return m_Storage->Name();
        }

        /// True if this buffer refers to a valid storage.
        [[nodiscard]] explicit operator bool() const noexcept { return m_Storage != nullptr; }

        /// Direct access to the backing vector; asserts if invalid.
        [[nodiscard]] std::vector<T>& Vector() noexcept
        {
            assert(m_Storage != nullptr);
            return m_Storage->Data();
        }

        [[nodiscard]] const std::vector<T>& Vector() const noexcept
        {
            assert(m_Storage != nullptr);
            return static_cast<const Internal::PropertyStorage<T>*>(m_Storage)->Data();
        }

        /// Element access; asserts if invalid or index out of range.
        [[nodiscard]] decltype(auto) operator[](size_t index)
        {
            assert(m_Storage != nullptr);
            assert(index < m_Storage->Data().size());
            return m_Storage->Data()[index];
        }

        /// Read-only element access; asserts if invalid or index out of range.
        [[nodiscard]] decltype(auto) operator[](size_t index) const
        {
            assert(m_Storage != nullptr);
            assert(index < m_Storage->Data().size());
            return static_cast<const Internal::PropertyStorage<T>*>(m_Storage)->Data()[index];
        }

        /// Returns a read-only span (unsupported for vector<bool>).
        [[nodiscard]] std::span<const T> Span() const noexcept requires (!std::is_same_v<T, bool>)
        {
            assert(m_Storage != nullptr);
            return std::span<const T>(static_cast<const Internal::PropertyStorage<T>*>(m_Storage)->Data());
        }

        /// Returns a mutable span (unsupported for vector<bool>).
        [[nodiscard]] std::span<T> Span() noexcept requires (!std::is_same_v<T, bool>)
        {
            assert(m_Storage != nullptr);
            return std::span<T>(m_Storage->Data());
        }

        /// Returns raw data pointer (unsupported for vector<bool>).
        [[nodiscard]] const T* Data() const noexcept requires (!std::is_same_v<T, bool>)
        {
            assert(m_Storage != nullptr);
            return static_cast<const Internal::PropertyStorage<T>*>(m_Storage)->Data().data();
        }

        /// Returns raw data pointer (unsupported for vector<bool>).
        [[nodiscard]] T* Data() noexcept requires (!std::is_same_v<T, bool>)
        {
            assert(m_Storage != nullptr);
            return m_Storage->Data().data();
        }

        /// Clears the handle so it no longer refers to storage.
        void Reset() noexcept
        {
            m_Storage = nullptr;
            m_Id = static_cast<PropertyId>(-1);
        }

    private:
        friend class PropertyRegistry;

        PropertyBuffer(PropertyId id, Internal::PropertyStorage<T>* storage) : m_Storage(storage), m_Id(id)
        {
        }

        Internal::PropertyStorage<T>* m_Storage{nullptr};
        PropertyId m_Id{static_cast<PropertyId>(-1)};
    };

    /// Const view onto a typed property storage.
    template <class T>
    class ConstPropertyBuffer
    {
    public:
        ConstPropertyBuffer() = default;

        /// Returns the underlying property id.
        [[nodiscard]] PropertyId Id() const noexcept { return m_Id; }

        /// Returns the property name; asserts if invalid.
        [[nodiscard]] std::string_view Name() const noexcept
        {
            assert(m_Storage != nullptr);
            return m_Storage->Name();
        }

        /// True if this buffer refers to a valid storage.
        [[nodiscard]] explicit operator bool() const noexcept { return m_Storage != nullptr; }

        /// Direct access to the backing vector; asserts if invalid.
        [[nodiscard]] const std::vector<T>& Vector() const noexcept
        {
            assert(m_Storage != nullptr);
            return m_Storage->Data();
        }

        /// Element access; asserts if invalid or index out of range.
        [[nodiscard]] decltype(auto) operator[](size_t index) const
        {
            assert(m_Storage != nullptr);
            assert(index < m_Storage->Data().size());
            return m_Storage->Data()[index];
        }

        /// Returns a read-only span (unsupported for vector<bool>).
        [[nodiscard]] std::span<const T> Span() const noexcept requires (!std::is_same_v<T, bool>)
        {
            assert(m_Storage != nullptr);
            return std::span<const T>(m_Storage->Data());
        }

        /// Returns raw data pointer (unsupported for vector<bool>).
        [[nodiscard]] const T* Data() const noexcept requires (!std::is_same_v<T, bool>)
        {
            assert(m_Storage != nullptr);
            return m_Storage->Data().data();
        }

    private:
        friend class PropertyRegistry;

        ConstPropertyBuffer(PropertyId id, const Internal::PropertyStorage<T>* storage)
            : m_Storage(storage), m_Id(id)
        {
        }

        const Internal::PropertyStorage<T>* m_Storage{nullptr};
        PropertyId m_Id{static_cast<PropertyId>(-1)};
    };

    struct BoolPropertyMap
    {
        Geometry::Linalg::DenseMatrix Values{};
        Geometry::Linalg::NumericDiagnostics Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept { return Diagnostics.Succeeded(); }
    };

    namespace Internal
    {
        template <class T>
        concept EigenMappablePropertyScalar = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;

        template <class T>
        concept EigenMappablePropertyVector = Geometry::Linalg::FixedSizeVector<T>;

        [[nodiscard]] inline BoolPropertyMap MakeBoolPropertyMap(const std::vector<bool>& values)
        {
            BoolPropertyMap result;
            result.Values = Geometry::Linalg::DenseMatrix(values.size(), 1u);
            for (std::size_t row = 0; row < values.size(); ++row)
            {
                result.Values(row, 0u) = values[row] ? 1.0 : 0.0;
            }
            result.Diagnostics.Status = Geometry::Linalg::NumericStatus::Success;
            result.Diagnostics.Rank = values.empty() ? 0u : 1u;
            return result;
        }
    } // namespace Internal

    template <class T>
        requires (Internal::EigenMappablePropertyScalar<T>)
    [[nodiscard]] auto MapProperty(PropertyBuffer<T>& property)
    {
        if (!property)
        {
            return Geometry::Linalg::MapAsMatrix(std::span<T>{}, 0u, 0u, 0);
        }
        return Geometry::Linalg::MapAsMatrix(property.Span(), property.Vector().size(), 1u, 1);
    }

    template <class T>
        requires (Internal::EigenMappablePropertyScalar<T>)
    [[nodiscard]] auto MapProperty(const ConstPropertyBuffer<T>& property)
    {
        if (!property)
        {
            return Geometry::Linalg::MapAsMatrix(std::span<const T>{}, 0u, 0u, 0);
        }
        return Geometry::Linalg::MapAsMatrix(property.Span(), property.Vector().size(), 1u, 1);
    }

    template <class T>
        requires (Internal::EigenMappablePropertyVector<T>)
    [[nodiscard]] auto MapProperty(PropertyBuffer<T>& property)
    {
        if (!property)
        {
            return Geometry::Linalg::MapVectorAsMatrix(std::span<T>{});
        }
        return Geometry::Linalg::MapVectorAsMatrix(property.Span());
    }

    template <class T>
        requires (Internal::EigenMappablePropertyVector<T>)
    [[nodiscard]] auto MapProperty(const ConstPropertyBuffer<T>& property)
    {
        if (!property)
        {
            return Geometry::Linalg::MapVectorAsMatrix(std::span<const T>{});
        }
        return Geometry::Linalg::MapVectorAsMatrix(property.Span());
    }

    [[nodiscard]] inline BoolPropertyMap MapProperty(PropertyBuffer<bool>& property)
    {
        if (!property)
        {
            BoolPropertyMap result;
            result.Diagnostics.Status = Geometry::Linalg::NumericStatus::InvalidInput;
            return result;
        }
        return Internal::MakeBoolPropertyMap(property.Vector());
    }

    [[nodiscard]] inline BoolPropertyMap MapProperty(const ConstPropertyBuffer<bool>& property)
    {
        if (!property)
        {
            BoolPropertyMap result;
            result.Diagnostics.Status = Geometry::Linalg::NumericStatus::InvalidInput;
            return result;
        }
        return Internal::MakeBoolPropertyMap(property.Vector());
    }

    template <class T>
    Internal::PropertyStorage<T>* PropertyRegistry::Storage(PropertyId id) noexcept
    {
        auto* base = Storage(id);
        if (base == nullptr || base->Type() != Internal::TypeInfo<T>::ID())
        {
            return nullptr;
        }
        return static_cast<Internal::PropertyStorage<T>*>(base);
    }

    template <class T>
    const Internal::PropertyStorage<T>* PropertyRegistry::Storage(PropertyId id) const noexcept
    {
        auto* base = Storage(id);
        if (base == nullptr || base->Type() != Internal::TypeInfo<T>::ID())
        {
            return nullptr;
        }
        return static_cast<const Internal::PropertyStorage<T>*>(base);
    }

    template <class T>
    std::optional<PropertyBuffer<T>> PropertyRegistry::Add(std::string name, T m_Defaultvalue)
    {
        if (Find(name).has_value())
        {
            return std::nullopt;
        }

        auto storage = std::make_unique<Internal::PropertyStorage<T>>(std::move(name), std::move(m_Defaultvalue));
        storage->Resize(m_Size);
        auto* raw = storage.get();
        PropertyId id = m_Storages.size();
        m_NameIndex.emplace(std::string(raw->Name()), id);
        m_Storages.push_back(std::move(storage));
        return PropertyBuffer<T>(id, raw);
    }

    template <class T>
    std::optional<PropertyBuffer<T>> PropertyRegistry::Get(std::string_view name)
    {
        if (auto id = Find(name))
        {
            return Get<T>(*id);
        }
        return std::nullopt;
    }

    template <class T>
    std::optional<ConstPropertyBuffer<T>> PropertyRegistry::Get(std::string_view name) const
    {
        if (auto id = Find(name))
        {
            return Get<T>(*id);
        }
        return std::nullopt;
    }

    template <class T>
    std::optional<PropertyBuffer<T>> PropertyRegistry::Get(PropertyId id)
    {
        if (auto* typed = Storage<T>(id))
        {
            return PropertyBuffer<T>(id, typed);
        }
        return std::nullopt;
    }

    template <class T>
    std::optional<ConstPropertyBuffer<T>> PropertyRegistry::Get(PropertyId id) const
    {
        if (auto* typed = Storage<T>(id))
        {
            return ConstPropertyBuffer<T>(id, typed);
        }
        return std::nullopt;
    }

    template <class T>
    PropertyBuffer<T> PropertyRegistry::GetOrAdd(std::string name, T m_Defaultvalue)
    {
        if (auto existing = Get<T>(name))
        {
            return *existing;
        }

        auto created = Add<T>(std::move(name), std::move(m_Defaultvalue));
        if (created)
        {
            return std::move(*created);
        }
        return PropertyBuffer<T>();
    }

    template <class T>
    bool PropertyRegistry::Remove(PropertyBuffer<T>& handle)
    {
        const auto id = handle.Id();
        handle.Reset();
        return Remove(id);
    }

    /// Value-semantic wrapper around PropertyBuffer.
    template <class T>
    class Property
    {
    public:
        Property() = default;

        explicit Property(PropertyBuffer<T> buffer) : m_Buffer(buffer)
        {
        }

        /// Returns true when this property refers to a valid storage.
        [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_Buffer); }
        explicit operator bool() const noexcept { return static_cast<bool>(m_Buffer); }

        /// Property name (asserts if invalid).
        [[nodiscard]] std::string_view Name() const { return m_Buffer.Name(); }

        /// Element access (asserts on invalid/out-of-range).
        [[nodiscard]] decltype(auto) operator[](size_t index) const { return m_Buffer[index]; }
        [[nodiscard]] decltype(auto) operator[](size_t index) { return m_Buffer[index]; }

        /// Backing storage access.
        [[nodiscard]] std::vector<T>& Vector() { return m_Buffer.Vector(); }
        [[nodiscard]] const std::vector<T>& Vector() const { return m_Buffer.Vector(); }

        /// Alias for Vector().
        [[nodiscard]] std::vector<T>& Array() { return m_Buffer.Vector(); }
        [[nodiscard]] const std::vector<T>& Array() const { return m_Buffer.Vector(); }

        /// Span view (unsupported for vector<bool>).
        [[nodiscard]] std::span<T> Span() requires (!std::is_same_v<T, bool>) { return m_Buffer.Span(); }
        [[nodiscard]] std::span<const T> Span() const requires (!std::is_same_v<T, bool>) { return m_Buffer.Span(); }

        /// Raw data view (unsupported for vector<bool>).
        [[nodiscard]] T* Data() requires (!std::is_same_v<T, bool>) { return m_Buffer.Data(); }
        [[nodiscard]] const T* Data() const requires (!std::is_same_v<T, bool>) { return m_Buffer.Data(); }

        /// Access to the underlying handle.
        [[nodiscard]] PropertyBuffer<T>& Handle() noexcept { return m_Buffer; }
        [[nodiscard]] const PropertyBuffer<T>& Handle() const noexcept { return m_Buffer; }

        /// Clears the handle so it no longer refers to storage.
        void Reset() noexcept { m_Buffer.Reset(); }

    private:
        PropertyBuffer<T> m_Buffer;
    };

    /// Read-only value-semantic wrapper around ConstPropertyBuffer.
    template <class T>
    class ConstProperty
    {
    public:
        ConstProperty() = default;

        explicit ConstProperty(ConstPropertyBuffer<T> buffer) : m_Buffer(buffer)
        {
        }

        [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_Buffer); }
        explicit operator bool() const noexcept { return static_cast<bool>(m_Buffer); }

        [[nodiscard]] std::string_view Name() const { return m_Buffer.Name(); }

        [[nodiscard]] decltype(auto) operator[](size_t index) const { return m_Buffer[index]; }

        [[nodiscard]] const std::vector<T>& Vector() const { return m_Buffer.Vector(); }
        [[nodiscard]] const std::vector<T>& Array() const { return m_Buffer.Vector(); }
        [[nodiscard]] std::span<const T> Span() const requires (!std::is_same_v<T, bool>) { return m_Buffer.Span(); }
        [[nodiscard]] const T* Data() const requires (!std::is_same_v<T, bool>) { return m_Buffer.Data(); }

        [[nodiscard]] const ConstPropertyBuffer<T>& Handle() const noexcept { return m_Buffer; }

    private:
        ConstPropertyBuffer<T> m_Buffer;
    };

    /// Property wrapper that indexes by a handle type (e.g., VertexHandle).
    template <class HandleT, class T>
    class HandleProperty : public Property<T>
    {
    public:
        using Property<T>::Property;

        explicit HandleProperty(Property<T> base) : Property<T>(std::move(base))
        {
        }

        /// Access by handle index (asserts on invalid/out-of-range).
        [[nodiscard]] decltype(auto) operator[](HandleT handle) { return Property<T>::operator[](handle.Index); }

        [[nodiscard]] decltype(auto) operator[](HandleT handle) const
        {
            return Property<T>::operator[](handle.Index);
        }
    };

    /// Read-only property wrapper indexed by a handle type (e.g., VertexHandle).
    template <class HandleT, class T>
    class ConstHandleProperty : public ConstProperty<T>
    {
    public:
        using ConstProperty<T>::ConstProperty;

        explicit ConstHandleProperty(ConstProperty<T> base) : ConstProperty<T>(std::move(base))
        {
        }

        [[nodiscard]] decltype(auto) operator[](HandleT handle) const { return ConstProperty<T>::operator[](handle.Index); }
    };

    /// Convenience wrapper for a PropertyRegistry.
    class PropertySet
    {
    public:
        PropertySet() = default;

        /// Number of elements in each property array.
        [[nodiscard]] size_t Size() const noexcept { return m_Registry.Size(); }

        inline void Clear() { m_Registry.Clear(); }
        inline void Reserve(size_t n) { m_Registry.Reserve(n); }
        inline void Resize(size_t n) { m_Registry.Resize(n); }
        inline void PushBack() { m_Registry.PushBack(); }
        inline void Swap(size_t i0, size_t i1) { m_Registry.Swap(i0, i1); }
        inline void ShrinkToFit() { m_Registry.ShrinkToFit(); }
        // GEOM-031 compatibility wrapper; new code should use ShrinkToFit().
        inline void Shrink_to_fit() { ShrinkToFit(); }
        [[nodiscard]] inline bool Empty() const { return m_Registry.Size() == 0; }
        [[nodiscard]] inline bool Exists(std::string_view name) const { return m_Registry.Contains(name); }
        [[nodiscard]] inline std::vector<std::string> Properties() const { return m_Registry.PropertyNames(); }
        [[nodiscard]] inline std::vector<PropertyDescriptor> Descriptors() const { return m_Registry.Descriptors(true); }

        /// Adds a property; returns invalid Property on name collision.
        template <class T>
        [[nodiscard]] Property<T> Add(std::string name, T default_value = T());

        /// Gets a property by name; returns invalid Property on mismatch.
        template <class T>
        [[nodiscard]] Property<T> Get(std::string_view name);

        /// Const overload; returns a read-only property view.
        template <class T>
        [[nodiscard]] ConstProperty<T> Get(std::string_view name) const;

        /// Gets or creates a property by name.
        template <class T>
        [[nodiscard]] Property<T> GetOrAdd(std::string name, T default_value = T());

        /// Removes a property and resets the handle.
        template <class T>
        void Remove(Property<T>& property);

        PropertyRegistry& Registry() noexcept { return m_Registry; }
        [[nodiscard]] const PropertyRegistry& Registry() const noexcept { return m_Registry; }

    private:
        PropertyRegistry m_Registry;
    };

    class ConstPropertySet
    {
    public:
        ConstPropertySet() = default;
        explicit ConstPropertySet(const PropertySet& set) : m_Set(&set) {}

        [[nodiscard]] bool IsValid() const noexcept { return m_Set != nullptr; }
        [[nodiscard]] explicit operator bool() const noexcept { return IsValid(); }

        [[nodiscard]] size_t Size() const noexcept { return m_Set ? m_Set->Size() : 0u; }
        [[nodiscard]] bool Empty() const { return m_Set == nullptr || m_Set->Empty(); }
        [[nodiscard]] bool Exists(std::string_view name) const { return m_Set != nullptr && m_Set->Exists(name); }
        [[nodiscard]] std::vector<std::string> Properties() const { return m_Set ? m_Set->Properties() : std::vector<std::string>{}; }
        [[nodiscard]] std::vector<PropertyDescriptor> Descriptors() const
        {
            return m_Set ? m_Set->Registry().Descriptors(false) : std::vector<PropertyDescriptor>{};
        }

        template <class T>
        [[nodiscard]] ConstProperty<T> Get(std::string_view name) const
        {
            if (m_Set == nullptr)
            {
                return ConstProperty<T>();
            }
            if (auto handle = m_Set->Registry().Get<T>(name))
            {
                return ConstProperty<T>(*handle);
            }
            return ConstProperty<T>();
        }

    private:
        const PropertySet* m_Set{nullptr};
    };

    template <class T>
    Property<T> PropertySet::Add(std::string name, T default_value)
    {
        if (auto handle = m_Registry.Add<T>(std::move(name), std::move(default_value)))
        {
            return Property<T>(*handle);
        }
        return Property<T>();
    }

    template <class T>
    Property<T> PropertySet::Get(std::string_view name)
    {
        if (auto handle = m_Registry.Get<T>(name))
        {
            return Property<T>(*handle);
        }
        return Property<T>();
    }

    template <class T>
    ConstProperty<T> PropertySet::Get(std::string_view name) const
    {
        if (auto handle = m_Registry.Get<T>(name))
        {
            return ConstProperty<T>(*handle);
        }
        return ConstProperty<T>();
    }

    template <class T>
    Property<T> PropertySet::GetOrAdd(std::string name, T default_value)
    {
        auto handle = m_Registry.GetOrAdd<T>(std::move(name), std::move(default_value));
        return Property<T>(handle);
    }

    template <class T>
    void PropertySet::Remove(Property<T>& property)
    {
        m_Registry.Remove(property.Handle());
        property.Reset();
    }

    using PropertyIndex = std::uint32_t;

    /// Sentinel index used to mark an invalid handle.
    constexpr PropertyIndex kInvalidIndex = std::numeric_limits<PropertyIndex>::max();

    /// Describes a contiguous sub-range of elements within a geometry container.
    /// Used by submesh views to restrict the visible window into the underlying
    /// storage without copying data. A Size of 0 means "use full extent".
    struct ElementRange
    {
        std::size_t Offset{0};
        std::size_t Size{0};
    };

    /// Lightweight index handle tagged by a type.
    template <typename Tag>
    struct Handle
    {
        PropertyIndex Index = kInvalidIndex;
        auto operator<=>(const Handle&) const = default;
        [[nodiscard]] bool IsValid() const { return Index != kInvalidIndex; }
    };

    // Export specific handle types
    struct VertexTag
    {
    };

    using VertexHandle = Handle<VertexTag>;

    struct FaceTag
    {
    };

    using FaceHandle = Handle<FaceTag>;

    struct EdgeTag
    {
    };

    using EdgeHandle = Handle<EdgeTag>;

    struct HalfedgeTag
    {
    };

    using HalfedgeHandle = Handle<HalfedgeTag>;

    struct NodeTag
    {
    };

    using NodeHandle = Handle<NodeTag>;

    template <class HandleT>
    class LiveElementRange;

    template <class HandleT>
    class LiveElementIterator
    {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = HandleT;
        using difference_type = std::ptrdiff_t;
        using pointer = const HandleT*;
        using reference = HandleT;

        LiveElementIterator() = default;

        [[nodiscard]] HandleT operator*() const noexcept
        {
            return HandleT{static_cast<PropertyIndex>(m_Index)};
        }

        LiveElementIterator& operator++()
        {
            ++m_Index;
            SkipDeleted();
            return *this;
        }

        void operator++(int)
        {
            ++(*this);
        }

        [[nodiscard]] friend bool operator==(const LiveElementIterator& it, std::default_sentinel_t) noexcept
        {
            return it.m_Range == nullptr || it.m_Index >= it.m_End;
        }

        [[nodiscard]] friend bool operator!=(const LiveElementIterator& it, std::default_sentinel_t sentinel) noexcept
        {
            return !(it == sentinel);
        }

    private:
        friend class LiveElementRange<HandleT>;

        LiveElementIterator(const LiveElementRange<HandleT>* range, std::size_t begin, std::size_t end)
            : m_Range(range), m_Index(begin), m_End(end)
        {
            SkipDeleted();
        }

        void SkipDeleted()
        {
            if (m_Range == nullptr || !m_Range->HasPredicate())
            {
                m_Index = m_End;
                return;
            }

            while (m_Index < m_End)
            {
                const HandleT handle{static_cast<PropertyIndex>(m_Index)};
                if (!m_Range->IsDeleted(handle))
                {
                    break;
                }
                ++m_Index;
            }
        }

        const LiveElementRange<HandleT>* m_Range{nullptr};
        std::size_t m_Index{0};
        std::size_t m_End{0};
    };

    template <class HandleT>
    class LiveElementRange
    {
    public:
        using DeletePredicate = std::function<bool(HandleT)>;
        using iterator = LiveElementIterator<HandleT>;

        LiveElementRange() = default;

        LiveElementRange(std::size_t count, DeletePredicate isDeleted)
            : LiveElementRange(0u, count, std::move(isDeleted))
        {
        }

        LiveElementRange(std::size_t offset, std::size_t count, DeletePredicate isDeleted)
            : m_Offset(offset), m_Count(count), m_IsDeleted(std::move(isDeleted))
        {
        }

        [[nodiscard]] iterator begin() const
        {
            if (!HasPredicate())
            {
                return iterator(this, EndIndex(), EndIndex());
            }
            return iterator(this, m_Offset, EndIndex());
        }

        [[nodiscard]] std::default_sentinel_t end() const noexcept
        {
            return {};
        }

        [[nodiscard]] std::size_t Offset() const noexcept { return m_Offset; }
        [[nodiscard]] std::size_t Count() const noexcept { return m_Count; }
        [[nodiscard]] bool Empty() const noexcept { return m_Count == 0u || !HasPredicate(); }

    private:
        friend class LiveElementIterator<HandleT>;

        [[nodiscard]] bool HasPredicate() const noexcept
        {
            return static_cast<bool>(m_IsDeleted);
        }

        [[nodiscard]] bool IsDeleted(HandleT handle) const
        {
            return !HasPredicate() || m_IsDeleted(handle);
        }

        [[nodiscard]] std::size_t EndIndex() const noexcept
        {
            return m_Offset + m_Count;
        }

        std::size_t m_Offset{0};
        std::size_t m_Count{0};
        DeletePredicate m_IsDeleted{};
    };

    // Stream operators are defined in Geometry.Properties.cpp (implementation unit)
    std::ostream& operator<<(std::ostream& os, VertexHandle v);
    std::ostream& operator<<(std::ostream& os, HalfedgeHandle h);
    std::ostream& operator<<(std::ostream& os, EdgeHandle e);
    std::ostream& operator<<(std::ostream& os, FaceHandle f);
    std::ostream& operator<<(std::ostream& os, NodeHandle n);

    /// Property indexed by VertexHandle.
    template <class T>
    using VertexProperty = HandleProperty<VertexHandle, T>;

    /// Read-only property indexed by VertexHandle.
    template <class T>
    using ConstVertexProperty = ConstHandleProperty<VertexHandle, T>;

    /// Property indexed by HalfedgeHandle.
    template <class T>
    using HalfedgeProperty = HandleProperty<HalfedgeHandle, T>;

    /// Read-only property indexed by HalfedgeHandle.
    template <class T>
    using ConstHalfedgeProperty = ConstHandleProperty<HalfedgeHandle, T>;

    /// Property indexed by EdgeHandle.
    template <class T>
    using EdgeProperty = HandleProperty<EdgeHandle, T>;

    /// Read-only property indexed by EdgeHandle.
    template <class T>
    using ConstEdgeProperty = ConstHandleProperty<EdgeHandle, T>;

    /// Property indexed by FaceHandle.
    template <class T>
    using FaceProperty = HandleProperty<FaceHandle, T>;

    /// Read-only property indexed by FaceHandle.
    template <class T>
    using ConstFaceProperty = ConstHandleProperty<FaceHandle, T>;

    /// Property indexed by NodeHandle.
    template <class T>
    using NodeProperty = HandleProperty<NodeHandle, T>;

    /// Read-only property indexed by NodeHandle.
    template <class T>
    using ConstNodeProperty = ConstHandleProperty<NodeHandle, T>;

    /// Standard property sets for topology elements.
    using Vertices = PropertySet;
    using Halfedges = PropertySet;
    using Edges = PropertySet;
    using Faces = PropertySet;
    using Nodes = PropertySet;
} // namespace Geometry
