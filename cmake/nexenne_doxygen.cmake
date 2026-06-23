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
# installed system-wide, we download the official prebuilt binary (no source
# build, so no flex/bison/git toolchain headache). Opt out with
# -DNEXENNE_FETCH_DOXYGEN_IF_MISSING=OFF. Docs are off by default anyway
# (NEXENNE_BUILD_DOCS), so the core library + test build never needs Doxygen.
#
option(NEXENNE_FETCH_DOXYGEN_IF_MISSING
    "Download a prebuilt Doxygen if not installed" ON)
set(NEXENNE_DOXYGEN_VERSION "1.12.0"
    CACHE STRING "Doxygen version to fetch when missing")
set(NEXENNE_DOXYGEN_SHA256
    "3c42c3f3fb206732b503862d9c9c11978920a8214f223a3950bbf2520be5f647"
    CACHE STRING "SHA256 of the Doxygen ${NEXENNE_DOXYGEN_VERSION} Linux x86_64 binary tarball")

find_package(Doxygen QUIET)

if(NOT DOXYGEN_FOUND AND NEXENNE_FETCH_DOXYGEN_IF_MISSING)
    # The prebuilt binary is published for Linux x86_64 only. On other hosts a
    # system Doxygen (package manager) is used; if absent, docs targets are
    # skipped with a clear message below rather than failing the build.
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux"
       AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        message(STATUS "[nexenne] Doxygen not found, fetching prebuilt ${NEXENNE_DOXYGEN_VERSION}")
        string(REPLACE "." "_" _doxy_tag "Release_${NEXENNE_DOXYGEN_VERSION}")
        include(FetchContent)
        FetchContent_Declare(
            nexenne_doxygen_bin
            URL "https://github.com/doxygen/doxygen/releases/download/${_doxy_tag}/doxygen-${NEXENNE_DOXYGEN_VERSION}.linux.bin.tar.gz"
            URL_HASH "SHA256=${NEXENNE_DOXYGEN_SHA256}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(nexenne_doxygen_bin)
        FetchContent_GetProperties(nexenne_doxygen_bin SOURCE_DIR _doxy_dir)
        if(EXISTS "${_doxy_dir}/bin/doxygen")
            set(DOXYGEN_EXECUTABLE "${_doxy_dir}/bin/doxygen")
            set(DOXYGEN_FOUND TRUE)
        endif()
    else()
        message(STATUS
            "[nexenne] Doxygen not found; the prebuilt fetch is Linux x86_64 only. "
            "Install Doxygen via your package manager to build docs.")
    endif()
endif()

#
# Graphviz dot drives the dependency and inheritance graphs. It is optional: if
# dot is not installed, HAVE_DOT is set NO so Doxygen skips those graphs and the
# build still succeeds (no Graphviz dependency to clone-and-build docs). The
# Doxyfile templates read these via @ONLY configure_file.
#
find_program(NEXENNE_DOT_EXECUTABLE NAMES dot)
if(NEXENNE_DOT_EXECUTABLE)
    set(NEXENNE_DOXYGEN_HAVE_DOT "YES")
    get_filename_component(NEXENNE_DOXYGEN_DOT_PATH "${NEXENNE_DOT_EXECUTABLE}" DIRECTORY)
else()
    set(NEXENNE_DOXYGEN_HAVE_DOT "NO")
    set(NEXENNE_DOXYGEN_DOT_PATH "")
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
