cmake_minimum_required(VERSION 3.16)
project(optoflash_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Try Qt6 first, then Qt5
find_package(Qt6 COMPONENTS Widgets SerialPort QUIET)
if (Qt6_FOUND)
    set(QT_VERSION 6)
    set(QT_LIBS Qt6::Widgets Qt6::SerialPort)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets SerialPort)
    set(QT_VERSION 5)
    set(QT_LIBS Qt5::Widgets Qt5::SerialPort)
endif()

add_executable(optoflash_gui
    main.cpp
)

target_link_libraries(optoflash_gui PRIVATE ${QT_LIBS})

target_compile_definitions(optoflash_gui PRIVATE QT_VERSION_MAJOR=${QT_VERSION})
