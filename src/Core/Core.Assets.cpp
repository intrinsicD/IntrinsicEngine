module;
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
#include <map>
#include <entt/entity/registry.hpp>

module Core:Assets.Impl;
import :Assets;

namespace Core::Assets
{
    void AssetManager::EnqueueReadyEvent(AssetHandle handle)
    {
        std::lock_guard lock(m_EventQueueMutex);
        m_ReadyQueue.push_back(handle);
    }

    void AssetManager::Update()
    {
        std::vector<AssetHandle> events;
        {
            std::lock_guard lock(m_EventQueueMutex);
            if (m_ReadyQueue.empty()) return;
            events.swap(m_ReadyQueue);
        }

        for (const auto& handle : events)
        {
            // 1. Process One-Shot Listeners
            std::vector<AssetCallback> runOneShots;
            {
                std::unique_lock lock(m_Mutex);
                auto it = m_OneShotListeners.find(handle);
                if (it != m_OneShotListeners.end())
                {
                    runOneShots = std::move(it->second);
                    m_OneShotListeners.erase(it);
                }
            }
            // Execute OUTSIDE lock
            for (const auto& cb : runOneShots) cb(handle);

            // 2. Process Persistent Listeners
            std::vector<AssetCallback> runPersistent;
            {
                std::shared_lock lock(m_Mutex);

                // Check if any listeners exist for this asset
                if (m_PersistentListeners.contains(handle))
                {
                    const auto& listeners = m_PersistentListeners.at(handle);
                    runPersistent.reserve(listeners.size());

                    // Copy valid callbacks only
                    for(const auto& [id, cb] : listeners) {
                        if(cb) runPersistent.push_back(cb);
                    }
                }
            }
            // Execute OUTSIDE lock (Safe to call Load() recursively now)
            for (const auto& cb : runPersistent) cb(handle);
        }
    }

    void AssetManager::RequestNotify(AssetHandle handle, AssetCallback callback)
    {
        std::unique_lock lock(m_Mutex);
        if (!m_Registry.valid(handle.ID)) return;

        // If already ready, fire immediately
        if (m_Registry.get<AssetInfo>(handle.ID).State == LoadState::Ready)
        {
            lock.unlock(); // Unlock before callback to prevent deadlocks
            callback(handle);
            return;
        }

        // Register for future
        m_OneShotListeners[handle].push_back(callback);
    }

    void AssetManager::FinalizeLoad(AssetHandle handle)
    {
        std::unique_lock lock(m_Mutex);
        if (m_Registry.valid(handle.ID))
        {
            auto& info = m_Registry.get<AssetInfo>(handle.ID);
            if (info.State == LoadState::Processing)
            {
                info.State = LoadState::Ready;
                EnqueueReadyEvent(handle);
                Log::Debug("Asset finalization signaled for: {}", info.Name);
            }
        }
    }

    void AssetManager::MoveToProcessing(AssetHandle handle)
    {
        std::unique_lock lock(m_Mutex);
        if (m_Registry.valid(handle.ID))
        {
            m_Registry.get<AssetInfo>(handle.ID).State = LoadState::Processing;
        }
    }

    static std::atomic<uint32_t> s_ListenerIdCounter{1};

    ListenerHandle AssetManager::Listen(AssetHandle handle, AssetCallback callback)
    {
        std::unique_lock lock(m_Mutex);
        if (!m_Registry.valid(handle.ID)) return {0};

        uint32_t id = s_ListenerIdCounter++;

        m_PersistentListeners[handle][id] = std::move(callback);

        // Optional: Fire immediately if valid?
        // For hot reloading textures, we usually want to know if it's already there to set it up initially.
        if (m_Registry.get<AssetInfo>(handle.ID).State == LoadState::Ready)
        {
            lock.unlock();
            callback(handle);
        }
        return {id};
    }

    void AssetManager::Unlisten(AssetHandle handle, ListenerHandle listener)
    {
        std::unique_lock lock(m_Mutex);
        if (m_PersistentListeners.contains(handle))
        {
            m_PersistentListeners[handle].erase(listener.ID);
        }
    }

    LoadState AssetManager::GetState(AssetHandle handle)
    {
        std::shared_lock lock(m_Mutex);
        if (!m_Registry.valid(handle.ID)) return LoadState::Unloaded;
        return m_Registry.get<AssetInfo>(handle.ID).State;
    }

    void AssetManager::Clear()
    {
        std::unique_lock lock(m_Mutex);
        m_Registry.clear();
        m_Lookup.clear();
        m_OneShotListeners.clear();
        m_PersistentListeners.clear();
        {
            std::lock_guard qLock(m_EventQueueMutex);
            m_ReadyQueue.clear();
        }
    }

    // Helper to get color based on state
    static ImVec4 GetStateColor(LoadState state)
    {
        switch (state)
        {
        case LoadState::Ready: return {0.2f, 0.8f, 0.2f, 1.0f}; // Green
        case LoadState::Loading: return {0.8f, 0.8f, 0.2f, 1.0f}; // Yellow
        case LoadState::Processing: return {0.2f, 0.8f, 0.8f, 1.0f};
        case LoadState::Failed: return {0.8f, 0.2f, 0.2f, 1.0f}; // Red
        case LoadState::Unloaded: return {0.5f, 0.5f, 0.5f, 1.0f}; // Grey
        }
        return {1, 1, 1, 1};
    }

    static const char* GetStateString(LoadState state)
    {
        switch (state)
        {
        case LoadState::Ready: return "READY";
        case LoadState::Loading: return "LOADING";
        case LoadState::Processing: return "PROCESSING";
        case LoadState::Failed: return "FAILED";
        case LoadState::Unloaded: return "UNLOADED";
        }
        return "UNKNOWN";
    }

    void AssetManager::AssetsUiPanel()
    {
        // 1. HEADER & STATISTICS
        // ---------------------------------------------------------------------
        // Quick glance counters
        int countReady = 0;
        int countLoading = 0;
        int countFailed = 0;

        // We do a quick pass or maintain these counters atomically in the manager
        // For UI purposes, iterating the view is fast enough for <10k entities
        auto view = m_Registry.view<AssetInfo>();
        for (auto [entity, info] : view.each())
        {
            if (info.State == LoadState::Ready) countReady++;
            else if (info.State == LoadState::Loading) countLoading++;
            else if (info.State == LoadState::Failed) countFailed++;
        }

        ImGui::Text("Total: %d", (int)view.size());
        ImGui::SameLine();
        ImGui::TextColored({0.2f, 0.8f, 0.2f, 1.0f}, "Ready: %d", countReady);
        ImGui::SameLine();
        ImGui::TextColored({0.8f, 0.8f, 0.2f, 1.0f}, "Loading: %d", countLoading);
        ImGui::SameLine();
        ImGui::TextColored({0.8f, 0.2f, 0.2f, 1.0f}, "Failed: %d", countFailed);

        ImGui::Separator();

        // 2. SEARCH & FILTER
        // ---------------------------------------------------------------------
        static char searchBuffer[128] = "";
        ImGui::InputTextWithHint("##SearchAssets", "Search by Name or Path...", searchBuffer, sizeof(searchBuffer));

        static bool showReady = true;
        static bool showLoading = true;
        static bool showFailed = true;

        ImGui::SameLine();
        ImGui::Checkbox("Ready", &showReady);
        ImGui::SameLine();
        ImGui::Checkbox("Loading", &showLoading);
        ImGui::SameLine();
        ImGui::Checkbox("Failed", &showFailed);

        // 3. ASSET TABLE
        // ---------------------------------------------------------------------
        ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable;

        // Determine height (leave space for bottom status bar if needed)
        if (ImGui::BeginTable("AssetTable", 5, flags))
        {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Name / Path", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableHeadersRow();

            // Gather filtered list for the Clipper
            std::vector<entt::entity> filteredEntities;
            filteredEntities.reserve(view.size());

            for (auto [entity, info] : view.each())
            {
                // State Filter
                if (info.State == LoadState::Ready && !showReady) continue;
                if (info.State == LoadState::Loading && !showLoading) continue;
                if (info.State == LoadState::Failed && !showFailed) continue;

                // Search String Filter
                if (searchBuffer[0] != '\0')
                {
                    // Case-insensitive check (simplified)
                    std::string searchStr = searchBuffer;
                    std::string nameStr = info.Name;
                    // Note: In production, use a faster, case-insensitive substring search
                    if (nameStr.find(searchStr) == std::string::npos) continue;
                }

                filteredEntities.push_back(entity);
            }

            // 4. LIST CLIPPER (Performance for large lists)
            // -----------------------------------------------------------------
            ImGuiListClipper clipper;
            clipper.Begin((int)filteredEntities.size());

            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                {
                    entt::entity entity = filteredEntities[i];
                    auto& info = m_Registry.get<AssetInfo>(entity);
                    auto* reloader = m_Registry.try_get<AssetReloader>(entity);

                    ImGui::PushID((int)entity);
                    ImGui::TableNextRow();

                    // --- Col 0: ID ---
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", (uint32_t)entity);

                    // --- Col 1: State ---
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(GetStateColor(info.State), "%s", GetStateString(info.State));
                    if (ImGui::IsItemHovered() && info.State == LoadState::Failed)
                    {
                        ImGui::SetTooltip("Asset failed to load. Check logs.");
                    }

                    // --- Col 2: Type ---
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(info.Type.c_str());

                    // --- Col 3: Name / Path ---
                    ImGui::TableSetColumnIndex(3);

                    // Drag Source Logic: Drag asset from UI into Scene!
                    ImGui::Selectable(info.Name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
                    if (ImGui::BeginDragDropSource())
                    {
                        // Pass the AssetHandle (which wraps the entity)
                        AssetHandle handle{entity};
                        // We pass the handle ID.
                        // The receiver (Inspector) must know this is an "ASSET_HANDLE" payload.
                        ImGui::SetDragDropPayload("ASSET_HANDLE", &handle, sizeof(AssetHandle));

                        // Preview
                        ImGui::Text("Assign %s", info.Name.c_str());
                        ImGui::EndDragDropSource();
                    }

                    if (ImGui::IsItemHovered())
                    {
                        // If we have source path, show it in tooltip
                        if (auto* src = m_Registry.try_get<AssetSource>(entity))
                        {
                            ImGui::SetTooltip("%s", src->FilePath.string().c_str());
                        }
                    }

                    // --- Col 4: Actions ---
                    ImGui::TableSetColumnIndex(4);

                    bool canReload = (reloader != nullptr) && (info.State != LoadState::Loading);

                    if (!canReload) ImGui::BeginDisabled();
                    if (ImGui::Button("Reload"))
                    {
                        if (reloader)
                        {
                            Log::Info("Manual Reload requested for: {}", info.Name);
                            reloader->ReloadAction(); // <--- MAGIC HAPPENS HERE
                        }
                    }
                    if (!canReload) ImGui::EndDisabled();

                    ImGui::PopID();
                }
            }

            ImGui::EndTable();
        }
    }
}
