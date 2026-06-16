cmake_minimum_required(VERSION 3.16)

project(eq_speaker_plugin)

option(EQVITA_AUDIO_DIAGNOSTICS "Enable rich per-output audio diagnostics in the plugin hook" OFF)

add_executable(eq_speaker
  main.c
  dsp.c
)

target_compile_options(eq_speaker PRIVATE
  -Wall
  -Wextra
  -std=gnu99
  -mcpu=cortex-a9
  -mfpu=neon
  -mfloat-abi=hard
  -mthumb
  $<$<CONFIG:Release>:-O3>
)

target_link_options(eq_speaker PRIVATE
  -nostdlib
)

target_link_libraries(eq_speaker
  eqcommon
  SceAudio_stub
  SceIofilemgrForDriver_stub
  SceSysmemForDriver_stub
  SceThreadmgrForDriver_stub
  SceSysclibForDriver_stub
  SceCtrlForDriver_stub
  taihenForKernel_stub
  taihenModuleUtils_stub
  m
  gcc
)

if(EQVITA_AUDIO_DIAGNOSTICS)
  target_compile_definitions(eq_speaker PRIVATE EQVITA_AUDIO_DIAGNOSTICS=1)
endif()

vita_create_self(eq_speaker.skprx eq_speaker CONFIG exports.yml UNSAFE)
vita_create_stubs(eq_speaker_stubs eq_speaker.skprx exports.yml KERNEL)

if(TARGET eq_speaker.skprx-self)
  add_dependencies(eq_speaker_stubs eq_speaker.skprx-self)
endif()
