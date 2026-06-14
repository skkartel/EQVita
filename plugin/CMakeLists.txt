cmake_minimum_required(VERSION 3.16)

project(eq_speaker_plugin)

add_executable(eq_speaker
  main.c
  dsp.c
)

target_compile_options(eq_speaker PRIVATE
  -Wall
  -Wextra
  -O2
  -std=gnu99
  -mtune=cortex-a9
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

vita_create_self(eq_speaker.skprx eq_speaker CONFIG exports.yml UNSAFE)
vita_create_stubs(eq_speaker_stubs eq_speaker.skprx exports.yml KERNEL)

if(TARGET eq_speaker.skprx-self)
  add_dependencies(eq_speaker_stubs eq_speaker.skprx-self)
endif()
