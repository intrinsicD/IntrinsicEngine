#include "MockRHI.hpp"
#include <type_traits>
using namespace Extrinsic::Tests;
static_assert(std::is_nothrow_default_constructible_v<MockDevice>);
static_assert(std::is_nothrow_destructible_v<MockDevice>);
static_assert(std::is_nothrow_default_constructible_v<MockCommandContext>);
static_assert(std::is_nothrow_destructible_v<MockCommandContext>);
static_assert(std::is_nothrow_default_constructible_v<MockBindlessHeap>);
static_assert(std::is_nothrow_destructible_v<MockBindlessHeap>);
static_assert(std::is_nothrow_default_constructible_v<MockTransferQueue>);
static_assert(std::is_nothrow_destructible_v<MockTransferQueue>);
static_assert(!std::is_move_constructible_v<MockDevice>);
static_assert(!std::is_copy_constructible_v<MockDevice>);
static_assert(std::is_nothrow_move_constructible_v<MockCommandContext>);
