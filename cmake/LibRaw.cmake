include(FetchContent)

function(nexus_add_libraw)
    if(TARGET libraw::libraw_r)
        return()
    endif()

    # LibRaw's main repository intentionally does not ship the CMake project used
    # by package managers. Fetch the official source plus the companion CMake
    # project, pinned to known revisions, and combine them in the build tree.
    FetchContent_Declare(
        nexus_libraw_source
        GIT_REPOSITORY https://github.com/LibRaw/LibRaw.git
        GIT_TAG 0.22.2
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        SOURCE_SUBDIR __nexus_no_cmake_project
    )

    # This is the same LibRaw-cmake revision currently used by vcpkg's LibRaw port.
    FetchContent_Declare(
        nexus_libraw_cmake
        GIT_REPOSITORY https://github.com/LibRaw/LibRaw-cmake.git
        GIT_TAG eb98e4325aef2ce85d2eb031c2ff18640ca616d3
        GIT_PROGRESS TRUE
        SOURCE_SUBDIR __nexus_no_cmake_project
    )

    FetchContent_MakeAvailable(nexus_libraw_source nexus_libraw_cmake)

    file(COPY "${nexus_libraw_cmake_SOURCE_DIR}/CMakeLists.txt"
         DESTINATION "${nexus_libraw_source_SOURCE_DIR}")
    file(COPY "${nexus_libraw_cmake_SOURCE_DIR}/cmake"
         DESTINATION "${nexus_libraw_source_SOURCE_DIR}")

    # Nexus currently needs Sony ARW decoding only. Keep LibRaw static and avoid
    # optional codecs/dependencies that are unrelated to ARW import.
    set(BUILD_SHARED_LIBS OFF)
    set(ENABLE_OPENMP OFF)
    set(ENABLE_LCMS OFF)
    set(ENABLE_JASPER OFF)
    set(ENABLE_EXAMPLES OFF)
    set(ENABLE_RAWSPEED OFF)
    set(ENABLE_DCRAW_DEBUG OFF)
    set(ENABLE_X3FTOOLS OFF)
    set(ENABLE_6BY9RPI OFF)
    set(LIBRAW_UNINSTALL_TARGET OFF)
    set(LIBRAW_INSTALL OFF)

    add_subdirectory(
        "${nexus_libraw_source_SOURCE_DIR}"
        "${nexus_libraw_source_BINARY_DIR}"
        EXCLUDE_FROM_ALL
    )
endfunction()
