add_custom_target(test_check ${CMAKE_COMMAND} -E env CTEST_OUTPUT_ON_FAILURE=1
        ${CMAKE_CTEST_COMMAND} -C $<CONFIG> --verbose
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR})

add_custom_target(juliet_tests)
add_dependencies(test_check juliet_tests)

file(GLOB TEST_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
foreach (testfile ${TEST_FILES})
  string(REGEX MATCH "^(.*)\\.[^.]*$" dummy ${testfile})
  set(test_name ${CMAKE_MATCH_1})
  message(STATUS "Found unit_test - " ${test_name})
  if (BUILD_JULIET_TESTS)
    add_executable(${test_name} ${CMAKE_CURRENT_SOURCE_DIR}/${testfile})
  else ()
    add_executable(${test_name} EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/${testfile})
  endif ()
  add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${test_name}>)

  target_include_directories(${test_name} PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/
   ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(${test_name} pthread)

  add_dependencies(juliet_tests ${test_name})
endforeach ()
