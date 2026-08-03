include_guard(GLOBAL)

# Distribution snapshot version. This is the version of the umbrella nexenne
# package and records one coherent state of every module below.
set(NEXENNE_VERSION "0.1.0")

set(NEXENNE_KNOWN_MODULES
    algorithm
    benchmark
    chrono
    container
    ecs
    filter
    geometry
    logging
    math
    random
    serialization
    signal
    utility
    gpio
)

# Independent module package/API versions. The umbrella package installs a
# manifest of this state; per-module package version files use these values.
set(NEXENNE_MODULE_ALGORITHM_VERSION     "0.1.0")
set(NEXENNE_MODULE_BENCHMARK_VERSION     "0.1.0")
set(NEXENNE_MODULE_CHRONO_VERSION        "0.1.0")
set(NEXENNE_MODULE_CONTAINER_VERSION     "0.1.0")
set(NEXENNE_MODULE_ECS_VERSION           "0.1.0")
set(NEXENNE_MODULE_FILTER_VERSION        "0.1.0")
set(NEXENNE_MODULE_GEOMETRY_VERSION      "0.1.0")
set(NEXENNE_MODULE_LOGGING_VERSION       "0.1.0")
set(NEXENNE_MODULE_MATH_VERSION          "0.1.0")
set(NEXENNE_MODULE_RANDOM_VERSION        "0.1.0")
set(NEXENNE_MODULE_SERIALIZATION_VERSION "0.1.0")
set(NEXENNE_MODULE_SIGNAL_VERSION        "0.1.0")
set(NEXENNE_MODULE_UTILITY_VERSION       "0.1.0")

set(NEXENNE_MODULE_GPIO_VERSION "0.1.0")

function(nexenne_validate_stable_semver variable_name)
    if(NOT DEFINED ${variable_name})
        message(FATAL_ERROR "${variable_name} is not defined")
    endif()
    set(_version "${${variable_name}}")
    if(NOT _version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR "${variable_name} must be a stable SemVer core: X.Y.Z")
    endif()
endfunction()

function(nexenne_get_module_version name out_var)
    string(TOUPPER "${name}" _upper)
    string(REPLACE "-" "_" _upper "${_upper}")
    set(_version_var "NEXENNE_MODULE_${_upper}_VERSION")
    if(NOT DEFINED ${_version_var})
        message(FATAL_ERROR
            "No version is defined for nexenne::${name}. "
            "Add ${_version_var} to cmake/nexenne_version.cmake.")
    endif()
    set(${out_var} "${${_version_var}}" PARENT_SCOPE)
endfunction()

nexenne_validate_stable_semver(NEXENNE_VERSION)
foreach(_module IN LISTS NEXENNE_KNOWN_MODULES)
    string(TOUPPER "${_module}" _upper)
    string(REPLACE "-" "_" _upper "${_upper}")
    nexenne_validate_stable_semver("NEXENNE_MODULE_${_upper}_VERSION")
endforeach()
