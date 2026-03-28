cmake_minimum_required(VERSION 3.28...3.30)

option(ENABLE_VIRTUALCAM2 "Enable Windows Virtual Camera 2" ON)
if(NOT ENABLE_VIRTUALCAM2)
  target_disable_feature(obs-virtualcam2-module "Virtual Camera 2 Support")
  target_disable(obs-virtualcam2-module)
  return()
endif()

if(NOT VIRTUALCAM2_GUID)
  set(VIRTUALCAM2_GUID "" CACHE STRING "Virtual Camera 2 GUID" FORCE)
  mark_as_advanced(VIRTUALCAM2_GUID)

  message(WARNING "Empty Virtual Camera 2 GUID set.")
  target_disable_feature(obs-virtualcam2-module "Virtual Camera 2 Support (empty GUID)")
  return()
else()
  set(VALID_GUID2 FALSE)
  check_uuid(${VIRTUALCAM2_GUID} VALID_GUID2)

  if(NOT VALID_GUID2)
    message(WARNING "Invalid Virtual Camera 2 GUID set.")
    target_disable_feature(obs-virtualcam2-module "Virtual Camera 2 Support (invalid GUID)")
    return()
  endif()

  # DirectShow API requires separate GUID tokens
  string(REPLACE "-" ";" GUID2_VALS ${VIRTUALCAM2_GUID})
  list(GET GUID2_VALS 0 GUID2_VALS_DATA1)
  list(GET GUID2_VALS 1 GUID2_VALS_DATA2)
  list(GET GUID2_VALS 2 GUID2_VALS_DATA3)
  list(GET GUID2_VALS 3 GUID2_VALS_DATA4)
  list(GET GUID2_VALS 4 GUID2_VALS_DATA5)
  set(GUID2_VAL01 ${GUID2_VALS_DATA1})
  set(GUID2_VAL02 ${GUID2_VALS_DATA2})
  set(GUID2_VAL03 ${GUID2_VALS_DATA3})
  string(SUBSTRING ${GUID2_VALS_DATA4} 0 2 GUID2_VAL04)
  string(SUBSTRING ${GUID2_VALS_DATA4} 2 2 GUID2_VAL05)
  string(SUBSTRING ${GUID2_VALS_DATA5} 0 2 GUID2_VAL06)
  string(SUBSTRING ${GUID2_VALS_DATA5} 2 2 GUID2_VAL07)
  string(SUBSTRING ${GUID2_VALS_DATA5} 4 2 GUID2_VAL08)
  string(SUBSTRING ${GUID2_VALS_DATA5} 6 2 GUID2_VAL09)
  string(SUBSTRING ${GUID2_VALS_DATA5} 8 2 GUID2_VAL10)
  string(SUBSTRING ${GUID2_VALS_DATA5} 10 2 GUID2_VAL11)
endif()

if(NOT TARGET OBS::winhandle)
  add_subdirectory("${CMAKE_SOURCE_DIR}/libobs" "${CMAKE_BINARY_DIR}/libobs")
endif()

if(NOT TARGET OBS::threading-windows)
  add_subdirectory("${CMAKE_SOURCE_DIR}/libobs" "${CMAKE_BINARY_DIR}/libobs")
endif()

if(NOT TARGET OBS::tiny-nv12-scale)
  add_subdirectory("${CMAKE_SOURCE_DIR}/shared/obs-tiny-nv12-scale" obs-tiny-nv12-scale)
endif()

if(NOT TARGET OBS::shared-memory-queue)
  add_subdirectory("${CMAKE_SOURCE_DIR}/shared/obs-shared-memory-queue" obs-shared-memory-queue)
endif()

if(NOT TARGET OBS::libdshowcapture)
  add_subdirectory("${CMAKE_SOURCE_DIR}/deps/libdshowcapture" libdshowcapture)
endif()

add_library(obs-virtualcam2-guid INTERFACE)
add_library(OBS::virtualcam2-guid ALIAS obs-virtualcam2-guid)

configure_file(virtualcam2-guid.h.in virtualcam2-guid.h)
target_sources(obs-virtualcam2-guid INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/virtualcam2-guid.h")
target_include_directories(obs-virtualcam2-guid INTERFACE "${CMAKE_CURRENT_BINARY_DIR}")

add_library(obs-virtualcam2-module MODULE)
add_library(OBS::virtualcam2 ALIAS obs-virtualcam2-module)

configure_file(cmake/windows/obs-module.rc.in virtualcam2-module.rc)
target_sources(
  obs-virtualcam2-module
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/virtualcam2-module.rc
    # Reuse placeholder & sleepto from virtualcam-module (same source tree)
    ../virtualcam-module/placeholder.cpp
    ../virtualcam-module/sleepto.c
    ../virtualcam-module/sleepto.h
    virtualcam2-filter.cpp
    virtualcam2-filter.hpp
    virtualcam2-module.cpp
)

target_include_directories(obs-virtualcam2-module
  PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../virtualcam-module"
)
target_compile_definitions(obs-virtualcam2-module PRIVATE VIRTUALCAM2_AVAILABLE)

target_link_libraries(
  obs-virtualcam2-module
  PRIVATE
    OBS::tiny-nv12-scale
    OBS::shared-memory-queue
    OBS::virtualcam2-guid
    OBS::libdshowcapture
    OBS::winhandle
    OBS::threading-windows
    gdiplus
    strmiids
    winmm
)

if(OBS_PARENT_ARCHITECTURE STREQUAL CMAKE_VS_PLATFORM_NAME)
  if(CMAKE_VS_PLATFORM_NAME STREQUAL ARM64)
    add_custom_command(
      TARGET obs-virtualcam2-module
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" --build ${CMAKE_SOURCE_DIR}/build_x64 --config $<CONFIG> -t obs-virtualcam2-module
      COMMENT "Build x64 obs-virtualcam2-module"
    )
  endif()

  if(CMAKE_VS_PLATFORM_NAME MATCHES "(ARM64|x64)")
    add_custom_command(
      TARGET obs-virtualcam2-module
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" --build ${CMAKE_SOURCE_DIR}/build_x86 --config $<CONFIG> -t obs-virtualcam2-module
      COMMENT "Build x86 obs-virtualcam2-module"
    )
  endif()

  add_dependencies(win-dshow obs-virtualcam2-module)
endif()

if(CMAKE_VS_PLATFORM_NAME STREQUAL ARM64)
  set(_OUTPUT_NAME2 virtualcam2-module-arm64)
elseif(CMAKE_VS_PLATFORM_NAME STREQUAL x64)
  set(_OUTPUT_NAME2 virtualcam2-module64)
else()
  set(_OUTPUT_NAME2 virtualcam2-module32)
endif()

target_sources(obs-virtualcam2-module PRIVATE cmake/windows/${_OUTPUT_NAME2}.def)

# Store the GUID string for install/uninstall bat scripts
set(VIRTUALCAM2_GUID_STR "${VIRTUALCAM2_GUID}")

configure_file(virtualcam2-install.bat.in virtualcam2-install.bat)
target_add_resource(obs-virtualcam2-module "${CMAKE_CURRENT_BINARY_DIR}/virtualcam2-install.bat"
                    "${OBS_DATA_DESTINATION}/obs-plugins/win-dshow"
)

configure_file(virtualcam2-uninstall.bat.in virtualcam2-uninstall.bat)
target_add_resource(obs-virtualcam2-module "${CMAKE_CURRENT_BINARY_DIR}/virtualcam2-uninstall.bat"
                    "${OBS_DATA_DESTINATION}/obs-plugins/win-dshow"
)

set_target_properties_obs(
  obs-virtualcam2-module PROPERTIES
                         FOLDER plugins/win-dshow
                         OUTPUT_NAME obs-${_OUTPUT_NAME2}
                         MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
