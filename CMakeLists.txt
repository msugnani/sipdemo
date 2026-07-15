cmake_minimum_required(VERSION 3.16)
project(sipdemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(SIPDEMO_BUILD_TESTS "Build unit tests (fetches GoogleTest)" ON)
option(SIPDEMO_SANITIZE_THREAD "Build with ThreadSanitizer (non-MSVC)" OFF)

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(SIPDEMO_SANITIZE_THREAD)
    add_compile_options(-fsanitize=thread -g -O1)
    add_link_options(-fsanitize=thread)
  endif()
endif()

# ---------------------------------------------------------------------------
# Core library: every component the plan asks us to hand-write.
# ---------------------------------------------------------------------------
add_library(sipdemo_core
  src/net/UdpSocket.cpp
  src/sip/SipParser.cpp
  src/sip/SipUas.cpp
  src/sip/Sdp.cpp
  src/rtp/RtpPacket.cpp
  src/jitter/JitterBuffer.cpp
  src/codec/G711.cpp
  src/codec/Resample.cpp
  src/dtmf/Rfc4733.cpp
  src/ws/WsClient.cpp
  src/stats/RtpStats.cpp
)
target_include_directories(sipdemo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
target_link_libraries(sipdemo_core PUBLIC Threads::Threads)

if(WIN32)
  target_link_libraries(sipdemo_core PUBLIC ws2_32)
  target_compile_definitions(sipdemo_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# ---------------------------------------------------------------------------
# Executables: the live endpoint and the chaos RTP source.
# ---------------------------------------------------------------------------
add_executable(sipdemo src/app/main.cpp)
target_link_libraries(sipdemo PRIVATE sipdemo_core)

add_executable(rtp_sender src/tools/rtp_sender.cpp)
target_link_libraries(rtp_sender PRIVATE sipdemo_core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SIPDEMO_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  include(GoogleTest)

  add_executable(sipdemo_tests
    tests/sip_test.cpp
    tests/sdp_test.cpp
    tests/rtp_test.cpp
    tests/jitter_test.cpp
    tests/queue_test.cpp
    tests/g711_test.cpp
    tests/dtmf_test.cpp
    tests/resample_test.cpp
  )
  target_link_libraries(sipdemo_tests PRIVATE sipdemo_core GTest::gtest_main)
  gtest_discover_tests(sipdemo_tests)

  # Convenience target: `cmake --build build --target check` (or check-$Config).
  # On multi-config generators (VS), pass -C via the build script / ctest.
  add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG> --output-on-failure --timeout 60
    DEPENDS sipdemo_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running sipdemo unit tests (ctest)"
  )
endif()
