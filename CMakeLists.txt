cmake_minimum_required(VERSION 3.18)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(BabylonNativeExamples)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(Dependencies)
add_subdirectory(Apps)
