cmake_minimum_required(VERSION 3.16)

project(EQVita)

set(A_NAME "${PROJECT_NAME}")
set(A_SELF eboot.bin)

set(VITA_APP_NAME "EQ Vita")
set(VITA_TITLEID "EQSP00001")
set(VITA_VERSION "01.13")

add_executable("${A_NAME}"
  main.c
  debug_screen/debugScreen.c
)

add_dependencies("${A_NAME}" eq_speaker_stubs)

target_compile_options("${A_NAME}" PRIVATE
  -Wall
  -Wextra
  -O2
  -std=c99
)

target_link_directories("${A_NAME}" PRIVATE
  ${CMAKE_BINARY_DIR}/plugin/eq_speaker_stubs
)

target_include_directories("${A_NAME}" PRIVATE
  ${CMAKE_SOURCE_DIR}/common
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/debug_screen
)

target_link_libraries("${A_NAME}"
  eqcommon
  EQSpeaker_stub
  SceAVConfig_stub
  SceCtrl_stub
  SceDisplay_stub
  SceLibKernel_stub
  SceIofilemgr_stub
)

vita_create_self("${A_SELF}" "${A_NAME}" UNSAFE)
vita_create_vpk("${A_NAME}.vpk" "${VITA_TITLEID}" "${A_SELF}"
  VERSION "${VITA_VERSION}"
  NAME "${VITA_APP_NAME}"
  FILE sce_sys/icon0.png sce_sys/icon0.png
       sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
       sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
       sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)

if(TARGET "${A_NAME}.vpk-vpk" AND TARGET "${A_SELF}-self")
  add_dependencies("${A_NAME}.vpk-vpk" "${A_SELF}-self")
endif()
