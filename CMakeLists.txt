cmake_minimum_required(VERSION 3.16)

option(EQVITA_HOST_TESTS "Build host-side unit tests instead of Vita artifacts" OFF)

if(EQVITA_HOST_TESTS)
  project(EQVitaHostTests C)
  set(CMAKE_C_STANDARD 99)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  enable_testing()
  add_subdirectory(tests)
  return()
endif()

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

get_filename_component(EQVITA_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE)
get_filename_component(EQVITA_VITASDK_SHARE "${EQVITA_TOOLCHAIN_FILE}" DIRECTORY)
if(NOT EXISTS "${EQVITA_VITASDK_SHARE}/vita.cmake")
  message(FATAL_ERROR "Unable to locate vita.cmake next to CMAKE_TOOLCHAIN_FILE. Pass the matching VitaSDK toolchain file or set VITASDK.")
endif()

project(eq_speaker)
include("${EQVITA_VITASDK_SHARE}/vita.cmake" REQUIRED)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")

add_subdirectory(common)
add_subdirectory(plugin)
add_subdirectory(app)
