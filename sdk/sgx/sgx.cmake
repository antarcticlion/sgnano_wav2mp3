# sgnano の外部実行ファイル（SGX）を組む（2026-10-24、docs/sgx_abi.md）。
#   sgx_add_program(<target> NAME <表示名> [FLAGS <数>] [STACK <バイト>] [LIBS <...>] SOURCES <...>)
# できるもの: ${CMAKE_BINARY_DIR}/sgx/<target>.sgx（像）と <target>.elf（読み解き用）。ファームとは結ばない（API の表だけを使う）
set(SGX_SDK_DIR ${CMAKE_CURRENT_LIST_DIR})

function(sgx_add_program target)
    # LIBS: プログラムが使う静的ライブラリ（2026-11-17、sgnano_wav2mp3 の SDK と同じ。LAME を m / c / gcc より先に置くため）
    cmake_parse_arguments(P "" "NAME;FLAGS;STACK" "SOURCES;DEFINES;INCLUDES;OPTIONS;LIBS" ${ARGN})
    if(NOT P_FLAGS)
        set(P_FLAGS 0)
    endif()
    if(NOT P_STACK)
        set(P_STACK 0)
    endif()
    add_executable(${target} ${P_SOURCES} ${SGX_SDK_DIR}/sgx_crt0.c ${SGX_SDK_DIR}/sgx_libc.c ${SGX_SDK_DIR}/sgx_dmath.S)
    set_target_properties(${target} PROPERTIES SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/sgx)
    target_include_directories(${target} PRIVATE ${SGX_SDK_DIR}/include ${P_INCLUDES})
    target_compile_definitions(${target} PRIVATE SGX_APP_NAME="${P_NAME}" SGX_APP_FLAGS=${P_FLAGS}u SGX_APP_STACK=${P_STACK}u ${P_DEFINES})
    target_compile_options(${target} PRIVATE -O2 -ffunction-sections -fdata-sections -fno-common ${P_OPTIONS})
    # 起動のファイルは使わない（入口は sgx_crt0.c）。libc は newlib-nano（OS に頼る口は sgx_libc.c）
    # -q（--emit-relocs）: 再配置の情報を残す（sgx_link.py が表を作る）
    target_link_options(${target} PRIVATE -T${SGX_SDK_DIR}/sgx.ld -nostartfiles --specs=nano.specs -Wl,--gc-sections -Wl,-q
                        -Wl,-Map=${CMAKE_BINARY_DIR}/sgx/${target}.map)
    # LIBS を先に置く（libmp3lame.a の中の powf / log10f を、あとの -lm で解決するため）
    target_link_libraries(${target} PRIVATE ${P_LIBS} m c gcc)
    # 倍精度の計算をファームの DCP の版へ（sgx_dmath.S、ABI 1.1、2026-10-25）。並びは include/sgx_dfuncs.inc
    file(STRINGS ${SGX_SDK_DIR}/include/sgx_dfuncs.inc _sgx_df REGEX "^SGX_DF\\(")
    foreach(_l ${_sgx_df})
        string(REGEX REPLACE "^SGX_DF\\(([A-Za-z0-9_]+)\\).*" "\\1" _n "${_l}")
        target_link_options(${target} PRIVATE -Wl,--wrap=${_n})
    endforeach()
    # C++ の例外の置き場を SRAM へ（sgx_libc.c。PSRAM では libsupc++ の参照の数の不可分の命令が効かない。2026-11-06）
    foreach(_n __cxa_allocate_exception __cxa_free_exception __cxa_allocate_dependent_exception __cxa_free_dependent_exception)
        target_link_options(${target} PRIVATE -Wl,--wrap=${_n})
    endforeach()
    string(REGEX REPLACE "objcopy$" "size" _sgx_size "${CMAKE_OBJCOPY}")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND python3 ${SGX_SDK_DIR}/sgx_link.py $<TARGET_FILE:${target}> ${CMAKE_BINARY_DIR}/sgx/${target}.sgx
        COMMAND ${_sgx_size} $<TARGET_FILE:${target}>
        BYPRODUCTS ${CMAKE_BINARY_DIR}/sgx/${target}.sgx
        COMMENT "SGX: ${target}.sgx")
endfunction()

# オーバーレイ（SDK の部品、2026-11-06、include/sgx_overlay.h）
#   sgx_add_overlay_module(<target> SOURCES <...> [INCLUDES <...>] [DEFINES <...>])
#     番地 0 に結んだモジュールの ELF（${CMAKE_BINARY_DIR}/sgx/ovl/<target>.elf）。libc は結ばない（常駐の機能は host の表から）
#   sgx_add_overlay_pack(<target> OUTPUT <file.ovl> MODULES <name>=<module target> ...)
#     モジュールを 1 つのパックにまとめる（sgx_ovl_pack.py）。常駐のプログラムは sgx_overlay.c を SOURCES に足して読む
function(sgx_add_overlay_module target)
    cmake_parse_arguments(P "" "" "SOURCES;INCLUDES;DEFINES" ${ARGN})
    add_executable(${target} ${P_SOURCES})
    set_target_properties(${target} PROPERTIES SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/sgx/ovl)
    target_include_directories(${target} PRIVATE ${SGX_SDK_DIR}/include ${P_INCLUDES})
    target_compile_definitions(${target} PRIVATE ${P_DEFINES})
    target_compile_options(${target} PRIVATE -O2 -ffunction-sections -fdata-sections -fno-common)
    target_link_options(${target} PRIVATE -T${SGX_SDK_DIR}/sgx_ovl.ld -nostartfiles -nostdlib -Wl,--gc-sections -Wl,-q)
    target_link_libraries(${target} PRIVATE gcc)
endfunction()

function(sgx_add_overlay_pack target)
    cmake_parse_arguments(P "" "OUTPUT" "MODULES" ${ARGN})
    set(_args "")
    set(_deps "")
    foreach(_m ${P_MODULES})
        string(REGEX REPLACE "=.*$" "" _name "${_m}")
        string(REGEX REPLACE "^[^=]*=" "" _t "${_m}")
        list(APPEND _args "${_name}=$<TARGET_FILE:${_t}>")
        list(APPEND _deps ${_t})
    endforeach()
    add_custom_command(OUTPUT ${P_OUTPUT}
        COMMAND python3 ${SGX_SDK_DIR}/sgx_ovl_pack.py ${P_OUTPUT} ${_args}
        DEPENDS ${_deps} ${SGX_SDK_DIR}/sgx_ovl_pack.py
        COMMENT "SGX overlay pack: ${P_OUTPUT}")
    add_custom_target(${target} ALL DEPENDS ${P_OUTPUT})
endfunction()

# sgx_link_cxx(<target>): C++ のライブラリを例外の巻き戻しの表つきでつなぐ（2026-11-06）。
#   --specs=nano.specs は -lstdc++ / -lsupc++ を libstdc++_nano.a / libsupc++_nano.a に差し替えるが、nano 版は例外なしで組まれていて
#   .ARM.exidx が無い。__cxa_throw の枠から先へ巻き戻せず、どの catch にも届かず terminate になる（POV-Ray の code 134）。
#   そこで同じマルチライブラリの完全版をパスで直に渡す（パスで渡せば nano.specs の差し替えを受けない）
function(sgx_link_cxx target)
    separate_arguments(_f UNIX_COMMAND "${CMAKE_CXX_FLAGS}")
    foreach(_lib libstdc++.a libsupc++.a)
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${_f} -print-file-name=${_lib} OUTPUT_VARIABLE _p OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT IS_ABSOLUTE "${_p}" OR NOT EXISTS "${_p}")
            message(FATAL_ERROR "sgx_link_cxx: ${_lib} not found (${_p})")
        endif()
        list(APPEND _libs "${_p}")
    endforeach()
    target_link_libraries(${target} PRIVATE ${_libs} m c gcc)
endfunction()
