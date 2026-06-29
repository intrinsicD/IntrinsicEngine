module;

#include <ostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Geometry.Properties;


namespace Geometry
{
    std::vector<std::string> PropertyRegistry::PropertyNames() const
    {
        std::vector<std::string> out;
        out.reserve(m_Storages.size());
        for (const auto& s : m_Storages)
        {
            if (!s) continue;
            out.emplace_back(std::string(s->Name()));
        }
        return out;
    }

    std::vector<PropertyDescriptor> PropertyRegistry::Descriptors(bool isMutable) const
    {
        std::vector<PropertyDescriptor> out;
        out.reserve(m_Storages.size());
        for (std::size_t i = 0; i < m_Storages.size(); ++i)
        {
            const auto& storage = m_Storages[i];
            if (!storage) continue;
            out.push_back(storage->Describe(i, isMutable));
        }
        return out;
    }

    void PropertyRegistry::Clear()
    {
        m_Size = 0;
        m_Storages.clear();
        m_NameIndex.clear();
    }

    void PropertyRegistry::Reserve(size_t n)
    {
        m_Storages.reserve(n);
    }

    void PropertyRegistry::Resize(size_t n)
    {
        m_Size = n;
        for (auto& storage : m_Storages)
            if (storage) storage->Resize(n);
    }

    void PropertyRegistry::ShrinkToFit()
    {
        for (auto& storage : m_Storages)
            if (storage) storage->ShrinkToFit();
    }

    void PropertyRegistry::PushBack()
    {
        m_Size += 1;
        for (auto& storage : m_Storages)
            if (storage) storage->PushBack();
    }

    void PropertyRegistry::Swap(size_t i0, size_t i1)
    {
        for (auto& storage : m_Storages)
            if (storage) storage->Swap(i0, i1);
    }

    bool PropertyRegistry::Contains(std::string_view name) const
    {
        return Find(name).has_value();
    }

    std::optional<PropertyId> PropertyRegistry::Find(std::string_view name) const
    {
        auto it = m_NameIndex.find(name);
        if (it != m_NameIndex.end()) return it->second;
        return std::nullopt;
    }

    bool PropertyRegistry::IsValidId(PropertyId id) const noexcept
    {
        return id < m_Storages.size() && static_cast<bool>(m_Storages[id]);
    }

    Internal::PropertyStorageBase* PropertyRegistry::Storage(PropertyId id) noexcept
    {
        if (!IsValidId(id))
        {
            return nullptr;
        }
        return m_Storages[id].get();
    }

    const Internal::PropertyStorageBase* PropertyRegistry::Storage(PropertyId id) const noexcept
    {
        if (!IsValidId(id))
        {
            return nullptr;
        }
        return m_Storages[id].get();
    }

    PropertyRegistry::PropertyRegistry(const PropertyRegistry& other) : m_Storages(), m_NameIndex(), m_Size(other.m_Size)
    {
        m_Storages.reserve(other.m_Storages.size());
        for (size_t i = 0; i < other.m_Storages.size(); ++i)
        {
            const auto& storage = other.m_Storages[i];
            if (!storage)
            {
                m_Storages.emplace_back(nullptr);
                continue;
            }
            m_Storages.push_back(storage->Clone());
            m_NameIndex.emplace(std::string(m_Storages.back()->Name()), i);
        }
    }

    PropertyRegistry& PropertyRegistry::operator=(const PropertyRegistry& other)
    {
        if (this == &other) return *this;

        m_Size = other.m_Size;
        m_Storages.clear();
        m_NameIndex.clear();
        m_Storages.reserve(other.m_Storages.size());
        for (size_t i = 0; i < other.m_Storages.size(); ++i)
        {
            const auto& storage = other.m_Storages[i];
            if (!storage)
            {
                m_Storages.emplace_back(nullptr);
                continue;
            }
            m_Storages.push_back(storage->Clone());
            m_NameIndex.emplace(std::string(m_Storages.back()->Name()), i);
        }
        return *this;
    }

    bool PropertyRegistry::Remove(PropertyId id)
    {
        if (id >= m_Storages.size())
        {
            return false;
        }

        if (!m_Storages[id])
        {
            return false;
        }

        // Remove from name index before clearing storage.
        m_NameIndex.erase(std::string(m_Storages[id]->Name()));
        // Preserve IDs by leaving an empty slot.
        m_Storages[id].reset();
        return true;
    }

    std::ostream& operator<<(std::ostream& os, VertexHandle v)
    {
        return os << "Vertex(" << v.Index << ")";
    }

    std::ostream& operator<<(std::ostream& os, HalfedgeHandle h)
    {
        return os << "Halfedge(" << h.Index << ")";
    }

    std::ostream& operator<<(std::ostream& os, EdgeHandle e)
    {
        return os << "Edge(" << e.Index << ")";
    }

    std::ostream& operator<<(std::ostream& os, FaceHandle f)
    {
        return os << "Face(" << f.Index << ")";
    }

    std::ostream& operator<<(std::ostream& os, NodeHandle n)
    {
        return os << "Node(" << n.Index << ")";
    }
}
