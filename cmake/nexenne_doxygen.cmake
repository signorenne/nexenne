include_guard(GLOBAL)

#
# Discovers Doxygen once and exposes a helper:
#
#   nexenne_add_module_docs(<name>
#     [DOXYFILE_IN <path>]   default: ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
#   )
#
# Each call defines a target named nexenne_<name>_docs that runs Doxygen
# against the module's configured Doxyfile. The aggregate target
# `nexenne_docs` (created by the root build) gathers them all so
# `cmake --build build/dev --target nexenne_docs` builds every module's docs.
#

#
# Default ON so a fresh clone is fully self-contained: if Doxygen is not
# installed system-wide, we build it from source. Users on slow machines
# can opt out with -DNEXENNE_FETCH_DOXYGEN_IF_MISSING=OFF.
#
option(NEXENNE_FETCH_DOXYGEN_IF_MISSING
    "Fetch and build Doxygen from source if not installed" ON)
set(NEXENNE_DOXYGEN_GIT_TAG "Release_1_12_0"
    CACHE STRING "Doxygen release tag to fetch if NEXENNE_FETCH_DOXYGEN_IF_MISSING")

find_package(Doxygen QUIET)

if(NOT DOXYGEN_FOUND AND NEXENNE_FETCH_DOXYGEN_IF_MISSING)
    message(STATUS "[nexenne] Doxygen not found, fetching ${NEXENNE_DOXYGEN_GIT_TAG}")
    include(FetchContent)
    FetchContent_Declare(
        nexenne_doxygen_src
        GIT_REPOSITORY https://github.com/doxygen/doxygen.git
        GIT_TAG        ${NEXENNE_DOXYGEN_GIT_TAG}
    )
    FetchContent_MakeAvailable(nexenne_doxygen_src)

    if(TARGET doxygen)
        set(DOXYGEN_EXECUTABLE "$<TARGET_FILE:doxygen>")
        set(DOXYGEN_FOUND TRUE)
    endif()
endif()


function(nexenne_add_module_docs name)
    set(_options  "")
    set(_oneval   DOXYFILE_IN)
    set(_multival "")
    cmake_parse_arguments(NDOC "${_options}" "${_oneval}" "${_multival}" ${ARGN})

    if(NOT NEXENNE_BUILD_DOCS)
        return()
    endif()

    if(NOT DOXYGEN_FOUND)
        message(WARNING
            "[nexenne::${name}] Doxygen is not available, doc target will not be created. "
            "Install doxygen, or pass -DNEXENNE_FETCH_DOXYGEN_IF_MISSING=ON.")
        return()
    endif()

    if(NOT NDOC_DOXYFILE_IN)
        set(NDOC_DOXYFILE_IN "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in")
    endif()
    if(NOT EXISTS "${NDOC_DOXYFILE_IN}")
        set(_shared_doxyfile_in
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../template/module/doc/Doxyfile.in")
        if(EXISTS "${_shared_doxyfile_in}")
            set(NDOC_DOXYFILE_IN "${_shared_doxyfile_in}")
        endif()
    endif()
    if(NOT EXISTS "${NDOC_DOXYFILE_IN}")
        message(FATAL_ERROR
            "nexenne_add_module_docs(${name}): Doxyfile template not found at ${NDOC_DOXYFILE_IN}")
    endif()

    set(_doxy_out "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")
    set(NEXENNE_MODULE_NAME "${name}")
    configure_file("${NDOC_DOXYFILE_IN}" "${_doxy_out}" @ONLY)

    set(_target "nexenne_${name}_docs")
    add_custom_target(${_target}
        COMMAND ${DOXYGEN_EXECUTABLE} "${_doxy_out}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "[nexenne::${name}] generating API docs with Doxygen"
        VERBATIM
    )
    if(TARGET doxygen)
        add_dependencies(${_target} doxygen)
    endif()

    if(TARGET nexenne_docs)
        add_dependencies(nexenne_docs ${_target})
    endif()

    message(STATUS "[nexenne] docs target: ${_target}")
endfunction()
