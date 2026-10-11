cmake_minimum_required(VERSION 3.24)

project(Nexus VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

include(cmake/LibRaw.cmake)
nexus_add_libraw()

qt_add_executable(Nexus WIN32
    src/main.cpp
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/imaging/ArwDecoder.cpp
    src/imaging/ArwDecoder.h
    src/imaging/Histogram.cpp
    src/imaging/Histogram.h
    src/imaging/ImageLoader.cpp
    src/imaging/ImageLoader.h
    src/imaging/PhotoMetadata.h
    src/imaging/TemperatureRenderService.cpp
    src/imaging/TemperatureRenderService.h
    src/imaging/WhiteBalancePickerService.cpp
    src/imaging/WhiteBalancePickerService.h
    src/ui/canvas/PhotoCanvas.cpp
    src/ui/canvas/PhotoCanvas.h
    src/ui/filmstrip/FilmstripWidget.cpp
    src/ui/filmstrip/FilmstripWidget.h
    src/ui/histogram/HistogramWidget.cpp
    src/ui/histogram/HistogramWidget.h
    src/ui/leftpanel/NavigatorPanel.cpp
    src/ui/leftpanel/NavigatorPanel.h
    src/ui/rightpanel/BasicPanel.cpp
    src/ui/rightpanel/BasicPanel.h
    src/ui/rightpanel/PhotoMetadataWidget.cpp
    src/ui/rightpanel/PhotoMetadataWidget.h
    src/ui/rightpanel/WhiteBalancePickerController.cpp
    src/ui/rightpanel/WhiteBalancePickerController.h
)

target_include_directories(Nexus PRIVATE src)
target_link_libraries(Nexus PRIVATE
    Qt6::Widgets
    libraw::libraw_r
)

set_target_properties(Nexus PROPERTIES
    WIN32_EXECUTABLE TRUE
)
