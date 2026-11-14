cmake_minimum_required(VERSION 3.16)
project(trowel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
include(GNUInstallDirs)

enable_testing()

set(LV2_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/lv2"
    CACHE PATH "Directory where LV2 bundles are installed")
set(CLAP_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/clap"
    CACHE PATH "Directory where CLAP bundles are installed")

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(LV2 REQUIRED IMPORTED_TARGET lv2)
pkg_check_modules(LILV QUIET IMPORTED_TARGET lilv-0)
pkg_check_modules(CAIRO QUIET IMPORTED_TARGET cairo)
pkg_check_modules(FREETYPE QUIET IMPORTED_TARGET freetype2)
pkg_check_modules(X11 QUIET IMPORTED_TARGET x11)

set(TROWEL_ASSETS
  "${CMAKE_SOURCE_DIR}/assets/knob.png"
  "${CMAKE_SOURCE_DIR}/assets/metal.png"
  "${CMAKE_SOURCE_DIR}/assets/fonts/Jost-ExtraBold.ttf"
  "${CMAKE_SOURCE_DIR}/assets/fonts/OFL.txt")

function(trowel_common target)
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_include_directories(${target} PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/dsp"
    "${CMAKE_SOURCE_DIR}/src/trowel")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic
      -fvisibility=hidden)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# LV2 and CLAP products.  Both share src/trowel.cpp; the Master variant is the
# same DSP with TROWEL_MASTER selecting the spectral Tame and its extra
# controls.
# ---------------------------------------------------------------------------
set(TROWEL_VARIANTS trowel trowel_master)
foreach(variant IN LISTS TROWEL_VARIANTS)
  if(variant STREQUAL "trowel_master")
    set(master_def TROWEL_MASTER)
    set(bundle_name "trowel-master")
    set(binary_name "trowel-master")
    set(ttl_name "trowel-master.ttl")
    set(manifest_name "manifest-trowel-master.ttl")
    set(ui_name "trowelMasterUI")
    set(clap_name "TrowelMaster")
    set(resource_name "TrowelMaster.resources")
  else()
    set(master_def "")
    set(bundle_name "trowel")
    set(binary_name "trowel")
    set(ttl_name "trowel.ttl")
    set(manifest_name "manifest-trowel.ttl")
    set(ui_name "trowelUI")
    set(clap_name "Trowel")
    set(resource_name "Trowel.resources")
  endif()

  set(bundle_dir "${CMAKE_BINARY_DIR}/lv2/${bundle_name}.lv2")
  set(bundle_files
    "${CMAKE_SOURCE_DIR}/lv2/${ttl_name}"
    "${CMAKE_SOURCE_DIR}/lv2/presets.ttl"
    ${TROWEL_ASSETS})

  add_library(${variant} MODULE src/trowel.cpp)
  trowel_common(${variant})
  target_compile_definitions(${variant} PRIVATE ${master_def})
  target_link_libraries(${variant} PRIVATE PkgConfig::LV2)
  target_include_directories(${variant} PRIVATE ${PKG_CONFIG_LV2_INCLUDE_DIRS})
  set_target_properties(${variant} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${binary_name}"
    LIBRARY_OUTPUT_DIRECTORY "${bundle_dir}")

  add_custom_target(${variant}_bundle ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${bundle_dir}"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/lv2/${manifest_name}"
            "${bundle_dir}/manifest.ttl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${bundle_files}
            "${bundle_dir}/"
    DEPENDS ${variant} ${bundle_files}
            "${CMAKE_SOURCE_DIR}/lv2/${manifest_name}")

  if(CAIRO_FOUND AND X11_FOUND AND FREETYPE_FOUND)
    add_library(${variant}_ui MODULE src/ui/trowel_ui.cpp)
    trowel_common(${variant}_ui)
    target_compile_definitions(${variant}_ui PRIVATE ${master_def})
    target_link_libraries(${variant}_ui PRIVATE
      PkgConfig::CAIRO PkgConfig::X11 PkgConfig::LV2 PkgConfig::FREETYPE)
    set_target_properties(${variant}_ui PROPERTIES
      PREFIX ""
      OUTPUT_NAME "${ui_name}"
      LIBRARY_OUTPUT_DIRECTORY "${bundle_dir}")

    add_executable(${variant}_ui_preview EXCLUDE_FROM_ALL
      src/ui/trowel_ui.cpp)
    trowel_common(${variant}_ui_preview)
    target_compile_definitions(${variant}_ui_preview PRIVATE
      TROWEL_UI_PREVIEW ${master_def})
    target_link_libraries(${variant}_ui_preview PRIVATE
      PkgConfig::CAIRO PkgConfig::X11 PkgConfig::LV2 PkgConfig::FREETYPE)

    add_custom_target(${variant}_ui_bundle ALL
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:${variant}_ui>" "${bundle_dir}/"
      DEPENDS ${variant}_ui)
    add_dependencies(${variant}_bundle ${variant}_ui_bundle)

    # CLAP: the same DSP and X11 UI, plus host latency and GR metering.
    add_library(${variant}_clap MODULE src/clap/trowel_clap.cpp)
    trowel_common(${variant}_clap)
    target_compile_definitions(${variant}_clap PRIVATE ${master_def})
    target_include_directories(${variant}_clap PRIVATE
      "${CMAKE_SOURCE_DIR}/third_party/clap/include"
      ${PKG_CONFIG_LV2_INCLUDE_DIRS})
    target_link_libraries(${variant}_clap PRIVATE PkgConfig::LV2
      ${CMAKE_DL_LIBS} Threads::Threads)
    set_target_properties(${variant}_clap PROPERTIES
      PREFIX ""
      OUTPUT_NAME "${clap_name}"
      SUFFIX ".clap"
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clap")

    add_custom_target(${variant}_clap_bundle ALL
      COMMAND ${CMAKE_COMMAND} -E make_directory
              "${CMAKE_BINARY_DIR}/clap/${resource_name}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TROWEL_ASSETS}
              "${CMAKE_BINARY_DIR}/clap/${resource_name}/"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:${variant}_ui>"
              "${CMAKE_BINARY_DIR}/clap/${resource_name}/${ui_name}.so"
      DEPENDS ${variant}_clap ${variant}_ui ${TROWEL_ASSETS})

    install(TARGETS ${variant}_clap LIBRARY DESTINATION "${CLAP_DIR}")
    install(DIRECTORY "${CMAKE_BINARY_DIR}/clap/${resource_name}"
            DESTINATION "${CLAP_DIR}")
  endif()

  install(TARGETS ${variant} LIBRARY DESTINATION "${LV2_DIR}/${bundle_name}.lv2")
  if(TARGET ${variant}_ui)
    install(TARGETS ${variant}_ui
            LIBRARY DESTINATION "${LV2_DIR}/${bundle_name}.lv2")
  endif()
  install(FILES "${CMAKE_SOURCE_DIR}/lv2/${manifest_name}"
          DESTINATION "${LV2_DIR}/${bundle_name}.lv2" RENAME manifest.ttl)
  install(FILES lv2/${ttl_name} lv2/presets.ttl ${TROWEL_ASSETS}
          DESTINATION "${LV2_DIR}/${bundle_name}.lv2")
endforeach()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
add_executable(trowel_bench EXCLUDE_FROM_ALL tools/bench.cpp)
trowel_common(trowel_bench)

add_executable(trowel_dsp_test test/test_dsp.cpp)
trowel_common(trowel_dsp_test)
add_test(NAME trowel_dsp COMMAND trowel_dsp_test)

if(LILV_FOUND)
  add_executable(trowel_host_test test/host_test.cpp)
  trowel_common(trowel_host_test)
  target_link_libraries(trowel_host_test PRIVATE PkgConfig::LILV)
  add_test(NAME trowel_host COMMAND trowel_host_test "${CMAKE_BINARY_DIR}/lv2")
endif()

find_library(XTST_LIBRARY Xtst)
find_program(XVFB_RUN xvfb-run)
if(XTST_LIBRARY AND XVFB_RUN AND TARGET trowel_ui)
  add_executable(trowel_ui_host_test test/ui_host_test.cpp)
  trowel_common(trowel_ui_host_test)
  target_link_libraries(trowel_ui_host_test PRIVATE PkgConfig::CAIRO
    PkgConfig::X11 ${CMAKE_DL_LIBS} ${XTST_LIBRARY})
  add_test(NAME trowel_ui_host
    COMMAND "${XVFB_RUN}" -a -s "-screen 0 1600x1100x24"
      "$<TARGET_FILE:trowel_ui_host_test>"
      "${CMAKE_BINARY_DIR}/lv2/trowel.lv2/trowelUI.so"
      "${CMAKE_BINARY_DIR}/trowel-ui-live.png")
  set_tests_properties(trowel_ui_host PROPERTIES
    ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
  add_test(NAME trowel_master_ui_host
    COMMAND "${XVFB_RUN}" -a -s "-screen 0 1600x1100x24"
      "$<TARGET_FILE:trowel_ui_host_test>"
      "${CMAKE_BINARY_DIR}/lv2/trowel-master.lv2/trowelMasterUI.so"
      "${CMAKE_BINARY_DIR}/trowel-master-ui-live.png")
  set_tests_properties(trowel_master_ui_host PROPERTIES
    ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
endif()

if(X11_FOUND AND XVFB_RUN AND TARGET trowel_clap)
  add_executable(trowel_clap_host_test test/clap_host_test.cpp)
  trowel_common(trowel_clap_host_test)
  target_include_directories(trowel_clap_host_test PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/clap/include")
  target_link_libraries(trowel_clap_host_test PRIVATE PkgConfig::X11
    ${CMAKE_DL_LIBS})
  add_dependencies(trowel_clap_host_test trowel_clap_bundle
                   trowel_master_clap_bundle)
  add_test(NAME trowel_clap_host
    COMMAND "${XVFB_RUN}" -a -s "-screen 0 1600x1000x24"
      "$<TARGET_FILE:trowel_clap_host_test>"
      "${CMAKE_BINARY_DIR}/clap/Trowel.clap"
      "com.tylorismusic.trowel")
  add_test(NAME trowel_master_clap_host
    COMMAND "${XVFB_RUN}" -a -s "-screen 0 1600x1000x24"
      "$<TARGET_FILE:trowel_clap_host_test>"
      "${CMAKE_BINARY_DIR}/clap/TrowelMaster.clap"
      "com.tylorismusic.trowel-master")
endif()
