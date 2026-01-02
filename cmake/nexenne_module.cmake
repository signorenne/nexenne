include_guard(GLOBAL)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)
include(nexenne_modules)

# nexenne_add_module(<name>
#   [KIND <INTERFACE|STATIC|SHARED|OBJECT>]   default INTERFACE (header-only)
#   [VERSION <ver>]                           default PROJECT_VERSION
#   [HEADERS <file>...]                       public headers
#   [SOURCES <file>...]                       required unless KIND is INTERFACE
#   [LINK_PUBLIC  <lib>...]                   external libs, transitively exposed
#   [LINK_PRIVATE <lib>...]                   external libs, impl-only (not INTERFACE)
# )
#
# Declares the library nexenne::<name>. Inter-module dependencies come from
# modules/<name>/module.deps, not from an argument here. KIND SHARED also
# generates include/nexenne/<name>/<name>_export.hpp with NEXENNE_<NAME>_EXPORT.
function(nexenne_add_module name)
    cmake_parse_arguments(NMOD "" "VERSION;KIND" "HEADERS;SOURCES;LINK_PUBLIC;LINK_PRIVATE" ${ARGN})

    if(NOT NMOD_KIND)
        set(NMOD_KIND "INTERFACE")
    endif()
    set(_valid_kinds INTERFACE STATIC SHARED OBJECT)
    if(NOT NMOD_KIND IN_LIST _valid_kinds)
        message(FATAL_ERROR "nexenne_add_module(${name}): KIND must be one of ${_valid_kinds}")
    endif()
    if(NMOD_KIND STREQUAL "INTERFACE" AND NMOD_SOURCES)
        message(FATAL_ERROR "nexenne_add_module(${name}): INTERFACE modules cannot have SOURCES")
    endif()
    if(NOT NMOD_KIND STREQUAL "INTERFACE" AND NOT NMOD_SOURCES)
        message(FATAL_ERROR "nexenne_add_module(${name}): KIND ${NMOD_KIND} requires SOURCES")
    endif()
    if(NMOD_KIND STREQUAL "INTERFACE" AND NMOD_LINK_PRIVATE)
        message(FATAL_ERROR "nexenne_add_module(${name}): LINK_PRIVATE needs a compiled KIND")
    endif()

    if(NOT NMOD_VERSION)
        if(PROJECT_VERSION)
            set(NMOD_VERSION "${PROJECT_VERSION}")
        else()
            set(NMOD_VERSION "0.0.0")
        endif()
    endif()

    nexenne_read_module_deps("${CMAKE_CURRENT_SOURCE_DIR}" _deps)

    set(_target  "nexenne_${name}")
    set(_fileset "nexenne_${name}_headers")

    if(NMOD_KIND STREQUAL "INTERFACE")
        set(_pub INTERFACE)
        add_library(${_target} INTERFACE)
    else()
        set(_pub PUBLIC)
        add_library(${_target} ${NMOD_KIND} ${NMOD_SOURCES})
        set_target_properties(${_target} PROPERTIES
            CXX_VISIBILITY_PRESET     hidden
            VISIBILITY_INLINES_HIDDEN ON
            POSITION_INDEPENDENT_CODE ON
            VERSION                   ${NMOD_VERSION}
            SOVERSION                 ${PROJECT_VERSION_MAJOR}
        )
    endif()

    add_library(nexenne::${name} ALIAS ${_target})
    set_target_properties(${_target} PROPERTIES EXPORT_NAME ${name})
    target_compile_features(${_target} ${_pub} cxx_std_23)

    if(NMOD_HEADERS)
        target_sources(${_target} ${_pub}
            FILE_SET ${_fileset} TYPE HEADERS
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
            FILES ${NMOD_HEADERS}
        )
    else()
        target_include_directories(${_target} ${_pub}
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        )
    endif()

    foreach(_dep IN LISTS _deps)
        if(NOT TARGET nexenne::${_dep})
            message(FATAL_ERROR
                "nexenne::${name} depends on nexenne::${_dep}, which is not defined yet. "
                "Check modules/${name}/module.deps and the build order.")
        endif()
        target_link_libraries(${_target} ${_pub} nexenne::${_dep})
    endforeach()
    foreach(_lib IN LISTS NMOD_LINK_PUBLIC)
        target_link_libraries(${_target} ${_pub} ${_lib})
    endforeach()
    foreach(_lib IN LISTS NMOD_LINK_PRIVATE)
        target_link_libraries(${_target} PRIVATE ${_lib})
    endforeach()

    if(NMOD_KIND STREQUAL "SHARED")
        string(TOUPPER "${name}" _upper)
        set(_export_header
            "${CMAKE_CURRENT_BINARY_DIR}/generated/nexenne/${name}/${name}_export.hpp")
        generate_export_header(${_target}
            BASE_NAME nexenne_${name}
            EXPORT_FILE_NAME "${_export_header}"
            EXPORT_MACRO_NAME "NEXENNE_${_upper}_EXPORT"
        )
        target_sources(${_target} PUBLIC
            FILE_SET ${_fileset} TYPE HEADERS
            BASE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/generated"
            FILES "${_export_header}"
        )
    endif()

    set_property(GLOBAL APPEND PROPERTY NEXENNE_MODULES ${name})

    if(NEXENNE_INSTALL)
        nexenne_install_module(${name} "${_fileset}" "${NMOD_VERSION}" "${_deps}" "${NMOD_HEADERS}")
    endif()

    message(STATUS "[nexenne] module: ${name} (${NMOD_KIND}; deps: ${_deps})")
endfunction()

# Install rules and package config for one module. Split out so the body of
# nexenne_add_module reads as one screen.
function(nexenne_install_module name fileset version deps headers)
    set(_target "nexenne_${name}")
    set(_export "nexenne_${name}_targets")

    if(headers)
        install(TARGETS ${_target}
            EXPORT   ${_export}
            FILE_SET ${fileset} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    else()
        install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
        install(TARGETS ${_target} EXPORT ${_export}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    install(EXPORT ${_export}
        FILE      nexenne-${name}-targets.cmake
        NAMESPACE nexenne::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nexenne-${name}
    )

    set(_module_name "${name}")
    set(_module_deps "")
    foreach(_dep IN LISTS deps)
        string(APPEND _module_deps "find_dependency(nexenne-${_dep} REQUIRED)\n")
    endforeach()
    configure_package_config_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/templates/module-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/nexenne-${name}-config.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nexenne-${name}
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/nexenne-${name}-config-version.cmake"
        VERSION ${version} COMPATIBILITY SameMajorVersion
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/nexenne-${name}-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/nexenne-${name}-config-version.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nexenne-${name}
    )
endfunction()

# nexenne_add_module_tests(<name> SOURCES <file>...)
# Builds nexenne_<name>_tests, a doctest binary linked against the module.
# Building the nexenne_tests target runs every module's binary directly; a
# failing suite returns nonzero and fails the build. No-op when tests are off.
function(nexenne_add_module_tests name)
    cmake_parse_arguments(NMT "" "" "SOURCES" ${ARGN})

    if(NOT NEXENNE_BUILD_TESTS)
        return()
    endif()
    if(NOT TARGET doctest::doctest_with_main)
        include(nexenne_testing)
    endif()

    # doctest's TEST_CASE expands __COUNTER__, which recent clang flags under
    # -Wpedantic as a C2y extension at the test's use site (not in doctest.h,
    # so a SYSTEM include cannot mask it). Silence just that one flag, after
    # nexenne::warnings so last-flag-wins keeps -Werror on our own code.
    if(NOT TARGET nexenne_doctest_quirks)
        add_library(nexenne_doctest_quirks INTERFACE)
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag(-Wno-c2y-extensions NEXENNE_HAVE_WNO_C2Y_EXTENSIONS)
        if(NEXENNE_HAVE_WNO_C2Y_EXTENSIONS)
            target_compile_options(nexenne_doctest_quirks INTERFACE -Wno-c2y-extensions)
        endif()
        if(TARGET nexenne::warnings)
            target_link_libraries(nexenne_doctest_quirks INTERFACE nexenne::warnings)
        endif()
    endif()

    set(_exe "nexenne_${name}_tests")
    add_executable(${_exe} ${NMT_SOURCES})
    target_link_libraries(${_exe} PRIVATE
        nexenne::${name} doctest::doctest_with_main nexenne::warnings nexenne_doctest_quirks)

    # Run the doctest binary; nexenne_tests aggregates every module's suite.
    add_custom_target(run_${_exe}
        COMMAND $<TARGET_FILE:${_exe}>
        DEPENDS ${_exe}
        COMMENT "[nexenne] doctest: ${name}"
        USES_TERMINAL VERBATIM
    )
    if(NOT TARGET nexenne_tests)
        add_custom_target(nexenne_tests COMMENT "[nexenne] running all doctest suites")
    endif()
    add_dependencies(nexenne_tests run_${_exe})
endfunction()

# nexenne_add_module_example(<name> TARGET <exe> SOURCES <file>...)
function(nexenne_add_module_example name)
    cmake_parse_arguments(NME "" "TARGET" "SOURCES" ${ARGN})

    if(NOT NEXENNE_BUILD_EXAMPLES)
        return()
    endif()
    if(NOT NME_TARGET)
        message(FATAL_ERROR "nexenne_add_module_example: TARGET is required")
    endif()

    add_executable(${NME_TARGET} ${NME_SOURCES})
    target_link_libraries(${NME_TARGET} PRIVATE nexenne::${name} nexenne::warnings)
endfunction()
