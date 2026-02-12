module;

#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

module Core:FeatureRegistry.Impl;

import :FeatureRegistry;
import :Hash;
import :Logging;

namespace Core
{
    // -------------------------------------------------------------------------
    // Registration
    // -------------------------------------------------------------------------

    bool FeatureRegistry::Register(FeatureInfo info, FeatureFactoryFn factory, FeatureDestroyFn destroy)
    {
        assert(factory && "Factory function must not be null");
        assert(destroy && "Destroy function must not be null");
        assert(info.Id.Value != 0 && "Feature ID must not be zero (empty name?)");

        if (FindEntry(info.Id))
        {
            Log::Warn("FeatureRegistry: duplicate registration for '{}'", info.Name);
            return false;
        }

        Log::Info("FeatureRegistry: registered '{}' (category {})",
                  info.Name, static_cast<int>(info.Category));
        m_Entries.push_back({std::move(info), std::move(factory), std::move(destroy)});
        return true;
    }

    // -------------------------------------------------------------------------
    // Unregistration
    // -------------------------------------------------------------------------

    bool FeatureRegistry::Unregister(Hash::StringID id)
    {
        for (auto it = m_Entries.begin(); it != m_Entries.end(); ++it)
        {
            if (it->Info.Id == id)
            {
                Log::Info("FeatureRegistry: unregistered '{}'", it->Info.Name);
                m_Entries.erase(it);
                return true;
            }
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    const FeatureInfo* FeatureRegistry::Find(Hash::StringID id) const
    {
        const Entry* entry = FindEntry(id);
        return entry ? &entry->Info : nullptr;
    }

    std::vector<const FeatureInfo*> FeatureRegistry::GetByCategory(FeatureCategory category) const
    {
        std::vector<const FeatureInfo*> result;
        for (const auto& entry : m_Entries)
        {
            if (entry.Info.Category == category)
            {
                result.push_back(&entry.Info);
            }
        }
        return result;
    }

    std::vector<const FeatureInfo*> FeatureRegistry::GetEnabled(FeatureCategory category) const
    {
        std::vector<const FeatureInfo*> result;
        for (const auto& entry : m_Entries)
        {
            if (entry.Info.Category == category && entry.Info.Enabled)
            {
                result.push_back(&entry.Info);
            }
        }
        return result;
    }

    // -------------------------------------------------------------------------
    // Enable / Disable
    // -------------------------------------------------------------------------

    bool FeatureRegistry::SetEnabled(Hash::StringID id, bool enabled)
    {
        Entry* entry = FindEntry(id);
        if (!entry) return false;
        entry->Info.Enabled = enabled;
        return true;
    }

    bool FeatureRegistry::IsEnabled(Hash::StringID id) const
    {
        const Entry* entry = FindEntry(id);
        return entry && entry->Info.Enabled;
    }

    // -------------------------------------------------------------------------
    // Instance Creation
    // -------------------------------------------------------------------------

    void* FeatureRegistry::CreateInstance(Hash::StringID id) const
    {
        const Entry* entry = FindEntry(id);
        if (!entry || !entry->Info.Enabled || !entry->Factory)
            return nullptr;
        return entry->Factory();
    }

    void FeatureRegistry::DestroyInstance(Hash::StringID id, void* instance) const
    {
        if (!instance) return;
        const Entry* entry = FindEntry(id);
        if (entry && entry->Destroy)
        {
            entry->Destroy(instance);
        }
    }

    // -------------------------------------------------------------------------
    // Metadata
    // -------------------------------------------------------------------------

    size_t FeatureRegistry::Count() const
    {
        return m_Entries.size();
    }

    size_t FeatureRegistry::CountByCategory(FeatureCategory category) const
    {
        size_t count = 0;
        for (const auto& entry : m_Entries)
        {
            if (entry.Info.Category == category)
                ++count;
        }
        return count;
    }

    void FeatureRegistry::Clear()
    {
        m_Entries.clear();
    }

    // -------------------------------------------------------------------------
    // Internal
    // -------------------------------------------------------------------------

    const FeatureRegistry::Entry* FeatureRegistry::FindEntry(Hash::StringID id) const
    {
        for (const auto& entry : m_Entries)
        {
            if (entry.Info.Id == id)
                return &entry;
        }
        return nullptr;
    }

    FeatureRegistry::Entry* FeatureRegistry::FindEntry(Hash::StringID id)
    {
        for (auto& entry : m_Entries)
        {
            if (entry.Info.Id == id)
                return &entry;
        }
        return nullptr;
    }
}
