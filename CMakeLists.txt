cmake_minimum_required(VERSION 3.24)

project(Nexus VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
find_package(libraw CONFIG REQUIRED)
qt_standard_project_setup()

qt_add_executable(Nexus WIN32
    src/main.cpp
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/imaging/ArwDecoder.cpp
    src/imaging/ArwDecoder.h
    src/imaging/ImageLoader.cpp
    src/imaging/ImageLoader.h
    src/ui/canvas/PhotoCanvas.cpp
    src/ui/canvas/PhotoCanvas.h
)

target_include_directories(Nexus PRIVATE src)
target_link_libraries(Nexus PRIVATE
    Qt6::Widgets
    libraw::raw_r
)

set_target_properties(Nexus PROPERTIES
    WIN32_EXECUTABLE TRUE
)
