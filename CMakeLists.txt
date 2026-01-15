cmake_minimum_required(VERSION 3.16)

# MotionCore can be used in two modes:
# 1) As a standalone project: cmake -S MotionCore -B build
# 2) As a subproject from the repository root.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MotionCore LANGUAGES C CXX)

    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)

    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    # Feature toggles (keep defaults consistent with the mono-repo root).
    option(ENABLE_OMPL "Enable OMPL integration" OFF)
    option(ENABLE_FCL  "Enable FCL integration"  ON)
    option(WXZ_ENFORCE_LEGACY_COMMUNICATION_INTERNAL_ONLY
        "When ON, including communication/communication.h requires WXZ_LEGACY_COMMUNICATION_ALLOWED=1 (internal/tests only)."
        ON)

    # Install/profile options (standalone defaults are SDK-friendly).
    option(WXZ_INSTALL_DOCS "Install markdown docs" ON)
    option(WXZ_INSTALL_DEV "Install headers and CMake package config" ON)
else()
    # As a subproject, options/variables are expected to be set by the parent.
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)
    if(NOT DEFINED WXZ_INSTALL_DOCS)
        set(WXZ_INSTALL_DOCS ON)
    endif()
    if(NOT DEFINED WXZ_INSTALL_DEV)
        set(WXZ_INSTALL_DEV ON)
    endif()

endif()

set(WXZ_MOTIONCORE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Repository root (mono-repo) helper.
get_filename_component(WXZ_REPO_ROOT "${WXZ_MOTIONCORE_DIR}" DIRECTORY)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Local developer workflow: install into the build directory by default.
# This avoids creating an extra "_install" folder and keeps MotionCore/Workstation independent.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}" CACHE PATH "Install prefix (default: build directory)" FORCE)
endif()

# Install layout (Option A: MotionCore as the primary namespace)
if(NOT DEFINED WXZ_INSTALL_INCLUDEDIR)
    set(WXZ_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR}/MotionCore)
endif()
if(NOT DEFINED WXZ_INSTALL_DOCDIR)
    set(WXZ_INSTALL_DOCDIR ${CMAKE_INSTALL_DATAROOTDIR}/MotionCore/docs)
endif()
if(NOT DEFINED WXZ_INSTALL_CONFIGDIR)
    set(WXZ_INSTALL_CONFIGDIR ${CMAKE_INSTALL_DATAROOTDIR}/MotionCore/config)
endif()
if(NOT DEFINED WXZ_INSTALL_RESOURCEDIR)
    set(WXZ_INSTALL_RESOURCEDIR ${CMAKE_INSTALL_DATAROOTDIR}/MotionCore/resources)
endif()
if(NOT DEFINED WXZ_INSTALL_PLUGINSDIR)
    set(WXZ_INSTALL_PLUGINSDIR ${CMAKE_INSTALL_LIBDIR}/MotionCore/plugins)
endif()
if(NOT DEFINED WXZ_PACKAGE_CONFIG_DEST)
    set(WXZ_PACKAGE_CONFIG_DEST ${CMAKE_INSTALL_LIBDIR}/cmake/MotionCore)
endif()

find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp REQUIRED)
if(TARGET yaml-cpp::yaml-cpp)
    set(YAMLCPP_LIB yaml-cpp::yaml-cpp)
elseif(TARGET yaml-cpp)
    set(YAMLCPP_LIB yaml-cpp)
else()
    find_library(YAMLCPP_LIB_NAMED yaml-cpp)
    if(YAMLCPP_LIB_NAMED)
        set(YAMLCPP_LIB ${YAMLCPP_LIB_NAMED})
    else()
        set(YAMLCPP_LIB yaml-cpp)
    endif()
endif()

# Optional OMPL/FCL via pkg-config, consistent with root behaviour.
if(ENABLE_OMPL)
    pkg_check_modules(OMPL QUIET ompl)
    if(NOT OMPL_FOUND)
        add_definitions(-DNO_OMPL)
        message(STATUS "OMPL not found via pkg-config; building without OMPL")
        set(ENABLE_OMPL OFF)
    endif()
else()
    add_definitions(-DNO_OMPL)
endif()

if(ENABLE_FCL)
    pkg_check_modules(FCL QUIET fcl)
    if(NOT FCL_FOUND)
        add_definitions(-DNO_FCL)
        message(STATUS "FCL not found via pkg-config; building without FCL")
        set(ENABLE_FCL OFF)
    endif()
endif()

# Fast-DDS (Fast-RTPS: fastrtps + Fast-CDR: fastcdr)
# Policy: pin to the repository-managed Fast-DDS v2.14.4. Builds must not
# silently pick up conflicting system installations.

set(WXZ_FASTDDS_VERSION "2.14.4")
set(WXZ_FASTCDR_VERSION "2.2.0")

set(_wxz_pinned_fastdds_prefix "${WXZ_REPO_ROOT}/depends/eprosima_fastdds/v${WXZ_FASTDDS_VERSION}/_install")

if(NOT DEFINED WXZ_FASTDDS_PREFIX OR "${WXZ_FASTDDS_PREFIX}" STREQUAL "")
    if(EXISTS "${_wxz_pinned_fastdds_prefix}")
        set(WXZ_FASTDDS_PREFIX "${_wxz_pinned_fastdds_prefix}" CACHE PATH "Pinned Fast-DDS prefix (v${WXZ_FASTDDS_VERSION})" FORCE)
    else()
        set(WXZ_FASTDDS_PREFIX "" CACHE PATH "Pinned Fast-DDS prefix (v${WXZ_FASTDDS_VERSION})" FORCE)
    endif()
endif()

if(NOT EXISTS "${WXZ_FASTDDS_PREFIX}")
    message(FATAL_ERROR
        "Pinned Fast-DDS prefix not found.\n"
        "Expected: ${_wxz_pinned_fastdds_prefix}\n"
        "Please ensure Fast-DDS v${WXZ_FASTDDS_VERSION} (and Fast-CDR v${WXZ_FASTCDR_VERSION}) is installed into that directory."
    )
endif()

set(_wxz_fastcdr_hint_paths
    "${WXZ_FASTDDS_PREFIX}/lib/cmake/fastcdr"
    "${WXZ_FASTDDS_PREFIX}/lib64/cmake/fastcdr"
    "${WXZ_FASTDDS_PREFIX}/lib/cmake"
    "${WXZ_FASTDDS_PREFIX}/lib64/cmake"
    "${WXZ_FASTDDS_PREFIX}"
)

set(_wxz_fastrtps_hint_paths
    "${WXZ_FASTDDS_PREFIX}/share/fastrtps/cmake"
    "${WXZ_FASTDDS_PREFIX}/lib/cmake/fastrtps"
    "${WXZ_FASTDDS_PREFIX}/lib64/cmake/fastrtps"
    "${WXZ_FASTDDS_PREFIX}/lib/cmake"
    "${WXZ_FASTDDS_PREFIX}/lib64/cmake"
    "${WXZ_FASTDDS_PREFIX}"
)

if(NOT TARGET fastcdr AND NOT TARGET fastcdr::fastcdr)
    find_package(fastcdr ${WXZ_FASTCDR_VERSION} EXACT REQUIRED CONFIG
        PATHS ${_wxz_fastcdr_hint_paths}
        NO_DEFAULT_PATH
    )
endif()

if(DEFINED fastcdr_DIR AND NOT "${fastcdr_DIR}" MATCHES "^${WXZ_FASTDDS_PREFIX}(/|$)")
    message(FATAL_ERROR "fastcdr_DIR must be under WXZ_FASTDDS_PREFIX. fastcdr_DIR=${fastcdr_DIR} WXZ_FASTDDS_PREFIX=${WXZ_FASTDDS_PREFIX}")
endif()

if(NOT TARGET fastrtps AND NOT TARGET FastRTPS::fastrtps)
    find_package(fastrtps ${WXZ_FASTDDS_VERSION} EXACT REQUIRED CONFIG
        PATHS ${_wxz_fastrtps_hint_paths}
        NO_DEFAULT_PATH
    )
endif()

if(DEFINED fastrtps_DIR AND NOT "${fastrtps_DIR}" MATCHES "^${WXZ_FASTDDS_PREFIX}(/|$)")
    message(FATAL_ERROR "fastrtps_DIR must be under WXZ_FASTDDS_PREFIX. fastrtps_DIR=${fastrtps_DIR} WXZ_FASTDDS_PREFIX=${WXZ_FASTDDS_PREFIX}")
endif()

if(NOT DEFINED FASTDDS_LIBRARIES OR "${FASTDDS_LIBRARIES}" STREQUAL "")
    set(FASTDDS_LIBRARIES "")
    if(TARGET fastrtps)
        list(APPEND FASTDDS_LIBRARIES fastrtps)
    elseif(TARGET FastRTPS::fastrtps)
        list(APPEND FASTDDS_LIBRARIES FastRTPS::fastrtps)
    else()
        message(FATAL_ERROR "fastrtps imported target not found")
    endif()
endif()

message(STATUS "FastDDS required and found; linking: ${FASTDDS_LIBRARIES}")

# Allow MotionCore sources to see FastDDS includes in standalone mode.
if(DEFINED fastrtps_INCLUDE_DIRS)
    include_directories(${fastrtps_INCLUDE_DIRS})
endif()
if(DEFINED fastcdr_INCLUDE_DIRS)
    include_directories(${fastcdr_INCLUDE_DIRS})
endif()

## MotionCore library target definition (merged from src/CMakeLists.txt)

set(WXZ_SOURCES
    src/logger.cpp
    src/observability.cpp
    src/clock.cpp
    src/time_sync.cpp
    src/metrics_prometheus.cpp
    src/metrics_http_server.cpp
    src/config.cpp
    src/param_server_public.cpp
    src/param_server_distributed.cpp
    src/fault_recovery_executor.cpp
    src/config_fetcher.cpp
    src/param_server.cpp
    src/param_store.cpp
    src/event_queue.cpp
    src/event_dispatcher.cpp
    src/thread_pool.cpp
    src/wxz_worker_group.cpp
    src/discovery.cpp
    src/inproc_channel.cpp
    src/shm_channel.cpp
    src/fastdds_channel.cpp
    src/byte_buffer_pool.cpp
    src/fastdds_participant_factory.cpp
    src/dds_security_precheck.cpp
    src/channel_registry.cpp
    src/channel_factory.cpp
    src/rpc_service.cpp
    src/rpc_client.cpp
    src/dto_core.cpp
    src/event_dto_utils.cpp
    src/event_dto_cdr.cpp
    src/heartbeat_dto_cdr.cpp
    src/event_dto_sample.cpp
    src/image2d_dto.cpp
    src/simple_pose_dto.cpp
    src/pose3d_dto.cpp
)

add_library(MotionCore SHARED ${WXZ_SOURCES})
set_target_properties(MotionCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Legacy communicator implementation is intentionally isolated from the MotionCore shared library.
# It is built as an internal-only compat target and only linked by regression tests / platform bridges.
add_library(MotionCore_legacy_communication STATIC src/communication.cpp)
set_target_properties(MotionCore_legacy_communication PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    EXCLUDE_FROM_ALL ON
)

target_include_directories(MotionCore_legacy_communication PUBLIC
    $<BUILD_INTERFACE:${WXZ_MOTIONCORE_DIR}/include>
)

target_include_directories(MotionCore_legacy_communication PRIVATE
    ${WXZ_MOTIONCORE_DIR}/src/internal/include
)

if(WXZ_ENFORCE_LEGACY_COMMUNICATION_INTERNAL_ONLY)
    target_compile_definitions(MotionCore_legacy_communication PRIVATE WXZ_LEGACY_COMMUNICATION_ALLOWED=1)
endif()

# Legacy communicator is a thin adapter over MotionCore (FastddsChannel, participant factory, etc.).
target_link_libraries(MotionCore_legacy_communication PUBLIC MotionCore)

target_include_directories(MotionCore PUBLIC
    $<BUILD_INTERFACE:${WXZ_MOTIONCORE_DIR}/include>
    $<INSTALL_INTERFACE:${WXZ_INSTALL_INCLUDEDIR}>
)

# Internal headers are not part of the public API and are not installed.
target_include_directories(MotionCore PRIVATE
    ${WXZ_MOTIONCORE_DIR}/src/internal/include
)

# Ensure the fastcdr headers used at compile time match the fastcdr library
# selected by CMake (avoid picking up an ABI-incompatible /usr/local include
# during compilation).
set(_wxz_fastcdr_include_to_add "")
if(DEFINED fastcdr_INCLUDE_DIR)
    # Unfortunately GCC searches /usr/local/include before /usr/include by default.
    # If an incompatible fastcdr is installed under /usr/local, compilation can pick
    # the wrong headers and later fail to link or crash at runtime.
    #
    # Adding -I/usr/include is unreliable because compilers may treat builtin include
    # dirs specially. Instead, create a non-builtin include root (under the build dir)
    # that points at the selected fastcdr headers, then add it to the include path.
    if(fastcdr_INCLUDE_DIR STREQUAL "/usr/include" AND EXISTS "/usr/include/fastcdr")
        set(_wxz_fastcdr_include_root "${CMAKE_BINARY_DIR}/_wxz_fastcdr_include")
        file(MAKE_DIRECTORY "${_wxz_fastcdr_include_root}")
        if(NOT EXISTS "${_wxz_fastcdr_include_root}/fastcdr")
            file(CREATE_LINK "/usr/include/fastcdr" "${_wxz_fastcdr_include_root}/fastcdr" SYMBOLIC)
        endif()
        set(_wxz_fastcdr_include_to_add "${_wxz_fastcdr_include_root}")
    else()
        set(_wxz_fastcdr_include_to_add "${fastcdr_INCLUDE_DIR}")
    endif()
endif()

if(NOT "${_wxz_fastcdr_include_to_add}" STREQUAL "")
    target_include_directories(MotionCore PRIVATE "${_wxz_fastcdr_include_to_add}")
    target_include_directories(MotionCore_legacy_communication PRIVATE "${_wxz_fastcdr_include_to_add}")
endif()

set(WXZ_PUBLIC_LINK_LIBS
    ${FASTDDS_LIBRARIES}
    nlohmann_json::nlohmann_json
)

set(WXZ_PRIVATE_LINK_LIBS
    ${OMPL_LIBRARIES}
    ${FCL_LIBRARIES}
    ${YAMLCPP_LIB}
    fastcdr
    Threads::Threads
)

find_package(CURL REQUIRED)

if(CURL_FOUND)
    list(APPEND WXZ_PRIVATE_LINK_LIBS CURL::libcurl)
endif()

# MotionCore is a shared library; downstream targets should only link dependencies
# that are required by public headers.
target_link_libraries(MotionCore
    PUBLIC  ${WXZ_PUBLIC_LINK_LIBS}
    PRIVATE ${WXZ_PRIVATE_LINK_LIBS}
)

# Install shared core library and export targets for downstream projects
install(TARGETS MotionCore
    EXPORT MotionCoreTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime-core
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime-core
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT runtime-core
)

# Downstream package:
#   find_package(MotionCore CONFIG REQUIRED)
#   target_link_libraries(app PRIVATE MotionCore::MotionCore)
install(EXPORT MotionCoreTargets
    FILE MotionCoreTargets.cmake
    NAMESPACE MotionCore::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MotionCore
    COMPONENT dev
)

# Also export a build-tree targets file so that using
#   -DCMAKE_PREFIX_PATH=<MotionCore/build>
# works even before/without installing to a separate prefix.
export(EXPORT MotionCoreTargets
    FILE "${CMAKE_BINARY_DIR}/cmake/MotionCoreTargets.cmake"
    NAMESPACE MotionCore::
)

if(WXZ_INSTALL_DEV)
    # Strict public header whitelist for downstream consumers.
    # Public headers are flattened under MotionCore/include/ (no MotionCore/ subdir).
    # Keep internal headers private: do not install include/internal/**.
    # Avoid installing dto/** and communication/** here (dto has its own install rule;
    # communication headers are internal-only).
    install(DIRECTORY ${WXZ_MOTIONCORE_DIR}/include/
        DESTINATION ${WXZ_INSTALL_INCLUDEDIR}
        COMPONENT dev
        FILES_MATCHING PATTERN "*.h"
        PATTERN "internal" EXCLUDE
        PATTERN "internal/*" EXCLUDE
        PATTERN "dto" EXCLUDE
        PATTERN "dto/*" EXCLUDE
        PATTERN "communication" EXCLUDE
        PATTERN "communication/*" EXCLUDE
    )
    install(DIRECTORY ${WXZ_MOTIONCORE_DIR}/include/dto/
        DESTINATION ${WXZ_INSTALL_INCLUDEDIR}/dto
        COMPONENT dev
        FILES_MATCHING PATTERN "*.h"
    )
endif()

install(DIRECTORY ${WXZ_MOTIONCORE_DIR}/config/
    DESTINATION ${WXZ_INSTALL_CONFIGDIR}
    COMPONENT runtime-core
    FILES_MATCHING PATTERN "*.yaml*"
)

install(DIRECTORY ${WXZ_MOTIONCORE_DIR}/resources/
    DESTINATION ${WXZ_INSTALL_RESOURCEDIR}
    COMPONENT runtime-core
)

if(WXZ_INSTALL_DOCS)
    install(DIRECTORY ${WXZ_MOTIONCORE_DIR}/docs/
        DESTINATION ${WXZ_INSTALL_DOCDIR}
        COMPONENT docs
        FILES_MATCHING PATTERN "*.md"
    )
endif()

configure_package_config_file(
    ${WXZ_MOTIONCORE_DIR}/cmake/MotionCoreConfig.cmake.in
    ${CMAKE_BINARY_DIR}/cmake/MotionCoreConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MotionCore
)

# Generate a convenience installer script in the build directory.
configure_file(
    ${WXZ_MOTIONCORE_DIR}/cmake/install.sh.in
    ${CMAKE_BINARY_DIR}/install.sh
    @ONLY
)

if(UNIX)
    execute_process(COMMAND chmod +x "${CMAKE_BINARY_DIR}/install.sh")
endif()

if(WXZ_INSTALL_DEV)
    install(FILES ${CMAKE_BINARY_DIR}/cmake/MotionCoreConfig.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MotionCore
        COMPONENT dev
    )
endif()
