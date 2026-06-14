add_executable(test_eq_shared
  test_eq_shared.c
)

target_include_directories(test_eq_shared PRIVATE
  ${CMAKE_SOURCE_DIR}
)

add_test(NAME eq_shared COMMAND test_eq_shared)

add_executable(test_dsp
  test_dsp.c
  ${CMAKE_SOURCE_DIR}/plugin/dsp.c
)

target_include_directories(test_dsp PRIVATE
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(test_dsp PRIVATE m)

add_test(NAME dsp COMMAND test_dsp)

add_executable(test_port_state
  test_port_state.c
)

target_include_directories(test_port_state PRIVATE
  ${CMAKE_SOURCE_DIR}
)

add_test(NAME port_state COMMAND test_port_state)

add_executable(test_port_registry
  test_port_registry.c
  ${CMAKE_SOURCE_DIR}/plugin/dsp.c
)

target_include_directories(test_port_registry PRIVATE
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(test_port_registry PRIVATE m)

add_test(NAME port_registry COMMAND test_port_registry)

add_executable(test_app_help_text
  test_app_help_text.c
)

target_compile_definitions(test_app_help_text PRIVATE
  EQVITA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME app_help_text COMMAND test_app_help_text)
