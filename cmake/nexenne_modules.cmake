include_guard(GLOBAL)

# Module discovery, dependency reading, and standalone bootstrap.
#
# A module declares its inter-module dependencies in one place only: a
# modules/<name>/module.deps file, one dependency module name per line
# (blank lines and # comments ignored). Everything that needs the dependency
# list reads that file: the root build for ordering, nexenne_add_module for
# linking, and nexenne_standalone_bootstrap for single-module checkouts.

# Read a module's dependency names into out_var (empty if no module.deps).
function(nexenne_read_module_deps module_dir out_var)
    set(_deps "")
    if(EXISTS "${module_dir}/module.deps")
        file(STRINGS "${module_dir}/module.deps" _lines)
        foreach(_line IN LISTS _lines)
            string(STRIP "${_line}" _line)
            if(_line AND NOT _line MATCHES "^#")
                list(APPEND _deps "${_line}")
            endif()
        endforeach()
    endif()
    set(${out_var} "${_deps}" PARENT_SCOPE)
endfunction()

# Discover every module under modules_dir (a directory with a CMakeLists.txt)
# and return them in dependency order, deps before dependents. Fails on a
# dependency cycle. Dependencies that are not themselves present as modules
# are ignored for ordering and left to link-time resolution.
function(nexenne_discover_modules modules_dir out_var)
    set(_modules "")
    if(IS_DIRECTORY "${modules_dir}")
        file(GLOB _candidates RELATIVE "${modules_dir}" "${modules_dir}/*")
        list(SORT _candidates)
        foreach(_candidate IN LISTS _candidates)
            if(IS_DIRECTORY "${modules_dir}/${_candidate}"
               AND EXISTS "${modules_dir}/${_candidate}/CMakeLists.txt")
                list(APPEND _modules "${_candidate}")
            endif()
        endforeach()
    endif()

    set(_ordered "")
    set(_remaining "${_modules}")
    while(_remaining)
        set(_progress FALSE)
        foreach(_module IN LISTS _remaining)
            nexenne_read_module_deps("${modules_dir}/${_module}" _module_deps)
            set(_ready TRUE)
            foreach(_dep IN LISTS _module_deps)
                if(_dep IN_LIST _modules AND NOT _dep IN_LIST _ordered)
                    set(_ready FALSE)
                    break()
                endif()
            endforeach()
            if(_ready)
                list(APPEND _ordered "${_module}")
                list(REMOVE_ITEM _remaining "${_module}")
                set(_progress TRUE)
            endif()
        endforeach()
        if(NOT _progress)
            message(FATAL_ERROR
                "[nexenne] dependency cycle or missing module among: ${_remaining}")
        endif()
    endwhile()

    set(${out_var} "${_ordered}" PARENT_SCOPE)
endfunction()

# Pull a sibling module (and its transitive deps) into a standalone build,
# deps first, each built without its own tests. Falls back to find_package
# when the sibling source is not checked out next to this module.
function(_nexenne_bootstrap_dep modules_dir dep)
    if(TARGET nexenne::${dep})
        return()
    endif()
    set(_dep_dir "${modules_dir}/${dep}")
    if(NOT EXISTS "${_dep_dir}/CMakeLists.txt")
        find_package(nexenne-${dep} REQUIRED)
        return()
    endif()
    nexenne_read_module_deps("${_dep_dir}" _sub_deps)
    foreach(_sub IN LISTS _sub_deps)
        _nexenne_bootstrap_dep("${modules_dir}" "${_sub}")
    endforeach()
    set(NEXENNE_BUILD_TESTS OFF)
    add_subdirectory("${_dep_dir}" "${CMAKE_BINARY_DIR}/_deps/nexenne-${dep}")
endfunction()

# Set up the umbrella helpers and dependencies when a single module is the
# top-level build. Call this from a module's CMakeLists guarded by
# CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR. This is a macro so
# include(CTest) and enable_testing() run in the module's directory scope,
# which is what lets ctest discover the module's tests.
macro(nexenne_standalone_bootstrap)
    set(CMAKE_CXX_STANDARD          23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS        OFF)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    option(NEXENNE_BUILD_TESTS        "Build tests"              ON)
    option(NEXENNE_BUILD_DOCS         "Generate Doxygen docs"    OFF)
    option(NEXENNE_INSTALL            "Generate install rules"   ON)
    option(NEXENNE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
    include(nexenne_warnings)
    include(nexenne_module)

    if(NEXENNE_BUILD_TESTS)
        include(nexenne_testing)
    endif()
    if(NEXENNE_BUILD_DOCS AND NOT TARGET nexenne_docs)
        include(nexenne_doxygen)
        add_custom_target(nexenne_docs)
    endif()

    nexenne_read_module_deps("${CMAKE_CURRENT_SOURCE_DIR}" _nexenne_boot_deps)
    foreach(_nexenne_boot_dep IN LISTS _nexenne_boot_deps)
        _nexenne_bootstrap_dep("${CMAKE_CURRENT_SOURCE_DIR}/.." "${_nexenne_boot_dep}")
    endforeach()
endmacro()
