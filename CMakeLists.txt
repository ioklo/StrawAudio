cmake_minimum_required(VERSION 3.22)
project(StrawAudio VERSION 0.1.0 LANGUAGES CXX OBJCXX)
if(NOT APPLE)
  message(FATAL_ERROR "StrawAudio requires macOS 14.2 or later")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.2")

add_library(strawaudio_core STATIC
  src/Options.cpp src/Converter.cpp src/UdpSender.cpp
  src/Capture.mm src/Session.mm)
target_include_directories(strawaudio_core PUBLIC include)
target_compile_options(strawaudio_core PRIVATE -Wall -Wextra -Wpedantic
  "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_link_libraries(strawaudio_core PUBLIC
  "-framework Foundation" "-framework CoreAudio" "-framework AudioToolbox")

add_executable(strawaudio src/main.cpp)
target_link_libraries(strawaudio PRIVATE strawaudio_core)
target_link_options(strawaudio PRIVATE
  "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist")
set_property(TARGET strawaudio APPEND PROPERTY LINK_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist")
add_custom_command(TARGET strawaudio POST_BUILD
  COMMAND /usr/bin/codesign --force --sign - --timestamp=none
    --identifier dev.strawaudio.cli "$<TARGET_FILE:strawaudio>"
  VERBATIM)

include(CTest)
if(BUILD_TESTING)
  add_executable(strawaudio_tests tests/CoreTests.cpp)
  target_link_libraries(strawaudio_tests PRIVATE strawaudio_core)
  add_test(NAME core COMMAND strawaudio_tests)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME udp_loopback COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/loopback.py "$<TARGET_FILE:strawaudio>")
  endif()
endif()
