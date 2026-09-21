cmake_minimum_required(VERSION 3.21)
project(tiltback VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDRM REQUIRED libdrm)
pkg_check_modules(X11 x11 xrandr xi)

qt_standard_project_setup(REQUIRES 6.5)

set(tiltback_src
    src/main.cpp
    src/kwininput.h
    src/kwininput.cpp
    src/backend.h
    src/backend.cpp
    src/kwinbackend.h
    src/kwinbackend.cpp
    src/followengine.h
    src/followengine.cpp
)
if(X11_FOUND)
    list(APPEND tiltback_src src/x11backend.h src/x11backend.cpp)
endif()

qt_add_executable(tiltback ${tiltback_src})

# pmOS 26.06 is Qt 6.11; Alpine 3.22's Qt is 6.8. Shipping 6.8 qmlcache
# fails at runtime (V4 data version mismatch). Interpret QML instead.
set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
qt_add_qml_module(tiltback
    URI TiltBack
    VERSION 1.0
    NO_CACHEGEN
    QML_FILES
        qml/Main.qml
    SOURCES
        src/clinicmodel.h
        src/clinicmodel.cpp
)

set_target_properties(tiltback PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(tiltback PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LIBDRM_INCLUDE_DIRS}
)
target_link_libraries(tiltback PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::DBus
    ${LIBDRM_LIBRARIES}
)

if(X11_FOUND)
    target_compile_definitions(tiltback PRIVATE TILTBACK_X11)
    target_include_directories(tiltback PRIVATE ${X11_INCLUDE_DIRS})
    target_compile_options(tiltback PRIVATE ${X11_CFLAGS_OTHER})
    target_link_libraries(tiltback PRIVATE ${X11_LIBRARIES})
    message(STATUS "TiltBack: X11 RandR/XInput backend enabled")
else()
    message(STATUS "TiltBack: X11 libs not found — KWin-only build")
endif()

set_source_files_properties(
    data/icons/hicolor/512x512/apps/org.tiltback.TiltBack.png
    PROPERTIES QT_RESOURCE_ALIAS org.tiltback.TiltBack.png)
qt_add_resources(tiltback "app_icon"
    PREFIX "/icons"
    FILES data/icons/hicolor/512x512/apps/org.tiltback.TiltBack.png)

install(TARGETS tiltback RUNTIME DESTINATION bin)
install(FILES data/org.tiltback.TiltBack.desktop
        DESTINATION share/applications)
install(DIRECTORY data/icons/hicolor
        DESTINATION share/icons)
