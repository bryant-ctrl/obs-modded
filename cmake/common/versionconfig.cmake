# OBS CMake common version helper module
# Modified for CI builds: skip git describe, use OBS_VERSION_OVERRIDE or a fixed default.

include_guard(GLOBAL)

if(NOT DEFINED OBS_VERSION_OVERRIDE)
  set(OBS_VERSION_OVERRIDE "31.0.0")
endif()

string(REGEX REPLACE "([0-9]+)\\.([0-9]+)\\.([0-9]+).*" "\\1;\\2;\\3"
       _obs_version_canonical "${OBS_VERSION_OVERRIDE}")

list(GET _obs_version_canonical 0 OBS_VERSION_MAJOR)
list(GET _obs_version_canonical 1 OBS_VERSION_MINOR)
list(GET _obs_version_canonical 2 OBS_VERSION_PATCH)

set(OBS_RELEASE_CANDIDATE 0)
set(OBS_BETA 0)
set(OBS_VERSION_CANONICAL "${OBS_VERSION_MAJOR}.${OBS_VERSION_MINOR}.${OBS_VERSION_PATCH}")
set(OBS_VERSION "${OBS_VERSION_OVERRIDE}")

message(STATUS "OBS version: ${OBS_VERSION}")
