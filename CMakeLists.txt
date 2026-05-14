cmake_minimum_required(VERSION 3.20)
project(gpu-screen-recorder-qt VERSION 5.7.9 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets DBus)

add_executable(gpu-screen-recorder-qt
    src/main.cpp
    src/global_shortcuts.cpp
)

target_include_directories(gpu-screen-recorder-qt PRIVATE src)
target_link_libraries(gpu-screen-recorder-qt PRIVATE
    Qt6::Widgets
    Qt6::DBus
)

target_compile_definitions(gpu-screen-recorder-qt PRIVATE
    GSR_VERSION="${PROJECT_VERSION}"
)

if(CMAKE_INSTALL_PREFIX)
    target_compile_definitions(gpu-screen-recorder-qt PRIVATE
        GSR_ICONS_PATH="${CMAKE_INSTALL_PREFIX}/share/icons"
    )
endif()

install(TARGETS gpu-screen-recorder-qt RUNTIME DESTINATION bin)
install(FILES com.dec05eba.gpu_screen_recorder.desktop
    DESTINATION share/applications
)
install(DIRECTORY icons/hicolor DESTINATION share/icons)
