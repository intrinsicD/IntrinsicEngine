module;

#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

export module Core.FeatureRegistry;

import Core.Hash;
import Core.Logging;

// -------------------------------------------------------------------------
// Core::FeatureRegistry — Central Feature Registration
// -------------------------------------------------------------------------
// PURPOSE: Provides a single, engine-wide registry where render features,
// geometry operators, UI panels, and ECS systems register at startup.
//
// This removes the need to hard-wire features into Engine or RenderSystem.
// Adding a new feature = add a .cppm module + one registration call.
//
// Design:
//   - Type-erased storage (no RTTI required)
//   - Factory-based instance creation
//   - Enable/disable mechanism for runtime configuration
//   - Cold-path only — not designed for per-frame hot-loop access
//   - Not thread-safe; all registration from main thread
//
// Usage:
//   Core::FeatureRegistry registry;
//   registry.Register<MySurfacePass>("SurfacePass", FeatureCategory::RenderFeature);
//   auto* info = registry.Find("SurfacePass"_id);
//   void* instance = registry.CreateInstance("SurfacePass"_id);
//   // ... use instance as concrete type ...
//   registry.DestroyInstance("SurfacePass"_id, instance);
// -------------------------------------------------------------------------

export namespace Core
{
    // Category tags for registered features.
    enum class FeatureCategory : uint8_t
    {
        RenderFeature = 0, // Graphics::IRenderFeature implementations
        GeometryOperator,  // Geometry processing operators
        Panel,             // UI panels (ImGui)
        System,            // ECS systems / FrameGraph nodes
    };

    // Metadata for a registered feature.
    struct FeatureInfo
    {
        Hash::StringID Id{};
        std::string Name;
        std::string Description;
        FeatureCategory Category{};
        bool Enabled = true;
    };

    struct FeatureDescriptor
    {
        Hash::StringID Id{};
        std::string_view Name;
        std::string_view Description;
        FeatureCategory Category{};
        bool DefaultEnabled = true;
    };

    [[nodiscard]] constexpr FeatureDescriptor MakeFeatureDescriptor(
        std::string_view name,
        FeatureCategory category,
        std::string_view description = {},
        bool defaultEnabled = true)
    {
        return FeatureDescriptor{
            .Id = Hash::StringID(name),
            .Name = name,
            .Description = description,
            .Category = category,
            .DefaultEnabled = defaultEnabled,
        };
    }

    // Type-erased factory: creates a heap-allocated instance.
    using FeatureFactoryFn = std::function<void*()>;

    // Type-erased destructor: destroys a heap-allocated instance.
    using FeatureDestroyFn = std::function<void(void*)>;

    class FeatureRegistry
    {
    public:
        // ----- Registration (explicit factory) -----

        // Register a feature with info + factory + destructor.
        // Returns true on success, false if a feature with the same ID exists.
        bool Register(FeatureInfo info, FeatureFactoryFn factory, FeatureDestroyFn destroy);

        bool Register(const FeatureDescriptor& descriptor, FeatureFactoryFn factory, FeatureDestroyFn destroy)
        {
            FeatureInfo info{};
            info.Id = descriptor.Id;
            info.Name = std::string(descriptor.Name);
            info.Description = std::string(descriptor.Description);
            info.Category = descriptor.Category;
            info.Enabled = descriptor.DefaultEnabled;
            return Register(std::move(info), std::move(factory), std::move(destroy));
        }

        // ----- Registration (convenience templates) -----

        // Register a default-constructible type T.
        template<typename T>
        bool Register(std::string name, FeatureCategory category,
                      std::string description = "")
        {
            FeatureInfo info{};
            info.Name = std::move(name);
            info.Description = std::move(description);
            info.Id = Hash::StringID(Hash::HashString(info.Name));
            info.Category = category;
            info.Enabled = true;
            return Register(
                std::move(info),
                []() -> void* { return new T(); },
                [](void* p) { delete static_cast<T*>(p); }
            );
        }

        // Register with a custom factory that returns T*.
        template<typename T, typename Factory>
        bool RegisterWithFactory(std::string name, FeatureCategory category,
                                 Factory&& factory, std::string description = "")
        {
            FeatureInfo info{};
            info.Name = std::move(name);
            info.Description = std::move(description);
            info.Id = Hash::StringID(Hash::HashString(info.Name));
            info.Category = category;
            info.Enabled = true;
            auto wrappedFactory = [f = std::forward<Factory>(factory)]() -> void* {
                return f();
            };

            return Register(
                std::move(info),
                std::move(wrappedFactory),
                [](void* p) { delete static_cast<T*>(p); }
            );
        }

        // ----- Unregistration -----

        // Unregister by ID. Returns true if found and removed.
        bool Unregister(Hash::StringID id);

        // ----- Query -----

        // Find feature info by ID. Returns nullptr if not found.
        [[nodiscard]] const FeatureInfo* Find(Hash::StringID id) const;
        [[nodiscard]] const FeatureInfo* Find(const FeatureDescriptor& descriptor) const
        {
            return Find(descriptor.Id);
        }

        // Get all features in a category (ordered by registration order).
        [[nodiscard]] std::vector<const FeatureInfo*> GetByCategory(FeatureCategory category) const;

        // Get only enabled features in a category.
        [[nodiscard]] std::vector<const FeatureInfo*> GetEnabled(FeatureCategory category) const;

        // ----- Enable / Disable -----

        // Returns true if found and state changed.
        bool SetEnabled(Hash::StringID id, bool enabled);
        bool SetEnabled(const FeatureDescriptor& descriptor, bool enabled)
        {
            return SetEnabled(descriptor.Id, enabled);
        }

        // Returns false if not found or disabled.
        [[nodiscard]] bool IsEnabled(Hash::StringID id) const;
        [[nodiscard]] bool IsEnabled(const FeatureDescriptor& descriptor) const
        {
            return IsEnabled(descriptor.Id);
        }

        // ----- Instance Creation -----

        // Create a new instance via factory. Caller owns the returned pointer.
        // Returns nullptr if ID not found or feature disabled.
        [[nodiscard]] void* CreateInstance(Hash::StringID id) const;
        [[nodiscard]] void* CreateInstance(const FeatureDescriptor& descriptor) const
        {
            return CreateInstance(descriptor.Id);
        }

        // Destroy an instance previously created by CreateInstance.
        // No-op if id not found or instance is null.
        void DestroyInstance(Hash::StringID id, void* instance) const;
        void DestroyInstance(const FeatureDescriptor& descriptor, void* instance) const
        {
            DestroyInstance(descriptor.Id, instance);
        }

        // ----- Metadata -----

        [[nodiscard]] size_t Count() const;
        [[nodiscard]] size_t CountByCategory(FeatureCategory category) const;

        // Clear all registrations.
        void Clear();

        // ----- Iteration -----

        // Call fn(const FeatureInfo&) for every registered feature.
        template<typename Fn>
        void ForEach(Fn&& fn) const
        {
            for (const auto& entry : m_Entries)
            {
                fn(entry.Info);
            }
        }

        // Call fn(const FeatureInfo&) for features in a specific category.
        template<typename Fn>
        void ForEachInCategory(FeatureCategory category, Fn&& fn) const
        {
            for (const auto& entry : m_Entries)
            {
                if (entry.Info.Category == category)
                {
                    fn(entry.Info);
                }
            }
        }

    private:
        struct Entry
        {
            FeatureInfo Info;
            FeatureFactoryFn Factory;
            FeatureDestroyFn Destroy;
        };

        std::vector<Entry> m_Entries;

        [[nodiscard]] const Entry* FindEntry(Hash::StringID id) const;
        Entry* FindEntry(Hash::StringID id);
    };
}
