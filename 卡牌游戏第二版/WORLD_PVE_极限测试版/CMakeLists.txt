cmake_minimum_required(VERSION 3.16)

project(WorldPveExtreme LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    set(QT_PACKAGE Qt6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(QT_PACKAGE Qt5)
endif()

add_executable(WorldPveExtreme
    main.cpp
    GameTypes.h
    MainWindow.h
    MainWindow.cpp
    MapManager.h
    MapManager.cpp
    StoryManager.h
    StoryManager.cpp
)

target_link_libraries(WorldPveExtreme PRIVATE ${QT_PACKAGE}::Widgets)

add_custom_command(TARGET WorldPveExtreme POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/data"
        "$<TARGET_FILE_DIR:WorldPveExtreme>/data"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:WorldPveExtreme>/assets"
)
