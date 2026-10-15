# Ninja parses /showIncludes to track direct and transitive header dependencies.
# Probe the prefix in the compiler's console encoding, including installations
# that only have Chinese resources and cannot switch to English with VSLANG.
include_guard()
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    set(snipaste_dependency_probe_dir "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/snipaste-dependency-probe")
    file(MAKE_DIRECTORY "${snipaste_dependency_probe_dir}")
    file(WRITE "${snipaste_dependency_probe_dir}/snipaste-dependency-probe.h" "#pragma once\n")
    file(WRITE "${snipaste_dependency_probe_dir}/probe.cpp" "#include \"snipaste-dependency-probe.h\"\n")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "VSLANG=1033"
            "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /EP
            "${snipaste_dependency_probe_dir}/probe.cpp"
        OUTPUT_VARIABLE snipaste_dependency_probe_output
        ERROR_VARIABLE snipaste_dependency_probe_error
        RESULT_VARIABLE snipaste_dependency_probe_result
        ENCODING AUTO
    )
    string(REGEX MATCH "(^|[\r\n])([^\r\n]*:)[ \t]+[^\r\n]*snipaste-dependency-probe[.]h"
        snipaste_dependency_probe_match "${snipaste_dependency_probe_output}${snipaste_dependency_probe_error}")
    if(NOT snipaste_dependency_probe_result EQUAL 0 OR NOT snipaste_dependency_probe_match)
        message(FATAL_ERROR "Could not detect the MSVC /showIncludes prefix for Ninja dependency tracking.")
    endif()
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}")
    # A compiler launcher also covers builds invoked directly through CMake or
    # Ninja, without requiring the caller to configure its environment. Preserve
    # any existing launcher (for example sccache) inside the environment wrapper.
    list(PREPEND CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_COMMAND}" -E env "VSLANG=1033")
endif()
