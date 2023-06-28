set(BABYLON_NATIVE_BUILD_APPS OFF)
add_subdirectory(BabylonNative)

set(BUILD_TESTING OFF)
set(BUILD_TOOLS OFF)
set(BUILD_XAUDIO_WIN8 OFF)
set(DIRECTX_ARCH ${CMAKE_VS_PLATFORM_NAME})
add_subdirectory(DirectXTK)
set_property(TARGET DirectXTK PROPERTY FOLDER Dependencies)
