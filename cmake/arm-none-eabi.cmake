# RP2350（Cortex-M33）のツールチェーン。sgnano の SDK とファームウェアに合わせる。
# FPU あり（+fp+dsp）・softfp。これを sgnano 側とそろえないと、浮動小数点の印（Tag_ABI_HardFP_use）で
# リンカが止まることがある。
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(_wav2mp3_arm_flags "-mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp -mfloat-abi=softfp")
set(CMAKE_C_FLAGS_INIT "${_wav2mp3_arm_flags}")
set(CMAKE_ASM_FLAGS_INIT "${_wav2mp3_arm_flags}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_wav2mp3_arm_flags}")
