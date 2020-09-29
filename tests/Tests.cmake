# enable tests
enable_testing()

# include the google test
include(GoogleTest)
find_package(GTest REQUIRED)

# get the current directory
get_filename_component(unit-test-path ${CMAKE_CURRENT_LIST_FILE} DIRECTORY)

# compile all the objects
file(GLOB files "${unit-test-path}/*.cc")

# sorts files alphabetically because some tests require
# files created in previous tests
list(SORT files)
add_custom_target(unit-tests)
foreach(file ${files})

    # grab the name of the test without the extension
    get_filename_component(fileName "${file}" NAME_WE)

    # create the test executable
    add_executable(${fileName} ${file})

    # link the stuff we need
    target_link_libraries(${fileName} ${GTEST_BOTH_LIBRARIES} gmock ${CMAKE_THREAD_LIBS_INIT})
    target_link_libraries(${fileName} bbts-common)
    target_compile_definitions(${fileName} PRIVATE -DGTEST_LINKED_AS_SHARED_LIBRARY )

    # add the test as a dependency of the unit test target
    add_dependencies(unit-tests ${fileName})

endforeach()