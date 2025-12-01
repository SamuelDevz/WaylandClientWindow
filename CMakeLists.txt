cmake_minimum_required(VERSION 3.27)
project(WaylandClientWindow)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(protocols STATIC xdg-shell-client-protocol.c xdg-decoration-unstable-v1.c)
target_link_libraries(protocols PUBLIC wayland-client wayland-cursor)

add_executable(WaylandClientWindow main.cpp)
target_link_libraries(WaylandClientWindow PUBLIC protocols)