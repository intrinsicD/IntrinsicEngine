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
    return "";
}
#endif
