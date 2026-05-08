# App/Sandbox

This directory contains the `Sandbox` module/files.

`Sandbox` is the generic reference integration target. `Sandbox::App` should
remain policy-light: it may observe lifecycle hooks, but engine feature wiring,
frame phases, and subsystem behavior belong in `Runtime` or lower engine layers.
The executable obtains its default configuration through `Runtime` and should
not import lower layers directly.

## Contents

- `CMakeLists.txt`
- `Sandbox.cppm`
- `main.cpp`
