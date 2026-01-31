cmake_minimum_required(VERSION 3.15)
project(magic_cpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
if (APPLE)
    include(CheckLinkerFlag)
    check_linker_flag(C "-Wl,-no_warn_duplicate_libraries" HAVE_NO_WARN_DUPLICATE)
    if(HAVE_NO_WARN_DUPLICATE)
        add_link_options("-Wl,-no_warn_duplicate_libraries")
    endif()
endif ()

enable_testing()

function(add_magic_cpp_target name)
    add_executable(${name} "src/${name}.cpp")
    add_test(NAME "${name}" COMMAND "${name}")
endfunction()

file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp)
foreach (SOURCE ${SOURCES})
    get_filename_component(NAME ${SOURCE} NAME_WE)
    add_magic_cpp_target(${NAME})
endforeach ()

if (CMAKE_CONFIGURATION_TYPES)
    add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND}
            --force-new-ctest-process --output-on-failure
            --build-config "$<CONFIGURATION>")
else ()
    add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND}
            --force-new-ctest-process --output-on-failure)
endif ()

find_program(CLANG_FORMAT clang-format NO_CMAKE_PATH)
if (CLANG_FORMAT)
    file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS src/*.cpp)
    add_custom_target(
            format
            COMMAND "${CLANG_FORMAT}" -i --verbose ${ALL_SOURCES}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_custom_target(
            check-format
            COMMAND "${CLANG_FORMAT}" --dry-run --Werror --verbose ${ALL_SOURCES}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif ()
