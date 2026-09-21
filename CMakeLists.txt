cmake_minimum_required(VERSION 3.21)
project(tiltback VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(tiltback
    src/main.cpp
)

# pmOS 26.06 is Qt 6.11; Alpine 3.22's Qt is 6.8. Shipping 6.8 qmlcache
# fails at runtime (V4 data version mismatch). Interpret QML instead.
set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
qt_add_qml_module(tiltback
    URI TiltBack
    VERSION 1.0
    NO_CACHEGEN
    QML_FILES
        qml/Main.qml
)

set_target_properties(tiltback PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_link_libraries(tiltback PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
)

install(TARGETS tiltback RUNTIME DESTINATION bin)
install(FILES data/org.tiltback.TiltBack.desktop
        DESTINATION share/applications)
