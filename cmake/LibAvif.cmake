include(FetchContent)

function(nexus_add_libavif)
    if(TARGET avif)
        return()
    endif()

    # libavif does not enable an AV1 codec by default. Build a pinned static
    # libavif + libaom encoder so AVIF export works without a system codec.
    set(BUILD_SHARED_LIBS OFF)
    set(AVIF_CODEC_AOM LOCAL CACHE STRING "" FORCE)
    set(AVIF_CODEC_AOM_DECODE OFF CACHE BOOL "" FORCE)
    set(AVIF_CODEC_AOM_ENCODE ON CACHE BOOL "" FORCE)
    set(AVIF_LIBYUV OFF CACHE STRING "" FORCE)
    set(AVIF_LIBSHARPYUV OFF CACHE STRING "" FORCE)
    set(AVIF_JPEG OFF CACHE STRING "" FORCE)
    set(AVIF_ZLIBPNG OFF CACHE STRING "" FORCE)
    set(AVIF_BUILD_APPS OFF CACHE BOOL "" FORCE)
    set(AVIF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(AVIF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(AVIF_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        nexus_libavif
        GIT_REPOSITORY https://github.com/AOMediaCodec/libavif.git
        GIT_TAG v1.4.1
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )

    FetchContent_MakeAvailable(nexus_libavif)
endfunction()
