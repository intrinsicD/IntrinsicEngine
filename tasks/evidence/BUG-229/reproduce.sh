#!/usr/bin/env bash
# Diagnostic only: independent Vulkan/X11 calls, not an engine preset build.
# Start an owned Xephyr display separately; defaults compare :9 against :1.
set -u
probe_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
probe_build=$(mktemp -d)
trap 'rm -rf "$probe_build"' EXIT
probe_icd=${VK_DRIVER_FILES:-/usr/share/vulkan/icd.d/nvidia_icd.json}
probe_compiler=${CC:-clang-23}
"$probe_compiler" -g -fsanitize=address "$probe_root/wsi-closefirst.c" -lvulkan -lxcb -o "$probe_build/wsi" || exit 2
"$probe_compiler" -g -fsanitize=address "$probe_root/wsi-xlib-closefirst.c" -lvulkan -lX11 -o "$probe_build/wsi-xlib" || exit 2
export VK_DRIVER_FILES="$probe_icd"
export ASAN_OPTIONS=detect_leaks=1:fast_unwind_on_malloc=0
export LSAN_OPTIONS="suppressions=$probe_root/../../../lsan.supp"
probe_failed=0
for probe_display in "${NESTED_DISPLAY:-:9}" "${NATIVE_DISPLAY:-:1}"; do
    for stage in 0 1 2 3; do
        echo "DISPLAY=$probe_display XCB stage=$stage"
        DISPLAY="$probe_display" "$probe_build/wsi" "$stage" || probe_failed=1
    done
    echo "DISPLAY=$probe_display XCB repeated capability queries"
    DISPLAY="$probe_display" "$probe_build/wsi" 2 10 || probe_failed=1
    echo "DISPLAY=$probe_display Xlib swapchain"
    DISPLAY="$probe_display" "$probe_build/wsi-xlib" 3 || probe_failed=1
done
exit "$probe_failed"
