# BUG-143 diagnosis artifacts

Two `intrinsic.frame_pacing.v1` captures from the same `ExtrinsicSandbox`
binary in the same session, 32 frames each, with only the X11 display power
state changed between them:

```bash
xset -display :1 dpms force off      # or: force on
./build/ci-vulkan/bin/ExtrinsicSandbox \
  --frame-pacing-report <out>.json --frame-pacing-frames 32
```

| Capture | Median frame total | Median `present_micros` |
| --- | --- | --- |
| `frame-pacing-display-off.json` | 1000.0 ms | 897.4 ms |
| `frame-pacing-display-on.json` | 102.5 ms | 0.1 ms |

`vkQueuePresentKHR` blocks for ~0.9 s per frame when the display is DPMS-off;
the residual ~100 ms in that case is the swapchain acquire inside
`render_prepare`, pinning a throttled frame at almost exactly 1 Hz. This is
the whole of the smoke's 13 s ↔ 34 s wall-clock variance, and the reason a
fixed frame budget cannot bound a test measured in seconds.
