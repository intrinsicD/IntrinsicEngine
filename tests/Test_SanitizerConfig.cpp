#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define INTRINSIC_ASAN_ENABLED 1
#endif
#endif

#if defined(__SANITIZE_ADDRESS__)
#define INTRINSIC_ASAN_ENABLED 1
#endif

#if defined(INTRINSIC_ASAN_ENABLED)
extern "C" const char* __asan_default_options()
{
    return "symbolize=1:detect_leaks=1:fast_unwind_on_malloc=0";
}

extern "C" const char* __lsan_default_options()
{
    return "fast_unwind_on_malloc=0";
}

extern "C" const char* __lsan_default_suppressions()
{
    return
        "leak:/lib/x86_64-linux-gnu/libdbus-1.so.3\n"
        "leak:libdbus\n"
        "leak:libvulkan\n"
        "leak:libGLX_nvidia\n"
        "leak:libnvidia-glcore\n"
        "leak:Unknown Module\n"
        "leak:libvulkan_virtio\n"
        "leak:__pthread_once_slow\n"
        "leak:pthread_once\n";
}
#endif

