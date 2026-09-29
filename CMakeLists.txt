cmake_minimum_required(VERSION 3.16)

# ESP-IDF only picks up sdkconfig.defaults and sdkconfig.defaults.<target> on
# its own. sdkconfig.defaults.local (gitignored, holds the WiFi PSK and the
# AetherSDR host) has to be named explicitly or it is silently ignored -- which
# looks exactly like credentials that do not work.
set(_vfo_defaults "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
    list(APPEND _vfo_defaults "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
    message(STATUS "VFO: using sdkconfig.defaults.local")
endif()
# Honour a caller-supplied list (used by the USB-networking overlay) rather
# than clobbering it.
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "${_vfo_defaults}")
endif()

# Keep each build directory's configuration to itself. By default IDF writes
# sdkconfig next to the project, so `idf.py -B build_usbnet` and `idf.py -B
# build` share one file: whichever ran last wins, and SDKCONFIG_DEFAULTS is
# only consulted when that file does not yet exist. The failure is silent --
# a USB-networking build comes out with USB networking compiled out, reports
# success, flashes, and simply does nothing.
if(NOT DEFINED SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()

# The radio this firmware is for: `idf.py -D VFO_RADIO=...`. There is one
# firmware per radio -- they share everything but the radio's client, and
# internal RAM has room for one -- and the image is named for it,
# vfo-knob-<radio>. That name is how a knob tells its own updates from another
# radio's firmware, and which update channel it follows (components/ota).
set(VFO_RADIO "aethersdr" CACHE STRING "The radio this firmware is built for")
set(_vfo_radios aethersdr icom)
if(NOT VFO_RADIO IN_LIST _vfo_radios)
    message(FATAL_ERROR "VFO_RADIO=${VFO_RADIO}: there is firmware for ${_vfo_radios}")
endif()

# Each radio's client implements components/radio/include/radio.h, so exactly
# one may be built: two would be two definitions of everything in it.
if(VFO_RADIO STREQUAL "icom")
    set(_vfo_client icom_client)
    list(APPEND EXCLUDE_COMPONENTS tci_client)
else()
    set(_vfo_client tci_client)
    list(APPEND EXCLUDE_COMPONENTS icom_client)
endif()

# The hardware this firmware is for: `idf.py -D VFO_BOARD=...`, independent of
# the radio. knob18 is the Waveshare ESP32-S3-Knob-Touch-LCD-1.8 this project
# was built on, and its image keeps the name it always had; any other board's
# image carries the board in its name, vfo-knob-<radio>-<board>, so it follows
# a channel of its own and a unit can never be offered -- or accept -- the
# other board's firmware. Their pin maps have nothing in common: one board's
# image on the other is a black screen and a silent I2C bus.
set(VFO_BOARD "knob18" CACHE STRING "The board this firmware is built for")
set(_vfo_boards knob18 lcd185c)
if(NOT VFO_BOARD IN_LIST _vfo_boards)
    message(FATAL_ERROR "VFO_BOARD=${VFO_BOARD}: there is firmware for ${_vfo_boards}")
endif()

# The panel drivers differ per board, and each brings its own managed
# component, so exactly one is built -- the same way as the radio clients.
if(VFO_BOARD STREQUAL "lcd185c")
    set(_vfo_panel panel_st77916)
    list(APPEND EXCLUDE_COMPONENTS panel)
else()
    set(_vfo_panel panel)
    list(APPEND EXCLUDE_COMPONENTS panel_st77916)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# The component manager records what it resolved in a lock file, and the two
# firmwares resolve different sets -- the icom one has no WebSocket client --
# so a shared file was rewritten by every build of the other radio, and a
# release refuses a tree that has changed. The AetherSDR firmware keeps
# dependencies.lock; any other radio's has its own, and so has any other
# board's: dependencies.<radio>.lock, dependencies.<board>.lock, or
# dependencies.<radio>-<board>.lock.
set(_vfo_lock "")
if(NOT VFO_RADIO STREQUAL "aethersdr")
    set(_vfo_lock "${VFO_RADIO}")
endif()
if(NOT VFO_BOARD STREQUAL "knob18")
    if(_vfo_lock)
        set(_vfo_lock "${_vfo_lock}-${VFO_BOARD}")
    else()
        set(_vfo_lock "${VFO_BOARD}")
    endif()
endif()
if(_vfo_lock)
    idf_build_set_property(DEPENDENCIES_LOCK dependencies.${_vfo_lock}.lock)
endif()
# For components that call the client (main, webcfg), and for code that looks
# different per radio: VFO_RADIO_AETHERSDR or VFO_RADIO_ICOM is defined as 1.
idf_build_set_property(VFO_RADIO_CLIENT ${_vfo_client})
string(TOUPPER "${VFO_RADIO}" _vfo_radio_uc)
idf_build_set_property(COMPILE_DEFINITIONS "VFO_RADIO_${_vfo_radio_uc}=1" APPEND)
# Likewise VFO_BOARD_KNOB18 or VFO_BOARD_LCD185C, and the panel component for
# main to require.
idf_build_set_property(VFO_BOARD ${VFO_BOARD})
idf_build_set_property(VFO_BOARD_PANEL ${_vfo_panel})
string(TOUPPER "${VFO_BOARD}" _vfo_board_uc)
idf_build_set_property(COMPILE_DEFINITIONS "VFO_BOARD_${_vfo_board_uc}=1" APPEND)
if(VFO_BOARD STREQUAL "knob18")
    project(vfo-knob-${VFO_RADIO})
else()
    project(vfo-knob-${VFO_RADIO}-${VFO_BOARD})
endif()
