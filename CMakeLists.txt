cmake_minimum_required(VERSION 3.7)
project(ViolaT)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/)
set(CMAKE_C_STANDARD 11)
SET(CMAKE_C_FLAGS_DISTRIBUTION "-Ofast")

find_package (Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Gstreamer
find_package(GStreamer REQUIRED)
include_directories(${GSTREAMER_INCLUDE_DIRS})

# GObject
find_package( GObject REQUIRED )
include_directories(${GOBJECT_INCLUDE_DIR})

set(SOURCE_FILES src/violat.c)
add_executable(violat ${SOURCE_FILES})
target_link_libraries(violat ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(violat ${GSTREAMER_LIBRARIES})
target_link_libraries(violat ${GOBJECT_LIBRARIES})
