# ARCH-022 spike: the host's module BMIs as one response file, from the host's own .modmap files.
# Usage: cmake -DHOST_ROOT=<build>/src -DOUT=<file> -P CollectHostModules.cmake
file(GLOB_RECURSE modmaps "${HOST_ROOT}/*.modmap")
set(flags "")
foreach(modmap IN LISTS modmaps)
    file(STRINGS "${modmap}" lines REGEX "^-fmodule-(file|output)=")
    foreach(line IN LISTS lines)
        # A module's own interface unit names its BMI with -fmodule-output; importers use -fmodule-file.
        string(REGEX REPLACE "^-fmodule-output=\"(.*)/([^/]+)\\.pcm\"$" "-fmodule-file=\"\\2=\\1/\\2.pcm\"" line "${line}")
        list(APPEND flags "${line}")
    endforeach()
endforeach()
list(REMOVE_DUPLICATES flags)
list(SORT flags)
list(JOIN flags "\n" text)
file(WRITE "${OUT}.tmp" "${text}\n")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
