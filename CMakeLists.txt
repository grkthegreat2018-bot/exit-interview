cmake_minimum_required(VERSION 3.21)
project(ExitInterview CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(raylib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

file(GLOB SRC CONFIGURE_DEPENDS "src/*.cpp")
add_executable(exit_interview ${SRC})

# file icon for Explorer/Steam - .rc is a no-op for the Linux CI build
if(WIN32)
    target_sources(exit_interview PRIVATE "${CMAKE_SOURCE_DIR}/exit_interview.rc")
endif()

target_link_libraries(exit_interview PRIVATE
    raylib
    nlohmann_json::nlohmann_json
    httplib::httplib
    OpenSSL::SSL
    OpenSSL::Crypto
)

# bake the source commit into the binary - the updater compares it against
# remote main so dev builds notice new commits even before any release
# exists. Generated per-build (not per-configure) so rebuilds after a new
# commit pick up the new sha without re-running cmake.
add_custom_target(ei_git_sha ALL
    COMMAND ${CMAKE_COMMAND}
        -DSRC=${CMAKE_SOURCE_DIR}
        -DOUT=${CMAKE_BINARY_DIR}/ei_sha.h
        -P ${CMAKE_SOURCE_DIR}/cmake/gen_sha.cmake
    BYPRODUCTS ${CMAKE_BINARY_DIR}/ei_sha.h
    COMMENT "stamping git sha")
add_dependencies(exit_interview ei_git_sha)
target_include_directories(exit_interview PRIVATE ${CMAKE_BINARY_DIR})

target_compile_definitions(exit_interview PRIVATE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    NOUSER
    NOGDI
    CPPHTTPLIB_OPENSSL_SUPPORT
)

if(MSVC)
    target_compile_options(exit_interview PRIVATE /W3 /MP /utf-8)
endif()

add_custom_command(TARGET exit_interview POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:exit_interview>/assets")
