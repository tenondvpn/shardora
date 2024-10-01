cmake_minimum_required(VERSION 3.8)

add_compile_options(-Wno-unused-parameter)

aux_source_directory(./ zjcvm_test_src)
add_executable(zjcvm_test ${zjcvm_test_src})

add_dependencies(zjcvm_test zjcvm)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(zjcvm_test PRIVATE zjcvm pthread gtest gtest_main gmock gmock_main pthread dl)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_link_libraries(zjcvm_test PRIVATE zjcvm gtest gtest_main gmock gmock_main)
endif()
