cmake_minimum_required(VERSION 3.10)
project(libraptor VERSION 1.0.0 LANGUAGES C CXX)

# This project can use C11, but will gracefully decay down to C89.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_C_EXTENSIONS OFF)

# This project requires C++11.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RAPTOR_VERSION_MAJOR "2")
set(RAPTOR_VERSION_MINOR "0")
set(RAPTOR_VERSION_PATCH "0")
set(RAPTOR_VERSION
    "${RAPTOR_VERSION_MAJOR}.${RAPTOR_VERSION_MINOR}.${RAPTOR_VERSION_PATCH}")

message(STATUS "Raptor-lite Version     : ${RAPTOR_VERSION}")

if(WIN32 AND MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/MT")
    set(CMAKE_CXX_FLAGS_DEBUG "/MTd")
    set(RAPTOR_BASELIB_LIBRARIES wsock32 ws2_32)
else()
    set(RAPTOR_BASELIB_LIBRARIES m pthread)
endif()

# Build option
option(RAPTOR_BUILD_ALLTESTS     "Build raptor-lite's all unit tests" ON)
option(RAPTOR_INSTALL "Install raptor-lite's header and library" ON)

if(WIN32 AND MSVC)
    add_definitions(/W4)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DUNICODE -D_UNICODE)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
else()
    add_definitions(-Wall)
endif()

include_directories(
    "${PROJECT_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/include"
)

# static library
add_library(raptor-lite STATIC "")

if (WIN32)
    set(
        RAPTOR_ENGINE_SOURCE
        #IOCP
        "${PROJECT_SOURCE_DIR}/src/windows/connection.cc"
        "${PROJECT_SOURCE_DIR}/src/windows/iocp_thread.cc"
        "${PROJECT_SOURCE_DIR}/src/windows/iocp.cc"
        "${PROJECT_SOURCE_DIR}/src/windows/socket_setting.cc"
        "${PROJECT_SOURCE_DIR}/src/windows/tcp_connector.cc"
        "${PROJECT_SOURCE_DIR}/src/windows/tcp_listener.cc"
    )
else()
    set(
        RAPTOR_ENGINE_SOURCE
        #EPOLL
        "${PROJECT_SOURCE_DIR}/src/linux/connection.cc"
        "${PROJECT_SOURCE_DIR}/src/linux/epoll_thread.cc"
        "${PROJECT_SOURCE_DIR}/src/linux/epoll.cc"
        "${PROJECT_SOURCE_DIR}/src/linux/socket_setting.cc"
        "${PROJECT_SOURCE_DIR}/src/linux/tcp_connector.cc"
        "${PROJECT_SOURCE_DIR}/src/linux/tcp_listener.cc"
    )
endif()

set(
    RAPTOR_COMMON_SOURCE
    "${PROJECT_SOURCE_DIR}/src/common/container_impl.cc"
    "${PROJECT_SOURCE_DIR}/src/common/endpoint_impl.cc"
    "${PROJECT_SOURCE_DIR}/src/common/host_port.cc"
    "${PROJECT_SOURCE_DIR}/src/common/resolve_address.cc"
    "${PROJECT_SOURCE_DIR}/src/common/socket_util.cc"
)

set(
    RAPTOR_UTILS_SOURCE
    "${PROJECT_SOURCE_DIR}/src/utils/color.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/cpu.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/list_entry.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/log.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/mpscq.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/slice_buffer.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/slice.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/status.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/string.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/sync.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/thread.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/time.cc"
    "${PROJECT_SOURCE_DIR}/src/utils/timer.cc"
)

set(
    RAPTOR_SURFACE_SOURCE
    "${PROJECT_SOURCE_DIR}/src/surface/acceptor.cc"
    "${PROJECT_SOURCE_DIR}/src/surface/connector.cc"
    "${PROJECT_SOURCE_DIR}/src/surface/container.cc"
    "${PROJECT_SOURCE_DIR}/src/surface/endpoint.cc"
    "${PROJECT_SOURCE_DIR}/src/surface/init.cc"
)

target_sources(raptor-lite
    PRIVATE
    ${RAPTOR_ENGINE_SOURCE}
    ${RAPTOR_COMMON_SOURCE}
    ${RAPTOR_UTILS_SOURCE}
    ${RAPTOR_SURFACE_SOURCE}
)

target_link_libraries(raptor-lite
    ${RAPTOR_BASELIB_LIBRARIES}
)

if(RAPTOR_BUILD_ALLTESTS)
    enable_testing()

    function(raptor_test_func test_file)
    get_filename_component(test_target_name "${test_file}" NAME_WE)

    add_executable("${test_target_name}" "")
    target_sources("${test_target_name}"
        PRIVATE
        "${PROJECT_SOURCE_DIR}/src/utils/testutil.cc"
        "${PROJECT_SOURCE_DIR}/src/utils/testutil.h"

        "${test_file}"
    )
    target_link_libraries("${test_target_name}" raptor-lite)
    target_include_directories("${test_target_name}"
        PRIVATE
        "${PROJECT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/include"
    )
    add_test(NAME "${test_target_name}" COMMAND "${test_target_name}")
    endfunction(raptor_test_func)

    # Add unit test source files below
    raptor_test_func("${PROJECT_SOURCE_DIR}/example/tcp_client_test.cc")
    raptor_test_func("${PROJECT_SOURCE_DIR}/example/tcp_server_test.cc")
    raptor_test_func("${PROJECT_SOURCE_DIR}/example/echo_client.cc")
    raptor_test_func("${PROJECT_SOURCE_DIR}/example/echo_server.cc")

endif(RAPTOR_BUILD_ALLTESTS)

if(RAPTOR_INSTALL)
    include(GNUInstallDirs)
    install(TARGETS raptor-lite
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    install(
        FILES
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/impl/acceptor.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/impl/connector.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/impl/container.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/impl/endpoint.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/impl/event.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/impl/handler.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/impl/property.h"

        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/raptor-lite/impl
    )
    install(
        FILES
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/atomic.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/color.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/cpu.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/list_entry.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/log.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/mpscq.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/ref_counted_ptr.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/ref_counted.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/slice_buffer.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/slice.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/status.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/sync.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/thread.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/time.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/timer.h"
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/utils/useful.h"

        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/raptor-lite/utils
    )
    install(
        FILES
            "${PROJECT_SOURCE_DIR}/include/raptor-lite/raptor-lite.h"

        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/raptor-lite
    )
endif(RAPTOR_INSTALL)
