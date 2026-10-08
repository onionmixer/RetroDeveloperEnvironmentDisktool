# Regression tests: every tests/test_*.sh is one CTest test, run with the
# rdedisktool built here (RDEDISKTOOL). Enabled with -DBUILD_TESTS=ON; run
# with `ctest` in the build directory. tests/emu/ (manual emulator checks)
# is not included.
find_program(BASH_PROGRAM bash REQUIRED)

file(GLOB RDE_TEST_SCRIPTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.sh")
list(SORT RDE_TEST_SCRIPTS)

foreach(script IN LISTS RDE_TEST_SCRIPTS)
    get_filename_component(name "${script}" NAME_WE)
    add_test(NAME ${name}
             COMMAND ${CMAKE_COMMAND} -E env "RDEDISKTOOL=$<TARGET_FILE:rdedisktool>"
                     ${BASH_PROGRAM} "${script}"
             WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endforeach()
