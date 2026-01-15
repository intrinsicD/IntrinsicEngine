#include <gtest/gtest.h>

import Core;

using namespace Core;
using namespace Core::Assets;

struct Reloadable
{
    int Value = 0;
};

TEST(AssetSystem, LeaseSurvivesReload_NewLeaseSeesNewValue)
{
    Core::Tasks::Scheduler::Initialize(1);
    AssetManager manager;

    int generation = 1;

    auto loader = [&](const std::string&, AssetHandle) -> std::shared_ptr<Reloadable>
    {
        auto r = std::make_shared<Reloadable>();
        r->Value = generation;
        return r;
    };

    auto handle = manager.Load<Reloadable>("reloadable", loader);
    Core::Tasks::Scheduler::WaitForAll();

    // Pin old value.
    auto lease1Res = manager.Pin<Reloadable>(handle);
    ASSERT_TRUE(lease1Res.has_value());
    auto lease1 = *lease1Res;
    ASSERT_TRUE((bool)lease1);
    EXPECT_EQ(lease1->Value, 1);

    // Trigger reload to new value.
    generation = 2;
    manager.ReloadAsset<Reloadable>(handle);
    Core::Tasks::Scheduler::WaitForAll();

    // Old lease must still see old data.
    EXPECT_EQ(lease1->Value, 1);

    // New lease must see new data.
    auto lease2Res = manager.Pin<Reloadable>(handle);
    ASSERT_TRUE(lease2Res.has_value());
    EXPECT_EQ((*lease2Res)->Value, 2);

    Core::Tasks::Scheduler::Shutdown();
}
