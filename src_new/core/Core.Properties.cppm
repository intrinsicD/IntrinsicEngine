// Provides heterogeneous named property columns and typed borrowed views so
// lower layers can share revision-aware storage without domain dependencies.
module;

#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

export module Core.Properties;

import Core.TypeToken;

namespace Extrinsic::Core
{
    export template <class T>
    concept PropertyValue = std::same_as<T, std::remove_cvref_t<T>> && std::copyable<T>;

    /// Process-local identifier for one PropertySet ownership epoch.
    export struct PropertyId
    {
        static constexpr std::uint32_t InvalidSlot =
            std::numeric_limits<std::uint32_t>::max();

        std::uint32_t Slot{InvalidSlot};
        std::uint64_t OwnerToken{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Slot != InvalidSlot && OwnerToken != 0u;
        }

        auto operator<=>(const PropertyId&) const = default;
    };

    /// Process-monotonic token for invalidating derived or cached data.
    /// Zero denotes no content mutation; observing a revision ends its edit epoch.
    export using PropertyRevision = std::uint64_t;
    export using PropertyTypeId = std::size_t;

    export using PropertyIndex = std::uint32_t;

    /// Sentinel index used to mark an invalid handle.
    constexpr PropertyIndex kInvalidIndex = std::numeric_limits<PropertyIndex>::max();

    export template <typename Tag>
    struct Handle
    {
        PropertyIndex Index = kInvalidIndex;
        auto operator<=>(const Handle&) const = default;
        [[nodiscard]] bool IsValid() const { return Index != kInvalidIndex; }
    };

    export struct PropertyDescriptor
    {
        PropertyId Id{};
        std::string Name{};
        PropertyTypeId Type{};
        std::string_view TypeName{};
        std::size_t ElementCount{};
        PropertyRevision ContentRevision{};
        bool Mutable{};
        bool SupportsContiguousSpan{};
    };

    namespace Detail
    {
        class RevisionState
        {
        public:
            void MarkModified() noexcept;
            [[nodiscard]] PropertyRevision Observe() const noexcept;
            [[nodiscard]] PropertyRevision Current() const noexcept;

        private:
            PropertyRevision m_Current{};
            mutable bool m_DirtySinceObservation{};
        };

        class StorageBase
        {
        public:
            explicit StorageBase(std::shared_ptr<RevisionState> revisions);
            virtual ~StorageBase() = default;

            StorageBase(const StorageBase&) = delete;
            StorageBase& operator=(const StorageBase&) = delete;

            [[nodiscard]] virtual std::unique_ptr<StorageBase>
            Clone(std::shared_ptr<RevisionState> revisions) const = 0;

            [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
            [[nodiscard]] virtual PropertyTypeId Type() const noexcept = 0;
            [[nodiscard]] virtual std::string_view TypeName() const noexcept = 0;
            [[nodiscard]] virtual PropertyDescriptor
            Describe(PropertyId id, bool mutableAccess) const = 0;

            virtual void Resize(std::size_t count) = 0;
            virtual void Reserve(std::size_t count) = 0;
            virtual void ShrinkToFit() = 0;
            virtual void PushBack() = 0;
            virtual void Swap(std::size_t a, std::size_t b) = 0;

            void MarkModified() noexcept;
            [[nodiscard]] PropertyRevision Revision() const noexcept;

        private:
            std::shared_ptr<RevisionState> m_Revisions{};
            PropertyRevision m_ContentRevision{};
        };

        template <PropertyValue T>
        class Storage final : public StorageBase
        {
        public:
            Storage(std::shared_ptr<RevisionState> revisions, std::string name,
                    T defaultValue);

            Storage(const Storage& other, std::shared_ptr<RevisionState> revisions);

            [[nodiscard]] std::unique_ptr<StorageBase>
            Clone(std::shared_ptr<RevisionState> revisions) const override;

            [[nodiscard]] std::string_view Name() const noexcept override;
            [[nodiscard]] PropertyTypeId Type() const noexcept override;
            [[nodiscard]] std::string_view TypeName() const noexcept override;
            [[nodiscard]] PropertyDescriptor Describe(PropertyId id,
                                                      bool mutableAccess) const override;

            void Resize(std::size_t count) override;
            void Reserve(std::size_t count) override;
            void ShrinkToFit() override;
            void PushBack() override;
            void Swap(std::size_t a, std::size_t b) override;
            [[nodiscard]] bool Assign(std::vector<T> values);

            [[nodiscard]] std::vector<T>& Values() noexcept;
            [[nodiscard]] const std::vector<T>& Values() const noexcept;

        private:
            std::string m_Name{};
            std::vector<T> m_Values{};
            T m_DefaultValue;
        };
    } // namespace Detail

    export template <PropertyValue T>
    class Property;

    export template <PropertyValue T>
    class ConstProperty;

    /// Owns named property columns that always share one element count.
    /// Concurrent access, including revision observation, requires external
    /// synchronization.
    export class PropertySet
    {
    public:
        PropertySet();
        ~PropertySet() = default;

        PropertySet(const PropertySet& other);
        PropertySet(PropertySet&& other) noexcept;
        PropertySet& operator=(const PropertySet& other);
        PropertySet& operator=(PropertySet&& other) noexcept;

        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] std::size_t PropertyCount() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;
        [[nodiscard]] PropertyRevision Revision() const noexcept;
        [[nodiscard]] std::optional<PropertyRevision>
        FindPropertyRevision(std::string_view name) const noexcept;

        [[nodiscard]] bool Contains(std::string_view name) const;
        [[nodiscard]] std::optional<PropertyId> Find(std::string_view name) const;
        /// Returns live properties in insertion order.
        [[nodiscard]] std::vector<std::string> PropertyNames() const;
        /// Returns descriptors in the same insertion order as PropertyNames().
        [[nodiscard]] std::vector<PropertyDescriptor> Descriptors();
        [[nodiscard]] std::vector<PropertyDescriptor> Descriptors() const;

        void Resize(std::size_t count);
        void ReserveElements(std::size_t count);
        void PushBack();
        void Swap(std::size_t a, std::size_t b);
        void ShrinkToFit();
        void Clear();

        template <PropertyValue T>
        [[nodiscard]] Property<T> Add(std::string name, T defaultValue);

        template <PropertyValue T>
            requires std::default_initializable<T>
        [[nodiscard]] Property<T> Add(std::string name)
        {
            return Add<T>(std::move(name), T{});
        }

        template <PropertyValue T>
        [[nodiscard]] Property<T> Get(std::string_view name);

        template <PropertyValue T>
        [[nodiscard]] ConstProperty<T> Get(std::string_view name) const;

        template <PropertyValue T>
        [[nodiscard]] Property<T> Get(PropertyId id);

        template <PropertyValue T>
        [[nodiscard]] ConstProperty<T> Get(PropertyId id) const;

        template <PropertyValue T>
        [[nodiscard]] Property<T> GetOrAdd(std::string name, T defaultValue);

        template <PropertyValue T>
            requires std::default_initializable<T>
        [[nodiscard]] Property<T> GetOrAdd(std::string name)
        {
            return GetOrAdd<T>(std::move(name), T{});
        }

        template <PropertyValue T>
        bool Remove(Property<T>& property);

        bool Remove(PropertyId id);

    private:
        template <PropertyValue U>
        friend class Property;

        template <PropertyValue U>
        friend class ConstProperty;

        struct Slot
        {
            std::unique_ptr<Detail::StorageBase> Value{};
        };

        struct StringHash
        {
            using is_transparent = void;

            [[nodiscard]] std::size_t
            operator()(std::string_view value) const noexcept
            {
                return std::hash<std::string_view>{}(value);
            }

            [[nodiscard]] std::size_t
            operator()(const std::string& value) const noexcept
            {
                return (*this)(std::string_view{value});
            }
        };

        void EnsureRevisionState();
        void MarkModified();
        void RebaseRevisions();
        void InvalidateAllHandles() noexcept;

        [[nodiscard]] PropertyId AcquireSlot();
        void ReleaseSlot(PropertyId id) noexcept;

        [[nodiscard]] Detail::StorageBase*
        ResolveStorage(PropertyId id) noexcept;
        [[nodiscard]] const Detail::StorageBase*
        ResolveStorage(PropertyId id) const noexcept;

        [[nodiscard]] std::vector<PropertyDescriptor>
        DescribeAll(bool mutableAccess) const;

        template <PropertyValue T>
        [[nodiscard]] Detail::Storage<T>* Resolve(PropertyId id) noexcept;

        template <PropertyValue T>
        [[nodiscard]] const Detail::Storage<T>*
        Resolve(PropertyId id) const noexcept;

        std::shared_ptr<Detail::RevisionState> m_Revisions{};
        std::vector<Slot> m_Slots{};
        std::unordered_map<std::string, PropertyId, StringHash, std::equal_to<>>
        m_NameIndex{};
        std::size_t m_Size{};
        std::uint64_t m_OwnerToken{};
    };

    /// Mutable borrowed view that must not outlive its PropertySet. Removal,
    /// clear, or set assignment invalidates the view; size and capacity edits
    /// can additionally invalidate spans and pointers.
    template <PropertyValue T>
    class Property
    {
    public:
        Property() = default;

        [[nodiscard]] PropertyId Id() const noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;

        [[nodiscard]] std::string_view Name() const noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] PropertyRevision Revision() const noexcept;

        void MarkModified() noexcept;

        [[nodiscard]] decltype(auto) operator[](std::size_t index);
        [[nodiscard]] decltype(auto) operator[](std::size_t index) const;

        /// Returns false for an invalid view or a value-count mismatch.
        [[nodiscard]] bool Assign(std::vector<T> values);
        [[nodiscard]] const std::vector<T>& Vector() const noexcept;

        [[nodiscard]] std::span<T> Span() noexcept
            requires(!std::same_as<T, bool>);
        [[nodiscard]] std::span<const T> Span() const noexcept
            requires(!std::same_as<T, bool>);

        [[nodiscard]] T* Data() noexcept
            requires(!std::same_as<T, bool>);
        [[nodiscard]] const T* Data() const noexcept
            requires(!std::same_as<T, bool>);

        void Reset() noexcept;

    private:
        friend class PropertySet;
        friend class ConstProperty<T>;

        Property(PropertySet* owner, PropertyId id) noexcept;

        [[nodiscard]] Detail::Storage<T>* Resolve() noexcept;
        [[nodiscard]] const Detail::Storage<T>* Resolve() const noexcept;

        PropertySet* m_Owner{};
        PropertyId m_Id{};
    };

    /// Read-only borrowed view with the same lifetime rules as Property<T>.
    template <PropertyValue T>
    class ConstProperty
    {
    public:
        ConstProperty() = default;
        explicit ConstProperty(const Property<T>& property) noexcept;

        [[nodiscard]] PropertyId Id() const noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;

        [[nodiscard]] std::string_view Name() const noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] PropertyRevision Revision() const noexcept;

        [[nodiscard]] decltype(auto) operator[](std::size_t index) const;
        [[nodiscard]] const std::vector<T>& Vector() const noexcept;

        [[nodiscard]] std::span<const T> Span() const noexcept
            requires(!std::same_as<T, bool>);

        [[nodiscard]] const T* Data() const noexcept
            requires(!std::same_as<T, bool>);

        void Reset() noexcept;

    private:
        friend class PropertySet;

        ConstProperty(const PropertySet* owner, PropertyId id) noexcept;

        [[nodiscard]] const Detail::Storage<T>* Resolve() const noexcept;

        const PropertySet* m_Owner{};
        PropertyId m_Id{};
    };

    /// Nullable borrowed read-only view that must not outlive its PropertySet.
    export class ConstPropertySet
    {
    public:
        ConstPropertySet() = default;
        explicit ConstPropertySet(const PropertySet& set) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] std::size_t PropertyCount() const noexcept;
        [[nodiscard]] bool Empty() const noexcept;
        [[nodiscard]] PropertyRevision Revision() const noexcept;
        [[nodiscard]] std::optional<PropertyRevision>
        FindPropertyRevision(std::string_view name) const noexcept;
        [[nodiscard]] bool Contains(std::string_view name) const;
        [[nodiscard]] std::optional<PropertyId> Find(std::string_view name) const;
        [[nodiscard]] std::vector<std::string> PropertyNames() const;
        [[nodiscard]] std::vector<PropertyDescriptor> Descriptors() const;

        template <PropertyValue T>
        [[nodiscard]] ConstProperty<T> Get(std::string_view name) const;

        template <PropertyValue T>
        [[nodiscard]] ConstProperty<T> Get(PropertyId id) const;

        void Reset() noexcept;

    private:
        const PropertySet* m_Set{};
    };

    template <PropertyValue T>
    Detail::Storage<T>::Storage(std::shared_ptr<RevisionState> revisions,
                                std::string name, T defaultValue)
        : StorageBase(std::move(revisions)), m_Name(std::move(name)),
          m_DefaultValue(std::move(defaultValue))
    {
    }

    template <PropertyValue T>
    Detail::Storage<T>::Storage(const Storage& other,
                                std::shared_ptr<RevisionState> revisions)
        : StorageBase(std::move(revisions)), m_Name(other.m_Name),
          m_Values(other.m_Values), m_DefaultValue(other.m_DefaultValue)
    {
        MarkModified();
    }

    template <PropertyValue T>
    std::unique_ptr<Detail::StorageBase>
    Detail::Storage<T>::Clone(std::shared_ptr<RevisionState> revisions) const
    {
        return std::make_unique<Storage<T>>(*this, std::move(revisions));
    }

    template <PropertyValue T>
    std::string_view Detail::Storage<T>::Name() const noexcept
    {
        return m_Name;
    }

    template <PropertyValue T>
    PropertyTypeId Detail::Storage<T>::Type() const noexcept
    {
        return TypeToken<T>();
    }

    template <PropertyValue T>
    std::string_view Detail::Storage<T>::TypeName() const noexcept
    {
        return TypeNameOf<T>();
    }

    template <PropertyValue T>
    PropertyDescriptor
    Detail::Storage<T>::Describe(const PropertyId id,
                                 const bool mutableAccess) const
    {
        return PropertyDescriptor{
            .Id = id,
            .Name = m_Name,
            .Type = TypeToken<T>(),
            .TypeName = TypeNameOf<T>(),
            .ElementCount = m_Values.size(),
            .ContentRevision = Revision(),
            .Mutable = mutableAccess,
            .SupportsContiguousSpan = !std::same_as<T, bool>,
        };
    }

    template <PropertyValue T>
    void Detail::Storage<T>::Resize(const std::size_t count)
    {
        if (m_Values.size() == count)
            return;

        MarkModified();
        m_Values.resize(count, m_DefaultValue);
    }

    template <PropertyValue T>
    void Detail::Storage<T>::Reserve(const std::size_t count)
    {
        m_Values.reserve(count);
    }

    template <PropertyValue T>
    void Detail::Storage<T>::ShrinkToFit()
    {
        m_Values.shrink_to_fit();
    }

    template <PropertyValue T>
    void Detail::Storage<T>::PushBack()
    {
        MarkModified();
        m_Values.push_back(m_DefaultValue);
    }

    template <PropertyValue T>
    void Detail::Storage<T>::Swap(const std::size_t a, const std::size_t b)
    {
        assert(a < m_Values.size());
        assert(b < m_Values.size());
        if (a == b)
            return;

        MarkModified();
        using Difference = typename std::vector<T>::difference_type;
        std::ranges::iter_swap(
            m_Values.begin() + static_cast<Difference>(a),
            m_Values.begin() + static_cast<Difference>(b));
    }

    template <PropertyValue T>
    bool Detail::Storage<T>::Assign(std::vector<T> values)
    {
        if (values.size() != m_Values.size())
            return false;

        MarkModified();
        m_Values = std::move(values);
        return true;
    }

    template <PropertyValue T>
    std::vector<T>& Detail::Storage<T>::Values() noexcept
    {
        MarkModified();
        return m_Values;
    }

    template <PropertyValue T>
    const std::vector<T>& Detail::Storage<T>::Values() const noexcept
    {
        return m_Values;
    }

    template <PropertyValue T>
    Detail::Storage<T>*
    PropertySet::Resolve(const PropertyId id) noexcept
    {
        Detail::StorageBase* storage = ResolveStorage(id);
        if (storage == nullptr || storage->Type() != TypeToken<T>() ||
            storage->TypeName() != TypeNameOf<T>())
        {
            return nullptr;
        }
        return static_cast<Detail::Storage<T>*>(storage);
    }

    template <PropertyValue T>
    const Detail::Storage<T>*
    PropertySet::Resolve(const PropertyId id) const noexcept
    {
        const Detail::StorageBase* storage = ResolveStorage(id);
        if (storage == nullptr || storage->Type() != TypeToken<T>() ||
            storage->TypeName() != TypeNameOf<T>())
        {
            return nullptr;
        }
        return static_cast<const Detail::Storage<T>*>(storage);
    }

    template <PropertyValue T>
    Property<T> PropertySet::Add(std::string name, T defaultValue)
    {
        if (Contains(name))
            return {};

        EnsureRevisionState();
        MarkModified();

        auto storage = std::make_unique<Detail::Storage<T>>(
            m_Revisions, std::move(name), std::move(defaultValue));
        storage->MarkModified();
        storage->Resize(m_Size);

        const PropertyId id = AcquireSlot();
        if (!id.IsValid())
            return {};

        assert(id.Slot < m_Slots.size());
        Slot& slot = m_Slots[id.Slot];
        m_NameIndex.emplace(std::string(storage->Name()), id);
        slot.Value = std::move(storage);
        return Property<T>{this, id};
    }

    template <PropertyValue T>
    Property<T> PropertySet::Get(const std::string_view name)
    {
        const std::optional<PropertyId> id = Find(name);
        return id.has_value() ? Get<T>(*id) : Property<T>{};
    }

    template <PropertyValue T>
    ConstProperty<T> PropertySet::Get(const std::string_view name) const
    {
        const std::optional<PropertyId> id = Find(name);
        return id.has_value() ? Get<T>(*id) : ConstProperty<T>{};
    }

    template <PropertyValue T>
    Property<T> PropertySet::Get(const PropertyId id)
    {
        return Resolve<T>(id) != nullptr
                   ? Property<T>{this, id}
                   : Property<T>{};
    }

    template <PropertyValue T>
    ConstProperty<T> PropertySet::Get(const PropertyId id) const
    {
        return Resolve<T>(id) != nullptr
                   ? ConstProperty<T>{this, id}
                   : ConstProperty<T>{};
    }

    template <PropertyValue T>
    Property<T> PropertySet::GetOrAdd(std::string name, T defaultValue)
    {
        if (Property<T> property = Get<T>(name))
            return property;
        return Add<T>(std::move(name), std::move(defaultValue));
    }

    template <PropertyValue T>
    bool PropertySet::Remove(Property<T>& property)
    {
        if (property.m_Owner != this)
            return false;

        const PropertyId id = property.m_Id;
        property.Reset();
        return Remove(id);
    }

    template <PropertyValue T>
    PropertyId Property<T>::Id() const noexcept
    {
        return m_Id;
    }

    template <PropertyValue T>
    bool Property<T>::IsValid() const noexcept
    {
        return Resolve() != nullptr;
    }

    template <PropertyValue T>
    Property<T>::operator bool() const noexcept
    {
        return IsValid();
    }

    template <PropertyValue T>
    std::string_view Property<T>::Name() const noexcept
    {
        const Detail::Storage<T>* storage = Resolve();
        return storage != nullptr ? storage->Name() : std::string_view{};
    }

    template <PropertyValue T>
    std::size_t Property<T>::Size() const noexcept
    {
        const Detail::Storage<T>* storage = Resolve();
        return storage != nullptr ? storage->Values().size() : 0u;
    }

    template <PropertyValue T>
    PropertyRevision Property<T>::Revision() const noexcept
    {
        const Detail::Storage<T>* storage = Resolve();
        return storage != nullptr ? storage->Revision() : 0u;
    }

    template <PropertyValue T>
    void Property<T>::MarkModified() noexcept
    {
        if (Detail::Storage<T>* storage = Resolve())
            storage->MarkModified();
    }

    template <PropertyValue T>
    decltype(auto) Property<T>::operator[](const std::size_t index)
    {
        Detail::Storage<T>* storage = Resolve();
        assert(storage != nullptr);
        std::vector<T>& values = storage->Values();
        assert(index < values.size());
        return values[index];
    }

    template <PropertyValue T>
    decltype(auto) Property<T>::operator[](const std::size_t index) const
    {
        const Detail::Storage<T>* storage = Resolve();
        assert(storage != nullptr);
        const std::vector<T>& values = storage->Values();
        assert(index < values.size());
        return values[index];
    }

    template <PropertyValue T>
    bool Property<T>::Assign(std::vector<T> values)
    {
        Detail::Storage<T>* storage = Resolve();
        return storage != nullptr && storage->Assign(std::move(values));
    }

    template <PropertyValue T>
    const std::vector<T>& Property<T>::Vector() const noexcept
    {
        const Detail::Storage<T>* storage = Resolve();
        assert(storage != nullptr);
        return storage->Values();
    }

    template <PropertyValue T>
    std::span<T> Property<T>::Span() noexcept
        requires(!std::same_as<T, bool>)
    {
        Detail::Storage<T>* storage = Resolve();
        assert(storage != nullptr);
        return std::span<T>{storage->Values()};
    }

    template <PropertyValue T>
    std::span<const T> Property<T>::Span() const noexcept
        requires(!std::same_as<T, bool>)
    {
        return std::span<const T>{Vector()};
    }

    template <PropertyValue T>
    T* Property<T>::Data() noexcept
        requires(!std::same_as<T, bool>)
    {
        return Span().data();
    }

    template <PropertyValue T>
    const T* Property<T>::Data() const noexcept
        requires(!std::same_as<T, bool>)
    {
        return Vector().data();
    }

    template <PropertyValue T>
    void Property<T>::Reset() noexcept
    {
        m_Owner = nullptr;
        m_Id = {};
    }

    template <PropertyValue T>
    Property<T>::Property(PropertySet* owner, const PropertyId id) noexcept
        : m_Owner(owner), m_Id(id)
    {
    }

    template <PropertyValue T>
    Detail::Storage<T>* Property<T>::Resolve() noexcept
    {
        return m_Owner != nullptr
                   ? m_Owner->Resolve<T>(m_Id)
                   : nullptr;
    }

    template <PropertyValue T>
    const Detail::Storage<T>* Property<T>::Resolve() const noexcept
    {
        const PropertySet* owner = m_Owner;
        return owner != nullptr
                   ? owner->Resolve<T>(m_Id)
                   : nullptr;
    }

    template <PropertyValue T>
    ConstProperty<T>::ConstProperty(const Property<T>& property) noexcept
        : m_Owner(property.m_Owner), m_Id(property.m_Id)
    {
    }

    template <PropertyValue T>
    PropertyId ConstProperty<T>::Id() const noexcept
    {
        return m_Id;
    }

    template <PropertyValue T>
    bool ConstProperty<T>::IsValid() const noexcept
    {
        return Resolve() != nullptr;
    }

    template <PropertyValue T>
    ConstProperty<T>::operator bool() const noexcept
    {
        return IsValid();
    }

    template <PropertyValue T>
    std::string_view ConstProperty<T>::Name() const noexcept
    {
        const Detail::Storage<T>* storage = Resolve();
        return storage != nullptr ? storage->Name() : std::string_view{};
    }

    template <PropertyValue T>
    std::size_t ConstProperty<T>::Size() const noexcept
    {
        const Detail::Storage<T>* storage = Resolve();
        return storage != nullptr ? storage->Values().size() : 0u;
    }

    template <PropertyValue T>
    PropertyRevision ConstProperty<T>::Revision() const noexcept
    {
        const Detail::Storage<T>* storage = Resolve();
        return storage != nullptr ? storage->Revision() : 0u;
    }

    template <PropertyValue T>
    decltype(auto) ConstProperty<T>::operator[](const std::size_t index) const
    {
        const Detail::Storage<T>* storage = Resolve();
        assert(storage != nullptr);
        const std::vector<T>& values = storage->Values();
        assert(index < values.size());
        return values[index];
    }

    template <PropertyValue T>
    const std::vector<T>& ConstProperty<T>::Vector() const noexcept
    {
        const Detail::Storage<T>* storage = Resolve();
        assert(storage != nullptr);
        return storage->Values();
    }

    template <PropertyValue T>
    std::span<const T> ConstProperty<T>::Span() const noexcept
        requires(!std::same_as<T, bool>)
    {
        return std::span<const T>{Vector()};
    }

    template <PropertyValue T>
    const T* ConstProperty<T>::Data() const noexcept
        requires(!std::same_as<T, bool>)
    {
        return Vector().data();
    }

    template <PropertyValue T>
    void ConstProperty<T>::Reset() noexcept
    {
        m_Owner = nullptr;
        m_Id = {};
    }

    template <PropertyValue T>
    ConstProperty<T>::ConstProperty(const PropertySet* owner,
                                    const PropertyId id) noexcept
        : m_Owner(owner), m_Id(id)
    {
    }

    template <PropertyValue T>
    const Detail::Storage<T>* ConstProperty<T>::Resolve() const noexcept
    {
        return m_Owner != nullptr
                   ? m_Owner->Resolve<T>(m_Id)
                   : nullptr;
    }

    template <PropertyValue T>
    ConstProperty<T> ConstPropertySet::Get(const std::string_view name) const
    {
        return m_Set != nullptr ? m_Set->Get<T>(name) : ConstProperty<T>{};
    }

    template <PropertyValue T>
    ConstProperty<T> ConstPropertySet::Get(const PropertyId id) const
    {
        return m_Set != nullptr ? m_Set->Get<T>(id) : ConstProperty<T>{};
    }
} // namespace Extrinsic::Core
