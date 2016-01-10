cmake_minimum_required (VERSION 2.6)

project (cr75 C)
set(cr75_VERSION 0.0.1)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/CMakeModules")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG")

find_package(PCSCLite REQUIRED)
include_directories(${PCSCLITE_INCLUDE_DIR})

find_package(LibUSB REQUIRED)
include_directories(${LIBUSB_1_INCLUDE_DIR})

if (${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    add_definitions(-DRESPONSECODE_DEFINED_IN_WINTYPES_H)
    set(cr75_BUNDLE_EXECDIR "MacOS")
else()
    set(cr75_BUNDLE_EXECDIR ${CMAKE_SYSTEM_NAME})
endif()

add_library(cr75 SHARED ifdhandler.c)
target_link_libraries(cr75 ${LIBUSB_1_LIBRARY})

configure_file(Info.plist Info.plist)

install(TARGETS cr75
    DESTINATION ${PCSCLITE_BUNDLE_DIRECTORY}/libcr75.bundle/Contents/${cr75_BUNDLE_EXECDIR})
install(FILES ${CMAKE_BINARY_DIR}/Info.plist
    DESTINATION ${PCSCLITE_BUNDLE_DIRECTORY}/libcr75.bundle/Contents)
