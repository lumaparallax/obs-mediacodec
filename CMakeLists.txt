cmake_minimum_required(VERSION 3.28...3.30)

project(obs-mediacodec VERSION 1.0.0)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android" AND NOT DEFINED ENV{TERMUX_VERSION})
  message(FATAL_ERROR "obs-mediacodec requires Android NDK, it cannot be built for standard Linux, Windows, or macOS.")
endif()

find_package(libobs REQUIRED)

add_library(obs-mediacodec MODULE)

target_sources(
  obs-mediacodec
  PRIVATE
    src/mediacodec-core.c
    src/obs-mediacodec.c
)

target_link_libraries(
  obs-mediacodec
  PRIVATE OBS::libobs mediandk android
)

set_target_properties(obs-mediacodec PROPERTIES PREFIX "")

include(GNUInstallDirs)

install(TARGETS obs-mediacodec
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins"
)

install(DIRECTORY data/locale
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/obs-mediacodec"
)
