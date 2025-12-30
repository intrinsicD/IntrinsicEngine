module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <limits>

export module Geometry:Properties;

export namespace Geometry
{
    using PropertyId = size_t;

    class PropertyRegistry;

    namespace Internal
    {
        using TypeID = std::uintptr_t;

        template <typename T>
        struct TypeInfo
        {
            static TypeID ID()
            {
                // The address of this static variable is unique per type T
                static char s_ID;
                return reinterpret_cast<std::uintptr_t>(&s_ID);
            }
        };

        class PropertyStorageBase
        {
        public:
            PropertyStorageBase() = default;
            virtual ~PropertyStorageBase() = default;

            PropertyStorageBase(const PropertyStorageBase&) = delete;
            PropertyStorageBase& operator=(const PropertyStorageBase&) = delete;

            [[nodiscard]] virtual std::unique_ptr<PropertyStorageBase> Clone() const = 0;

            [[nodiscard]] virtual std::string_view Name() const = 0;
            virtual void Reserve(size_t n) = 0;
            virtual void Resize(size_t n) = 0;
            virtual void ShrinkToFit() = 0;
            virtual void PushBack() = 0;
            virtual void Swap(size_t i0, size_t i1) = 0;

            [[nodiscard]] virtual TypeID Type() const noexcept = 0;
        };

        template <class T>
        class PropertyStorage final : public PropertyStorageBase
        {
        public:
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

    class PropertyRegistry
    {
    public:
        PropertyRegistry() = default;
        ~PropertyRegistry() = default;

        PropertyRegistry(const PropertyRegistry& other);
        PropertyRegistry(PropertyRegistry&&) noexcept = default;
        PropertyRegistry& operator=(const PropertyRegistry& other);
        PropertyRegistry& operator=(PropertyRegistry&&) noexcept = default;

        [[nodiscard]] size_t Size() const noexcept { return m_Size; }
        [[nodiscard]] size_t PropertyCount() const noexcept { return m_Storages.size(); }

        [[nodiscard]] std::vector<std::string> PropertyNames() const;

        inline void Clear() { m_Storages.clear(); }
        inline void Reserve(size_t n) { m_Storages.reserve(n); }

        void Resize(size_t n)
        {
            m_Size = n;
            for (auto& storage : m_Storages) storage->Resize(n);
        }

        void ShrinkToFit() { for (auto& storage : m_Storages) storage->ShrinkToFit(); }
        void PushBack() { for (auto& storage : m_Storages) storage->PushBack(); }

        void Swap(size_t i0, size_t i1)
        {
            for (auto& storage : m_Storages) storage->Swap(i0, i1);
        }

        [[nodiscard]] bool Contains(std::string_view name) const
        {
            return Find(name).has_value();
        }

        [[nodiscard]] std::optional<PropertyId> Find(std::string_view name) const
        {
            for (size_t i = 0; i < m_Storages.size(); ++i)
            {
                if (m_Storages[i]->Name() == name) return i;
            }
            return std::nullopt;
        }

        template <class T>
        [[nodiscard]] std::optional<PropertyBuffer<T>> Add(std::string name, T m_Defaultvalue = T());

        template <class T>
        [[nodiscard]] std::optional<PropertyBuffer<T>> Get(std::string_view name);

        template <class T>
        [[nodiscard]] std::optional<ConstPropertyBuffer<T>> Get(std::string_view name) const;

        template <class T>
        [[nodiscard]] std::optional<PropertyBuffer<T>> Get(PropertyId id);

        template <class T>
        [[nodiscard]] std::optional<ConstPropertyBuffer<T>> Get(PropertyId id) const;

        template <class T>
        [[nodiscard]] PropertyBuffer<T> GetOrAdd(std::string name, T m_Defaultvalue = T());

        template <class T>
        bool Remove(PropertyBuffer<T>& handle);

        bool Remove(PropertyId id);

    private:
        [[nodiscard]] Internal::PropertyStorageBase* Storage(PropertyId id) noexcept
        {
            return m_Storages[id].get();
        }

        [[nodiscard]] const Internal::PropertyStorageBase* Storage(PropertyId id) const noexcept
        {
            return m_Storages[id].get();
        }

        template <class T>
        [[nodiscard]] Internal::PropertyStorage<T>* Storage(PropertyId id) noexcept;

        template <class T>
        [[nodiscard]] const Internal::PropertyStorage<T>* Storage(PropertyId id) const noexcept;

        std::vector<std::unique_ptr<Internal::PropertyStorageBase>> m_Storages;
        size_t m_Size{0};
    };

    template <class T>
    class PropertyBuffer
    {
    public:
        PropertyBuffer() = default;

        [[nodiscard]] PropertyId Id() const noexcept { return m_Id; }

        [[nodiscard]] const std::string& Name() const noexcept
        {
            assert(m_Storage != nullptr);
            return m_Storage->Name();
        }

        [[nodiscard]] explicit operator bool() const noexcept { return m_Storage != nullptr; }

        [[nodiscard]] std::vector<T>& Vector() const noexcept
        {
            assert(m_Storage != nullptr);
            return m_Storage->Data();
        }

        [[nodiscard]] decltype(auto) operator[](size_t index) const
        {
            assert(m_Storage != nullptr);
            assert(index < m_Storage->Data().size());
            return m_Storage->Data()[index];
        }

        [[nodiscard]] std::span<const T> Span() const noexcept requires (!std::is_same_v<T, bool>)
        {
            assert(m_Storage != nullptr);
            return std::span<T>(m_Storage->Data());
        }

        [[nodiscard]] std::span<T> Span() noexcept requires (!std::is_same_v<T, bool>)
        {
            assert(m_Storage != nullptr);
            return std::span<T>(m_Storage->Data());
        }

        [[nodiscard]] const T* Data() const noexcept requires (!std::is_same_v<T, bool>)
        {
            assert(m_Storage != nullptr);
            return m_Storage->Data().data();
        }

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

    template <class T>
    class ConstPropertyBuffer
    {
    public:
        ConstPropertyBuffer() = default;

        [[nodiscard]] PropertyId Id() const noexcept { return m_Id; }

        [[nodiscard]] const std::string& Name() const noexcept
        {
            assert(m_Storage != nullptr);
            return m_Storage->name();
        }

        [[nodiscard]] explicit operator bool() const noexcept { return m_Storage != nullptr; }

        [[nodiscard]] const std::vector<T>& Vector() const noexcept
        {
            assert(m_Storage != nullptr);
            return m_Storage->Data();
        }

        [[nodiscard]] decltype(auto) operator[](size_t index) const
        {
            assert(m_Storage != nullptr);
            assert(index < m_Storage->Data().size());
            return m_Storage->Data()[index];
        }

        [[nodiscard]] std::span<const T> Span() const noexcept requires (!std::is_same_v<T, bool>)
        {
            assert(m_Storage != nullptr);
            return std::span<const T>(m_Storage->Data());
        }

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
        m_Storages.push_back(std::move(storage));
        return PropertyBuffer<T>(m_Storages.size() - 1U, raw);
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
        if (auto existing = get<T>(name))
        {
            return *existing;
        }

        auto created = add<T>(std::move(name), std::move(m_Defaultvalue));
        if (created)
        {
            return std::move(*created);
        }
        return PropertyBuffer<T>();
    }

    template <class T>
    bool PropertyRegistry::Remove(PropertyBuffer<T>& handle)
    {
        if (!handle)
        {
            return false;
        }

        auto* typed = storage<T>(handle.id_);
        if (typed != handle.storage_)
        {
            return false;
        }

        const bool removed = remove(handle.id_);
        if (removed)
        {
            handle.reset();
        }
        return removed;
    }

    template <class T>
    class Property
    {
    public:
        Property() = default;

        explicit Property(PropertyBuffer<T> buffer) : m_Buffer(buffer)
        {
        }

        [[nodiscard]] bool IsValid() const noexcept { return static_cast<bool>(m_Buffer); }
        explicit operator bool() const noexcept { return static_cast<bool>(m_Buffer); }

        [[nodiscard]] const std::string& Name() const { return m_Buffer.Name(); }

        [[nodiscard]] decltype(auto) operator[](size_t index) const { return m_Buffer[index]; }
        [[nodiscard]] decltype(auto) operator[](size_t index) { return m_Buffer[index]; }

        [[nodiscard]] std::vector<T>& Vector() { return m_Buffer.Vector(); }
        [[nodiscard]] const std::vector<T>& Vector() const { return m_Buffer.Vector(); }

        [[nodiscard]] std::vector<T>& Array() { return m_Buffer.Vector(); }
        [[nodiscard]] const std::vector<T>& Array() const { return m_Buffer.Vector(); }

        [[nodiscard]] std::span<T> Span() { return m_Buffer.Span(); }
        [[nodiscard]] std::span<const T> Span() const { return m_Buffer.Span(); }

        [[nodiscard]] PropertyBuffer<T>& Handle() noexcept { return m_Buffer; }
        [[nodiscard]] const PropertyBuffer<T>& Handle() const noexcept { return m_Buffer; }

        void Reset() noexcept { m_Buffer.Reset(); }

    private:
        PropertyBuffer<T> m_Buffer;
    };

    template <class HandleT, class T>
    class HandleProperty : public Property<T>
    {
    public:
        using Property<T>::Property;

        explicit HandleProperty(Property<T> base) : Property<T>(std::move(base))
        {
        }

        [[nodiscard]] decltype(auto) operator[](HandleT handle) { return Property<T>::operator[](handle.Index); }

        [[nodiscard]] decltype(auto) operator[](HandleT handle) const
        {
            return Property<T>::operator[](handle.Index);
        }
    };

    class PropertySet
    {
    public:
        PropertySet() = default;

        [[nodiscard]] size_t Size() const noexcept { return m_Registry.Size(); }

        inline void Clear() { m_Registry.Clear(); }
        inline void Reserve(size_t n) { m_Registry.Reserve(n); }
        inline void Resize(size_t n) { m_Registry.Resize(n); }
        inline void PushBack() { m_Registry.PushBack(); }
        inline void Swap(size_t i0, size_t i1) { m_Registry.Swap(i0, i1); }
        inline void Shrink_to_fit() { m_Registry.ShrinkToFit(); }
        [[nodiscard]] inline bool Empty() const { return m_Registry.Size() == 0; }
        [[nodiscard]] inline bool Exists(std::string_view name) const { return m_Registry.Contains(name); }
        [[nodiscard]] inline std::vector<std::string> Properties() const { return m_Registry.PropertyNames(); }

        template <class T>
        [[nodiscard]] Property<T> Add(std::string name, T default_value = T());

        template <class T>
        [[nodiscard]] Property<T> Get(std::string_view name);

        template <class T>
        [[nodiscard]] Property<T> Get(std::string_view name) const;

        template <class T>
        [[nodiscard]] Property<T> GetOrAdd(std::string name, T default_value = T());

        template <class T>
        void Remove(Property<T>& property);

        PropertyRegistry& Registry() noexcept { return m_Registry; }
        [[nodiscard]] const PropertyRegistry& Registry() const noexcept { return m_Registry; }

    private:
        PropertyRegistry m_Registry;
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
    Property<T> PropertySet::Get(std::string_view name) const
    {
        return const_cast<PropertySet*>(this)->Get<T>(name);
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
        m_Registry.Remove(property.handle());
        property.reset();
    }

    using PropertyIndex = std::uint32_t;

    constexpr PropertyIndex kInvalidIndex = std::numeric_limits<PropertyIndex>::max();

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

    std::ostream& operator<<(std::ostream& os, VertexHandle v);

    std::ostream& operator<<(std::ostream& os, HalfedgeHandle h);

    std::ostream& operator<<(std::ostream& os, EdgeHandle e);

    std::ostream& operator<<(std::ostream& os, FaceHandle f);

    std::ostream& operator<<(std::ostream& os, NodeHandle n);

    template <class T>
    using VertexProperty = HandleProperty<VertexHandle, T>;

    template <class T>
    using HalfedgeProperty = HandleProperty<HalfedgeHandle, T>;

    template <class T>
    using EdgeProperty = HandleProperty<EdgeHandle, T>;

    template <class T>
    using FaceProperty = HandleProperty<FaceHandle, T>;

    template <class T>
    using NodeProperty = HandleProperty<NodeHandle, T>;

    using Vertices = PropertySet;
    using Halfedges = PropertySet;
    using Edges = PropertySet;
    using Faces = PropertySet;
    using Nodes = PropertySet;
} // namespace engine::geometry
