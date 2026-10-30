include(FetchContent)

function(nexus_prepare_avif_tool)
    if(NOT WIN32)
        message(FATAL_ERROR "Apollo's bundled AVIF encoder is currently provided for Windows builds only.")
    endif()

    # Use libavif's official statically linked Windows tools instead of building
    # libaom locally. This keeps Apollo self-contained without requiring Perl,
    # NASM, YASM, or another system-installed AV1 toolchain.
    FetchContent_Declare(
        nexus_avif_tools
        URL https://github.com/AOMediaCodec/libavif/releases/download/v1.4.1/windows-artifacts.zip
        URL_HASH SHA256=54d665ba6ca8f4ba98fa3dc2c761ebe78c32fadf93d3bf92d5880a6622271656
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(nexus_avif_tools)

    file(GLOB_RECURSE _avifenc_candidates
        LIST_DIRECTORIES FALSE
        "${nexus_avif_tools_SOURCE_DIR}/*avifenc.exe"
    )
    list(LENGTH _avifenc_candidates _avifenc_count)
    if(_avifenc_count EQUAL 0)
        message(FATAL_ERROR "Apollo could not locate avifenc.exe in the official libavif Windows artifact.")
    endif()

    list(GET _avifenc_candidates 0 _avifenc_executable)
    set(NEXUS_AVIFENC_EXECUTABLE "${_avifenc_executable}" PARENT_SCOPE)
endfunction()

function(nexus_bundle_avif_tool target_name)
    if(NOT NEXUS_AVIFENC_EXECUTABLE OR NOT EXISTS "${NEXUS_AVIFENC_EXECUTABLE}")
        message(FATAL_ERROR "nexus_prepare_avif_tool() must be called before nexus_bundle_avif_tool().")
    endif()

    add_custom_command(
        TARGET ${target_name}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NEXUS_AVIFENC_EXECUTABLE}"
                "$<TARGET_FILE_DIR:${target_name}>/avifenc.exe"
        VERBATIM
    )
endfunction()
