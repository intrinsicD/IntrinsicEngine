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
        AssetCallback fireCallback{};
        {
            std::unique_lock lock(m_Mutex);
            if (!m_Registry.valid(handle.ID))
                return;

            // If already ready, fire immediately outside the lock.
            if (m_Registry.get<AssetInfo>(handle.ID).State == LoadState::Ready)
            {
                fireCallback = std::move(callback);
            }
            else
            {
                // Register for future
                m_OneShotListeners[handle].push_back(std::move(callback));
            }
        }

        if (fireCallback)
            fireCallback(handle);
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
        AssetCallback fireCallback{};
        ListenerHandle out{0};
        {
            std::unique_lock lock(m_Mutex);
            if (!m_Registry.valid(handle.ID))
                return {0};

            const uint32_t id = s_ListenerIdCounter++;
            out = {id};

            // Keep the persistent listener registered, then snapshot for immediate fire if ready.
            m_PersistentListeners[handle][id] = callback;

            if (m_Registry.get<AssetInfo>(handle.ID).State == LoadState::Ready)
                fireCallback = std::move(callback);
        }

        if (fireCallback)
            fireCallback(handle);

        return out;
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
        // 1. Snapshot entities to destroy and clear auxiliary maps under lock.
        std::vector<entt::entity> toDestroy;
        {
            std::unique_lock lock(m_Mutex);
            m_Lookup.clear();
            m_OneShotListeners.clear();
            m_PersistentListeners.clear();

            auto view = m_Registry.view<AssetInfo>();
            toDestroy.reserve(view.size_hint());
            for (auto entity : view)
                toDestroy.push_back(entity);
        }

        // 2. Destroy entities one-by-one WITHOUT holding the main mutex.
        // Destructors (e.g. ~Material -> MaterialSystem::Destroy -> AssetManager::Unlisten)
        // can safely re-acquire m_Mutex because we released it above.
        for (auto entity : toDestroy)
        {
            if (m_Registry.valid(entity))
                m_Registry.destroy(entity);
        }

        // 3. Final registry cleanup (should be empty; ensures no orphaned entities remain).
        m_Registry.clear();

        // 4. Drain event queue
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
        // 1. SNAPSHOT UNDER LOCK
        // ---------------------------------------------------------------------
        // Snapshot asset state to avoid racing with background loader threads that
        // modify m_Registry under lock in FinalizeLoad() / Reload().
        struct AssetSnapshot
        {
            entt::entity Entity;
            AssetInfo Info;
            std::string SourcePath;
            bool HasReloader = false;
        };

        std::vector<AssetSnapshot> snapshot;
        {
            std::shared_lock lock(m_Mutex);
            auto view = m_Registry.view<AssetInfo>();
            snapshot.reserve(view.size_hint());
            for (auto [entity, info] : view.each())
            {
                AssetSnapshot entry{entity, info, {}, false};
                if (auto* src = m_Registry.try_get<AssetSource>(entity))
                    entry.SourcePath = src->FilePath.string();
                entry.HasReloader = m_Registry.any_of<AssetReloader>(entity);
                snapshot.push_back(std::move(entry));
            }
        }

        // 2. HEADER & STATISTICS (from snapshot, no lock needed)
        // ---------------------------------------------------------------------
        int countReady = 0;
        int countLoading = 0;
        int countFailed = 0;

        for (const auto& entry : snapshot)
        {
            if (entry.Info.State == LoadState::Ready) countReady++;
            else if (entry.Info.State == LoadState::Loading) countLoading++;
            else if (entry.Info.State == LoadState::Failed) countFailed++;
        }

        ImGui::Text("Total: %d", (int)snapshot.size());
        ImGui::SameLine();
        ImGui::TextColored({0.2f, 0.8f, 0.2f, 1.0f}, "Ready: %d", countReady);
        ImGui::SameLine();
        ImGui::TextColored({0.8f, 0.8f, 0.2f, 1.0f}, "Loading: %d", countLoading);
        ImGui::SameLine();
        ImGui::TextColored({0.8f, 0.2f, 0.2f, 1.0f}, "Failed: %d", countFailed);

        ImGui::Separator();

        // 3. SEARCH & FILTER
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

        // 4. ASSET TABLE (from snapshot)
        // ---------------------------------------------------------------------
        ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable;

        if (ImGui::BeginTable("AssetTable", 5, flags))
        {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Name / Path", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableHeadersRow();

            // Gather filtered list for the Clipper
            std::vector<size_t> filteredIndices;
            filteredIndices.reserve(snapshot.size());

            for (size_t idx = 0; idx < snapshot.size(); ++idx)
            {
                const auto& entry = snapshot[idx];

                // State Filter
                if (entry.Info.State == LoadState::Ready && !showReady) continue;
                if (entry.Info.State == LoadState::Loading && !showLoading) continue;
                if (entry.Info.State == LoadState::Failed && !showFailed) continue;

                // Search String Filter
                if (searchBuffer[0] != '\0')
                {
                    std::string searchStr = searchBuffer;
                    if (entry.Info.Name.find(searchStr) == std::string::npos) continue;
                }

                filteredIndices.push_back(idx);
            }

            // 5. LIST CLIPPER (Performance for large lists)
            // -----------------------------------------------------------------
            ImGuiListClipper clipper;
            clipper.Begin((int)filteredIndices.size());

            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                {
                    const auto& entry = snapshot[filteredIndices[i]];

                    ImGui::PushID((int)entry.Entity);
                    ImGui::TableNextRow();

                    // --- Col 0: ID ---
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", (uint32_t)entry.Entity);

                    // --- Col 1: State ---
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(GetStateColor(entry.Info.State), "%s", GetStateString(entry.Info.State));
                    if (ImGui::IsItemHovered() && entry.Info.State == LoadState::Failed)
                    {
                        ImGui::SetTooltip("Asset failed to load. Check logs.");
                    }

                    // --- Col 2: Type ---
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(entry.Info.Type.c_str());

                    // --- Col 3: Name / Path ---
                    ImGui::TableSetColumnIndex(3);

                    // Drag Source Logic: Drag asset from UI into Scene!
                    ImGui::Selectable(entry.Info.Name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
                    if (ImGui::BeginDragDropSource())
                    {
                        AssetHandle handle{entry.Entity};
                        ImGui::SetDragDropPayload("ASSET_HANDLE", &handle, sizeof(AssetHandle));
                        ImGui::Text("Assign %s", entry.Info.Name.c_str());
                        ImGui::EndDragDropSource();
                    }

                    if (ImGui::IsItemHovered() && !entry.SourcePath.empty())
                    {
                        ImGui::SetTooltip("%s", entry.SourcePath.c_str());
                    }

                    // --- Col 4: Actions ---
                    ImGui::TableSetColumnIndex(4);

                    bool canReload = entry.HasReloader && (entry.Info.State != LoadState::Loading);

                    if (!canReload) ImGui::BeginDisabled();
                    if (ImGui::Button("Reload"))
                    {
                        // Re-acquire lock to safely fetch the reloader action.
                        // This is safe: we only lock briefly for the single entity.
                        std::function<void()> reloadAction;
                        {
                            std::shared_lock lock(m_Mutex);
                            if (m_Registry.valid(entry.Entity))
                            {
                                if (auto* reloader = m_Registry.try_get<AssetReloader>(entry.Entity))
                                    reloadAction = reloader->ReloadAction;
                            }
                        }
                        if (reloadAction)
                        {
                            Log::Info("Manual Reload requested for: {}", entry.Info.Name);
                            reloadAction();
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
