module;

#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Geometry:Properties.Impl;

import :Properties;

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

    PropertyRegistry::PropertyRegistry(const PropertyRegistry& other) : m_Storages(), m_Size(other.m_Size)
    {
        m_Storages.reserve(other.m_Storages.size());
        for (const auto& storage : other.m_Storages)
        {
            if (!storage)
            {
                m_Storages.emplace_back(nullptr);
                continue;
            }
            m_Storages.push_back(storage->Clone());
        }
    }

    PropertyRegistry& PropertyRegistry::operator=(const PropertyRegistry& other)
    {
        if (this == &other) return *this;

        m_Size = other.m_Size;
        m_Storages.clear();
        m_Storages.reserve(other.m_Storages.size());
        for (const auto& storage : other.m_Storages)
        {
            if (!storage)
            {
                m_Storages.emplace_back(nullptr);
                continue;
            }
            m_Storages.push_back(storage->Clone());
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
