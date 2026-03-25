cmake_minimum_required(VERSION 3.16)

project(ddp-matrix LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RGB_MATRIX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rpi-rgb-led-matrix")
set(RGB_MATRIX_INCLUDE_DIR "${RGB_MATRIX_DIR}/include")
set(RGB_MATRIX_LIBDIR "${RGB_MATRIX_DIR}/lib")
set(RGB_MATRIX_LIB "${RGB_MATRIX_LIBDIR}/librgbmatrix.a")

if(NOT EXISTS "${RGB_MATRIX_DIR}/Makefile")
  message(FATAL_ERROR
          "Missing rpi-rgb-led-matrix submodule. "
          "Run: git submodule update --init --recursive")
endif()

set(DDP_MATRIX_GPIO_MAPPING "adafruit-hat-pwm" CACHE STRING
    "Value passed to --led-gpio-mapping")
set(DDP_MATRIX_PANEL_ROWS "64" CACHE STRING
    "Rows per panel passed to --led-rows")
set(DDP_MATRIX_PANEL_COLS "128" CACHE STRING
    "Columns per panel passed to --led-cols")
set(DDP_MATRIX_PANEL_CHAIN "1" CACHE STRING
    "Daisy-chained panels passed to --led-chain")
set(DDP_MATRIX_PANEL_PARALLEL "1" CACHE STRING
    "Parallel panel chains passed to --led-parallel")
set(DDP_MATRIX_BRIGHTNESS "100" CACHE STRING
    "Brightness passed to --led-brightness")
set(DDP_MATRIX_GPIO_SLOWDOWN "2" CACHE STRING
    "GPIO slowdown passed to --led-slowdown-gpio")
option(DDP_MATRIX_ENABLE_REALTIME
       "Enable realtime scheduling for the service"
       ON)
set(DDP_MATRIX_REALTIME_PRIORITY "40" CACHE STRING
    "Realtime priority passed to --realtime-priority")
set(DDP_MATRIX_EXTRA_ARGS "" CACHE STRING
    "Additional arguments appended to the systemd ExecStart")

set(DDP_MATRIX_INSTALL_DIR "/opt" CACHE PATH
    "Install directory for the ddp-receiver binary")
set(DDP_MATRIX_SYSTEMD_DIR "/etc/systemd/system" CACHE PATH
    "Systemd unit directory")
set(DDP_MATRIX_SYSTEMD_SERVICE_NAME "ddp-receiver.service" CACHE STRING
    "Systemd service file name")
option(DDP_MATRIX_MANAGE_SYSTEMD_SERVICE
       "Run systemctl daemon-reload/enable/restart during install"
       ON)

function(ddp_matrix_require_integer variable_name minimum_value)
  if(NOT ${variable_name} MATCHES "^[0-9]+$")
    message(FATAL_ERROR
            "${variable_name} must be an integer, got '${${variable_name}}'")
  endif()
  if(${${variable_name}} LESS ${minimum_value})
    message(FATAL_ERROR
            "${variable_name} must be >= ${minimum_value}, got '${${variable_name}}'")
  endif()
endfunction()

ddp_matrix_require_integer(DDP_MATRIX_PANEL_ROWS 1)
ddp_matrix_require_integer(DDP_MATRIX_PANEL_COLS 1)
ddp_matrix_require_integer(DDP_MATRIX_PANEL_CHAIN 1)
ddp_matrix_require_integer(DDP_MATRIX_PANEL_PARALLEL 1)
ddp_matrix_require_integer(DDP_MATRIX_BRIGHTNESS 1)
ddp_matrix_require_integer(DDP_MATRIX_GPIO_SLOWDOWN 0)
ddp_matrix_require_integer(DDP_MATRIX_REALTIME_PRIORITY 1)

if(DDP_MATRIX_BRIGHTNESS GREATER 100)
  message(FATAL_ERROR
          "DDP_MATRIX_BRIGHTNESS must be <= 100, got '${DDP_MATRIX_BRIGHTNESS}'")
endif()

if(DDP_MATRIX_REALTIME_PRIORITY GREATER 99)
  message(FATAL_ERROR
          "DDP_MATRIX_REALTIME_PRIORITY must be <= 99, got '${DDP_MATRIX_REALTIME_PRIORITY}'")
endif()

math(EXPR DDP_MATRIX_PANEL_COUNT
     "${DDP_MATRIX_PANEL_CHAIN} * ${DDP_MATRIX_PANEL_PARALLEL}")
math(EXPR DDP_MATRIX_CANVAS_WIDTH
     "${DDP_MATRIX_PANEL_COLS} * ${DDP_MATRIX_PANEL_CHAIN}")
math(EXPR DDP_MATRIX_CANVAS_HEIGHT
     "${DDP_MATRIX_PANEL_ROWS} * ${DDP_MATRIX_PANEL_PARALLEL}")

if(DDP_MATRIX_ENABLE_REALTIME)
  set(DDP_MATRIX_REALTIME_FLAG
      "--realtime-priority=${DDP_MATRIX_REALTIME_PRIORITY}")
  set(DDP_MATRIX_ENABLE_REALTIME_LITERAL "true")
else()
  set(DDP_MATRIX_REALTIME_FLAG "--no-realtime")
  set(DDP_MATRIX_ENABLE_REALTIME_LITERAL "false")
endif()

set(DDP_MATRIX_SERVICE_ARGS
    "--led-gpio-mapping=${DDP_MATRIX_GPIO_MAPPING}"
    "--led-rows=${DDP_MATRIX_PANEL_ROWS}"
    "--led-cols=${DDP_MATRIX_PANEL_COLS}"
    "--led-chain=${DDP_MATRIX_PANEL_CHAIN}"
    "--led-parallel=${DDP_MATRIX_PANEL_PARALLEL}"
    "--led-brightness=${DDP_MATRIX_BRIGHTNESS}"
    "--led-slowdown-gpio=${DDP_MATRIX_GPIO_SLOWDOWN}"
    "${DDP_MATRIX_REALTIME_FLAG}")

string(STRIP "${DDP_MATRIX_EXTRA_ARGS}" DDP_MATRIX_EXTRA_ARGS_STRIPPED)
if(NOT DDP_MATRIX_EXTRA_ARGS_STRIPPED STREQUAL "")
  list(APPEND DDP_MATRIX_SERVICE_ARGS "${DDP_MATRIX_EXTRA_ARGS_STRIPPED}")
endif()

string(JOIN " " DDP_MATRIX_SERVICE_ARGS_STRING ${DDP_MATRIX_SERVICE_ARGS})

message(STATUS
        "ddp-matrix panels: ${DDP_MATRIX_PANEL_COUNT} total "
        "(chain=${DDP_MATRIX_PANEL_CHAIN}, parallel=${DDP_MATRIX_PANEL_PARALLEL})")
message(STATUS
        "ddp-matrix panel geometry: "
        "${DDP_MATRIX_PANEL_COLS}x${DDP_MATRIX_PANEL_ROWS} pixels per panel")
message(STATUS
        "ddp-matrix canvas geometry: "
        "${DDP_MATRIX_CANVAS_WIDTH}x${DDP_MATRIX_CANVAS_HEIGHT}")
message(STATUS "ddp-matrix service args: ${DDP_MATRIX_SERVICE_ARGS_STRING}")

find_program(RGB_MATRIX_MAKE_EXECUTABLE NAMES gmake make)
if(NOT RGB_MATRIX_MAKE_EXECUTABLE)
  message(FATAL_ERROR "Unable to find GNU make for rpi-rgb-led-matrix/lib")
endif()

if(DDP_MATRIX_MANAGE_SYSTEMD_SERVICE)
  find_program(DDP_MATRIX_SYSTEMCTL_EXECUTABLE NAMES systemctl)
  if(NOT DDP_MATRIX_SYSTEMCTL_EXECUTABLE)
    message(FATAL_ERROR
            "DDP_MATRIX_MANAGE_SYSTEMD_SERVICE is ON but systemctl was not found")
  endif()
else()
  set(DDP_MATRIX_SYSTEMCTL_EXECUTABLE "" CACHE FILEPATH
      "Path to the systemctl executable")
endif()

file(GLOB RGB_MATRIX_BUILD_FILES CONFIGURE_DEPENDS
     "${RGB_MATRIX_DIR}/Makefile"
     "${RGB_MATRIX_DIR}/config.mk"
     "${RGB_MATRIX_LIBDIR}/Makefile"
     "${RGB_MATRIX_INCLUDE_DIR}/*.h"
     "${RGB_MATRIX_LIBDIR}/*.h"
     "${RGB_MATRIX_LIBDIR}/*.c"
     "${RGB_MATRIX_LIBDIR}/*.cc")

configure_file(ddp-receiver-config.h.in
               "${CMAKE_CURRENT_BINARY_DIR}/ddp-receiver-config.h"
               @ONLY)
configure_file(ddp-receiver.service.in
               "${CMAKE_CURRENT_BINARY_DIR}/${DDP_MATRIX_SYSTEMD_SERVICE_NAME}"
               @ONLY)

if(DDP_MATRIX_MANAGE_SYSTEMD_SERVICE)
  configure_file(cmake/install-systemd.cmake.in
                 "${CMAKE_CURRENT_BINARY_DIR}/install-systemd.cmake"
                 @ONLY)
endif()

add_custom_command(
  OUTPUT "${RGB_MATRIX_LIB}"
  COMMAND "${RGB_MATRIX_MAKE_EXECUTABLE}" -C "${RGB_MATRIX_LIBDIR}"
  DEPENDS ${RGB_MATRIX_BUILD_FILES}
  COMMENT "Building rpi-rgb-led-matrix static library"
  VERBATIM)

add_custom_target(rgbmatrix-build DEPENDS "${RGB_MATRIX_LIB}")

add_executable(ddp-receiver
               ddp-receiver.cc
               ddp-receiver.h
               "${CMAKE_CURRENT_BINARY_DIR}/ddp-receiver-config.h")
add_dependencies(ddp-receiver rgbmatrix-build)

target_include_directories(ddp-receiver PRIVATE
                           "${RGB_MATRIX_INCLUDE_DIR}"
                           "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(ddp-receiver PRIVATE
                      "${RGB_MATRIX_LIB}"
                      rt
                      m
                      pthread)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ddp-receiver PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS ddp-receiver RUNTIME DESTINATION "${DDP_MATRIX_INSTALL_DIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${DDP_MATRIX_SYSTEMD_SERVICE_NAME}"
        DESTINATION "${DDP_MATRIX_SYSTEMD_DIR}")

if(DDP_MATRIX_MANAGE_SYSTEMD_SERVICE)
  install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/install-systemd.cmake")
endif()
