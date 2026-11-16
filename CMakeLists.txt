# sgnano_wav2mp3: WAV / VGM / VGZ -> MP3（sgnano の外部実行ファイル wav2mp3.sgx）と LAME 3.100 の単体ビルド。
#
# ホスト（試験用）:
#   cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/host
#   ctest --test-dir build/host --output-on-failure
#
# SGX（実機用、arm-none-eabi-gcc）:
#   cmake -S . -B build/sgx -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
#   cmake --build build/sgx
#   できるもの: build/sgx/sgx/wav2mp3.sgx、build/sgx/third_party/lame/libmp3lame.a
#
# LAME は LGPL-2.0-or-later。sgnano のファームウェア / wav2mp3.sgx がリンクする場合の
# 対応するソースとビルド手順をこのリポジトリで配布する（README.md の「LGPL」）。
cmake_minimum_required(VERSION 3.21)
project(sgnano_wav2mp3 VERSION 1.0.0 LANGUAGES C ASM)

include(GNUInstallDirs)

if(CMAKE_CROSSCOMPILING)
    set(_wav2mp3_host_default OFF)
    set(_wav2mp3_sgx_default ON)
else()
    set(_wav2mp3_host_default ON)
    set(_wav2mp3_sgx_default OFF)
endif()

option(WAV2MP3_BUILD_HOST "Build the host tools (w2mhost / mp3dec)" ${_wav2mp3_host_default})
option(WAV2MP3_BUILD_SGX "Build wav2mp3.sgx for a real sgnano (needs arm-none-eabi-gcc)" ${_wav2mp3_sgx_default})
option(WAV2MP3_BUILD_TESTS "Register the host test with CTest" ON)

# LAME 3.100（LGPL）。ホストでも SGX でも、この 1 つのターゲットを組み立てる。
# インストールすると libmp3lame.a・lame.h・CMake のパッケージ（mp3lame::mp3lame）を出す。
add_subdirectory(third_party/lame)

if(WAV2MP3_BUILD_SGX)
    if(NOT CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR
            "WAV2MP3_BUILD_SGX needs a cross compiler. "
            "Use -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake")
    endif()
    set(SGNANO_SGX_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk/sgx" CACHE PATH "sgnano SGX SDK directory")
    if(NOT EXISTS "${SGNANO_SGX_SDK_DIR}/sgx.cmake")
        message(FATAL_ERROR "SGNANO_SGX_SDK_DIR does not contain sgx.cmake: ${SGNANO_SGX_SDK_DIR}")
    endif()
    include("${SGNANO_SGX_SDK_DIR}/sgx.cmake")

    # FLAGS 1 = SGX_F_ESC_KEY（ESC はプログラムへ渡す。wav2mp3 が途中までの出力を消す）。
    # LAME はフレームごとにスタックを 30〜40KB 使うので 128KB（64KB では flush で溢れた）。
    # LAME は別の静的ライブラリ（libmp3lame.a）としてリンクする。LGPL の「差し替えて組み直せる」形。
    # LIBS に置くと、libmp3lame.a が -lm / -lc / -lgcc より前に来る（powf / log10f のため）。
    sgx_add_program(wav2mp3 NAME "WAV2MP3" FLAGS 1 STACK 131072
        SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/apps/wav2mp3/main_sgx.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/apps/wav2mp3/w2m.c"
        INCLUDES
            "${CMAKE_CURRENT_SOURCE_DIR}/apps/wav2mp3"
        LIBS mp3lame
        OPTIONS -O2 -w)
endif()

if(WAV2MP3_BUILD_HOST)
    add_executable(w2mhost
        "${CMAKE_CURRENT_SOURCE_DIR}/host/w2mhost.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/apps/wav2mp3/w2m.c")
    target_include_directories(w2mhost PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/apps/wav2mp3")
    target_link_libraries(w2mhost PRIVATE mp3lame m)
    target_compile_options(w2mhost PRIVATE -O2 -Wall -Wextra)

    add_executable(mp3dec "${CMAKE_CURRENT_SOURCE_DIR}/host/mp3dec.c")
    target_include_directories(mp3dec PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minimp3")
    target_link_libraries(mp3dec PRIVATE m)
    target_compile_options(mp3dec PRIVATE -O2 -Wall -Wextra)

    if(WAV2MP3_BUILD_TESTS)
        enable_testing()
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME wav2mp3
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_wav2mp3.py"
                        --w2mhost $<TARGET_FILE:w2mhost>
                        --mp3dec $<TARGET_FILE:mp3dec>)
        else()
            message(WARNING "python3 not found: the host test is not registered with CTest")
        endif()
    endif()
endif()

# 文書とライセンスも入れておく（LGPL の対応ソースとして配るため）。
install(FILES README.md LICENSE NOTICE
        DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(DIRECTORY LICENSES/
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/LICENSES")
