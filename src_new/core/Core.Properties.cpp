// Implements revision and ownership epochs plus value semantics for the
// heterogeneous property storage exposed by Core.Properties.
module;

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>

module Core.Properties;

namespace Extrinsic::Core
{
    namespace
    {
        std::atomic<std::uint64_t> g_NextPropertyRevision{1u};
        std::atomic<std::uint64_t> g_NextPropertyOwnerToken{1u};

        [[nodiscard]] std::uint64_t NextNonzeroToken(
            std::atomic<std::uint64_t>& source) noexcept
        {
            std::uint64_t token{};
            do
            {
                token = source.fetch_add(1u, std::memory_order_relaxed);
            }
            while (token == 0u);
            return token;
        }

        [[nodiscard]] PropertyRevision NextPropertyRevision() noexcept
        {
            return NextNonzeroToken(g_NextPropertyRevision);
        }

        [[nodiscard]] std::uint64_t NextPropertyOwnerToken() noexcept
        {
            return NextNonzeroToken(g_NextPropertyOwnerToken);
        }
    } // namespace

    void Detail::RevisionState::MarkModified() noexcept
    {
        if (m_DirtySinceObservation)
            return;

        m_Current = NextPropertyRevision();
        m_DirtySinceObservation = true;
    }

    PropertyRevision Detail::RevisionState::Observe() const noexcept
    {
        m_DirtySinceObservation = false;
        return m_Current;
    }

    PropertyRevision Detail::RevisionState::Current() const noexcept
    {
        return m_Current;
    }

    Detail::StorageBase::StorageBase(
        std::shared_ptr<RevisionState> revisions)
        : m_Revisions(std::move(revisions))
    {
    }

    void Detail::StorageBase::MarkModified() noexcept
    {
        if (m_Revisions == nullptr)
            return;

        m_Revisions->MarkModified();
        m_ContentRevision = m_Revisions->Current();
    }

    PropertyRevision Detail::StorageBase::Revision() const noexcept
    {
        if (m_Revisions != nullptr)
            (void)m_Revisions->Observe();
        return m_ContentRevision;
    }

    PropertySet::PropertySet()
        : m_Revisions(std::make_shared<Detail::RevisionState>())
    {
        InvalidateAllHandles();
    }

    PropertySet::PropertySet(const PropertySet& other)
        : m_Revisions(std::make_shared<Detail::RevisionState>()),
          m_Size(other.m_Size)
    {
        InvalidateAllHandles();
        m_Slots.resize(other.m_Slots.size());
        m_NameIndex.reserve(other.m_NameIndex.size());

        for (std::size_t index = 0u; index < other.m_Slots.size(); ++index)
        {
            const Slot& source = other.m_Slots[index];
            Slot& destination = m_Slots[index];
            if (source.Value == nullptr)
                continue;

            destination.Value = source.Value->Clone(m_Revisions);
            const PropertyId id{
                .Slot = static_cast<std::uint32_t>(index),
                .OwnerToken = m_OwnerToken,
            };
            m_NameIndex.emplace(
                std::string{destination.Value->Name()}, id);
        }

        if (m_Size != 0u && m_NameIndex.empty())
            MarkModified();
    }

    PropertySet::PropertySet(PropertySet&& other) noexcept
        : m_Revisions(std::move(other.m_Revisions)),
          m_Slots(std::move(other.m_Slots)),
          m_NameIndex(std::move(other.m_NameIndex)),
          m_Size(std::exchange(other.m_Size, 0u)),
          m_OwnerToken(std::exchange(other.m_OwnerToken, 0u))
    {
        other.m_Slots.clear();
        other.m_NameIndex.clear();
        other.m_Revisions.reset();
        other.InvalidateAllHandles();
        RebaseRevisions();
    }

    PropertySet& PropertySet::operator=(const PropertySet& other)
    {
        if (this == &other)
            return *this;

        PropertySet replacement{other};
        m_Revisions = std::move(replacement.m_Revisions);
        m_Slots = std::move(replacement.m_Slots);
        m_NameIndex = std::move(replacement.m_NameIndex);
        m_Size = replacement.m_Size;
        m_OwnerToken = std::exchange(replacement.m_OwnerToken, 0u);
        return *this;
    }

    PropertySet& PropertySet::operator=(PropertySet&& other) noexcept
    {
        if (this == &other)
            return *this;

        m_Revisions = std::move(other.m_Revisions);
        m_Slots = std::move(other.m_Slots);
        m_NameIndex = std::move(other.m_NameIndex);
        m_Size = std::exchange(other.m_Size, 0u);
        m_OwnerToken = std::exchange(other.m_OwnerToken, 0u);

        other.m_Slots.clear();
        other.m_NameIndex.clear();
        other.m_Revisions.reset();
        other.InvalidateAllHandles();
        RebaseRevisions();
        return *this;
    }

    std::size_t PropertySet::Size() const noexcept
    {
        return m_Size;
    }

    std::size_t PropertySet::PropertyCount() const noexcept
    {
        return m_NameIndex.size();
    }

    bool PropertySet::Empty() const noexcept
    {
        return m_Size == 0u;
    }

    PropertyRevision PropertySet::Revision() const noexcept
    {
        return m_Revisions != nullptr ? m_Revisions->Observe() : 0u;
    }

    std::optional<PropertyRevision> PropertySet::FindPropertyRevision(
        const std::string_view name) const noexcept
    {
        const std::optional<PropertyId> id = Find(name);
        if (!id.has_value())
            return std::nullopt;

        const Detail::StorageBase* storage = ResolveStorage(*id);
        return storage != nullptr
                   ? std::optional<PropertyRevision>{storage->Revision()}
                   : std::nullopt;
    }

    bool PropertySet::Contains(const std::string_view name) const
    {
        return m_NameIndex.find(name) != m_NameIndex.end();
    }

    std::optional<PropertyId> PropertySet::Find(
        const std::string_view name) const
    {
        const auto found = m_NameIndex.find(name);
        return found != m_NameIndex.end()
                   ? std::optional<PropertyId>{found->second}
                   : std::nullopt;
    }

    std::vector<std::string> PropertySet::PropertyNames() const
    {
        std::vector<std::string> names;
        names.reserve(m_NameIndex.size());
        for (const Slot& slot : m_Slots)
        {
            if (slot.Value != nullptr)
                names.emplace_back(slot.Value->Name());
        }
        return names;
    }

    std::vector<PropertyDescriptor> PropertySet::Descriptors()
    {
        return DescribeAll(true);
    }

    std::vector<PropertyDescriptor> PropertySet::Descriptors() const
    {
        return DescribeAll(false);
    }

    void PropertySet::Resize(const std::size_t count)
    {
        if (m_Size == count)
            return;

        MarkModified();
        for (Slot& slot : m_Slots)
        {
            if (slot.Value != nullptr)
                slot.Value->Resize(count);
        }
        m_Size = count;
    }

    void PropertySet::ReserveElements(const std::size_t count)
    {
        for (Slot& slot : m_Slots)
        {
            if (slot.Value != nullptr)
                slot.Value->Reserve(count);
        }
    }

    void PropertySet::PushBack()
    {
        MarkModified();
        for (Slot& slot : m_Slots)
        {
            if (slot.Value != nullptr)
                slot.Value->PushBack();
        }
        ++m_Size;
    }

    void PropertySet::Swap(const std::size_t a, const std::size_t b)
    {
        assert(a < m_Size);
        assert(b < m_Size);
        if (a >= m_Size || b >= m_Size || a == b)
            return;

        MarkModified();
        for (Slot& slot : m_Slots)
        {
            if (slot.Value != nullptr)
                slot.Value->Swap(a, b);
        }
    }

    void PropertySet::ShrinkToFit()
    {
        for (Slot& slot : m_Slots)
        {
            if (slot.Value != nullptr)
                slot.Value->ShrinkToFit();
        }
    }

    void PropertySet::Clear()
    {
        if (m_Size == 0u && m_Slots.empty())
            return;

        MarkModified();
        InvalidateAllHandles();
        m_Slots.clear();
        m_NameIndex.clear();
        m_Size = 0u;
    }

    bool PropertySet::Remove(const PropertyId id)
    {
        Detail::StorageBase* storage = ResolveStorage(id);
        if (storage == nullptr)
            return false;

        const auto name = m_NameIndex.find(storage->Name());
        if (name != m_NameIndex.end())
            m_NameIndex.erase(name);

        MarkModified();
        ReleaseSlot(id);
        return true;
    }

    void PropertySet::EnsureRevisionState()
    {
        if (m_Revisions == nullptr)
            m_Revisions = std::make_shared<Detail::RevisionState>();
    }

    void PropertySet::MarkModified()
    {
        EnsureRevisionState();
        m_Revisions->MarkModified();
    }

    void PropertySet::RebaseRevisions()
    {
        if (m_Size == 0u && m_NameIndex.empty())
            return;

        EnsureRevisionState();
        (void)m_Revisions->Observe();
        MarkModified();
        for (Slot& slot : m_Slots)
        {
            if (slot.Value != nullptr)
                slot.Value->MarkModified();
        }
    }

    void PropertySet::InvalidateAllHandles() noexcept
    {
        m_OwnerToken = NextPropertyOwnerToken();
    }

    PropertyId PropertySet::AcquireSlot()
    {
        if (m_Slots.size() >= PropertyId::InvalidSlot)
            return {};

        const auto index = static_cast<std::uint32_t>(m_Slots.size());
        m_Slots.emplace_back();
        return PropertyId{
            .Slot = index,
            .OwnerToken = m_OwnerToken,
        };
    }

    void PropertySet::ReleaseSlot(const PropertyId id) noexcept
    {
        if (!id.IsValid() || id.Slot >= m_Slots.size())
            return;

        Slot& slot = m_Slots[id.Slot];
        if (id.OwnerToken != m_OwnerToken || slot.Value == nullptr)
            return;

        slot.Value.reset();
    }

    Detail::StorageBase* PropertySet::ResolveStorage(
        const PropertyId id) noexcept
    {
        if (!id.IsValid() || id.OwnerToken != m_OwnerToken ||
            id.Slot >= m_Slots.size())
        {
            return nullptr;
        }

        Slot& slot = m_Slots[id.Slot];
        return slot.Value.get();
    }

    const Detail::StorageBase* PropertySet::ResolveStorage(
        const PropertyId id) const noexcept
    {
        if (!id.IsValid() || id.OwnerToken != m_OwnerToken ||
            id.Slot >= m_Slots.size())
        {
            return nullptr;
        }

        const Slot& slot = m_Slots[id.Slot];
        return slot.Value.get();
    }

    std::vector<PropertyDescriptor> PropertySet::DescribeAll(
        const bool mutableAccess) const
    {
        std::vector<PropertyDescriptor> descriptors;
        descriptors.reserve(m_NameIndex.size());
        for (std::size_t index = 0u; index < m_Slots.size(); ++index)
        {
            const Slot& slot = m_Slots[index];
            if (slot.Value == nullptr)
                continue;

            descriptors.push_back(slot.Value->Describe(
                PropertyId{
                    .Slot = static_cast<std::uint32_t>(index),
                    .OwnerToken = m_OwnerToken,
                },
                mutableAccess));
        }
        return descriptors;
    }

    ConstPropertySet::ConstPropertySet(const PropertySet& set) noexcept
        : m_Set(&set)
    {
    }

    bool ConstPropertySet::IsValid() const noexcept
    {
        return m_Set != nullptr;
    }

    ConstPropertySet::operator bool() const noexcept
    {
        return IsValid();
    }

    std::size_t ConstPropertySet::Size() const noexcept
    {
        return m_Set != nullptr ? m_Set->Size() : 0u;
    }

    std::size_t ConstPropertySet::PropertyCount() const noexcept
    {
        return m_Set != nullptr ? m_Set->PropertyCount() : 0u;
    }

    bool ConstPropertySet::Empty() const noexcept
    {
        return m_Set == nullptr || m_Set->Empty();
    }

    PropertyRevision ConstPropertySet::Revision() const noexcept
    {
        return m_Set != nullptr ? m_Set->Revision() : 0u;
    }

    std::optional<PropertyRevision> ConstPropertySet::FindPropertyRevision(
        const std::string_view name) const noexcept
    {
        return m_Set != nullptr ? m_Set->FindPropertyRevision(name)
                                : std::nullopt;
    }

    bool ConstPropertySet::Contains(const std::string_view name) const
    {
        return m_Set != nullptr && m_Set->Contains(name);
    }

    std::optional<PropertyId> ConstPropertySet::Find(
        const std::string_view name) const
    {
        return m_Set != nullptr ? m_Set->Find(name) : std::nullopt;
    }

    std::vector<std::string> ConstPropertySet::PropertyNames() const
    {
        return m_Set != nullptr ? m_Set->PropertyNames()
                                : std::vector<std::string>{};
    }

    std::vector<PropertyDescriptor> ConstPropertySet::Descriptors() const
    {
        return m_Set != nullptr ? m_Set->Descriptors()
                                : std::vector<PropertyDescriptor>{};
    }

    void ConstPropertySet::Reset() noexcept
    {
        m_Set = nullptr;
    }
} // namespace Extrinsic::Core
