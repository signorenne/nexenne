cmake_minimum_required(VERSION 3.23)

project(nexenne
    VERSION      0.1.0
    DESCRIPTION  "Nexenne: modular C++23 libraries"
    HOMEPAGE_URL "https://github.com/signorenne/nexenne"
    LANGUAGES    CXX
)

set(CMAKE_CXX_STANDARD          23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(NEXENNE_BUILD_TESTS        "Build nexenne unit tests"          ${PROJECT_IS_TOP_LEVEL})
option(NEXENNE_BUILD_EXAMPLES     "Build nexenne example programs"    ${PROJECT_IS_TOP_LEVEL})
option(NEXENNE_BUILD_BENCHMARKS   "Build nexenne benchmark suites"    ${PROJECT_IS_TOP_LEVEL})
option(NEXENNE_BUILD_DOCS         "Generate Doxygen documentation"    OFF)
option(NEXENNE_INSTALL            "Generate nexenne install rules"    ${PROJECT_IS_TOP_LEVEL})
option(NEXENNE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(nexenne_warnings)
include(nexenne_module)

if(NEXENNE_BUILD_TESTS)
    include(nexenne_testing)
    # Always present so --target nexenne_tests works even before any module
    # adds a suite; modules attach their run targets to it.
    add_custom_target(nexenne_tests COMMENT "[nexenne] running all doctest suites")
endif()

# Aggregate docs target; per-module targets attach to it via the docs helper.
if(NEXENNE_BUILD_DOCS)
    include(nexenne_doxygen)
    add_custom_target(nexenne_docs COMMENT "[nexenne] building all module docs")
endif()

# Add each module under modules/ in dependency order. New module directories
# are picked up automatically on the next configure.
nexenne_discover_modules("${CMAKE_CURRENT_SOURCE_DIR}/modules" _ordered_modules)
foreach(_module IN LISTS _ordered_modules)
    string(TOUPPER "${_module}" _upper)
    option(NEXENNE_MODULE_${_upper} "Build the nexenne::${_module} module" ON)
    if(NEXENNE_MODULE_${_upper})
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/modules/${_module}")
    endif()
endforeach()

if(NEXENNE_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()
if(NEXENNE_BUILD_BENCHMARKS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/CMakeLists.txt")
    add_subdirectory(benchmarks)
endif()

# nexenne::all links every enabled module. Only defined once a module exists.
get_property(_enabled_modules GLOBAL PROPERTY NEXENNE_MODULES)
if(_enabled_modules)
    add_library(nexenne_all INTERFACE)
    add_library(nexenne::all ALIAS nexenne_all)
    set_target_properties(nexenne_all PROPERTIES EXPORT_NAME all)
    foreach(_module IN LISTS _enabled_modules)
        target_link_libraries(nexenne_all INTERFACE nexenne::${_module})
    endforeach()
    target_sources(nexenne_all INTERFACE
        FILE_SET nexenne_umbrella_headers TYPE HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/include/nexenne/nexenne.hpp
    )
endif()

if(NEXENNE_INSTALL AND _enabled_modules)
    install(TARGETS nexenne_all
        EXPORT  nexenne_targets
        FILE_SET nexenne_umbrella_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(EXPORT nexenne_targets
        FILE      nexenne-targets.cmake
        NAMESPACE nexenne::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nexenne
    )
    set(_nexenne_components "${_enabled_modules}")
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/templates/nexenne-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/nexenne-config.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nexenne
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/nexenne-config-version.cmake"
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/nexenne-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/nexenne-config-version.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nexenne
    )
endif()

message(STATUS "[nexenne] v${PROJECT_VERSION} | modules: ${_enabled_modules}")
message(STATUS "[nexenne] tests=${NEXENNE_BUILD_TESTS} examples=${NEXENNE_BUILD_EXAMPLES} "
               "benchmarks=${NEXENNE_BUILD_BENCHMARKS} docs=${NEXENNE_BUILD_DOCS} "
               "install=${NEXENNE_INSTALL} werror=${NEXENNE_WARNINGS_AS_ERRORS}")
