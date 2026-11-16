# LAME 3.100 のエンコーダ（libmp3lame）を 1 つの静的ライブラリにする。
#
# - ホスト: そのままの gcc。試験用の w2mhost がリンクする。
# - SGX / ファームウェア: cmake/arm-none-eabi.cmake で組む（Cortex-M33、newlib-nano）。
#   wav2mp3.sgx がリンクするほか、sgnano 本体のファームウェアからも同じ libmp3lame.a を使える。
#
# `config.h` は sgnano が書いたもの（configure は走らせない）。公開するヘッダは include/lame.h だけ。
include(GNUInstallDirs)
set(MP3LAME_VERSION "3.100")

file(GLOB LAME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libmp3lame/*.c")

add_library(mp3lame STATIC ${LAME_SOURCES})
add_library(mp3lame::mp3lame ALIAS mp3lame)

target_include_directories(mp3lame
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/libmp3lame)

target_compile_definitions(mp3lame PRIVATE HAVE_CONFIG_H)
# 取り込んだソースは警告を出さない（-w）。関数ごとの節にして、SGX では使わない関数を落とせるようにする。
target_compile_options(mp3lame PRIVATE -O2 -w -ffunction-sections -fdata-sections -fno-common)
set_target_properties(mp3lame PROPERTIES OUTPUT_NAME mp3lame)

# ---- インストール（ほかの CMake のプロジェクトから find_package(mp3lame) で使う）----
include(CMakePackageConfigHelpers)

install(TARGETS mp3lame
        EXPORT mp3lameTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/lame.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/COPYING"
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/README"
        "${CMAKE_CURRENT_SOURCE_DIR}/UPSTREAM.md"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/lame")

install(EXPORT mp3lameTargets
        NAMESPACE mp3lame::
        FILE mp3lameTargets.cmake
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mp3lame")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mp3lameConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/mp3lameConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mp3lame")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/mp3lameConfigVersion.cmake"
    VERSION "${MP3LAME_VERSION}"
    COMPATIBILITY SameMajorVersion)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/mp3lameConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/mp3lameConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mp3lame")
