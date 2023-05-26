set(SCRIPTS
    "node_modules/babylonjs/babylon.max.js"
    "node_modules/babylonjs-loaders/babylonjs.loaders.js"
    "Scripts/index.js")

set(SOURCES
    "Win32/RenderDoc.h"
    "Win32/RenderDoc.cpp"
    "Win32/App.cpp")

add_executable(ConsoleApp ${SCRIPTS} ${SOURCES})

target_compile_definitions(ConsoleApp PRIVATE UNICODE)
target_compile_definitions(ConsoleApp PRIVATE _UNICODE)

target_compile_options(ConsoleApp PRIVATE /await)

target_link_to_dependencies(ConsoleApp
    PRIVATE AppRuntime
    PRIVATE Console
    PRIVATE DirectXTK
    PRIVATE ExternalTexture
    PRIVATE NativeEngine
    PRIVATE ScriptLoader
    PRIVATE Window
    PRIVATE XMLHttpRequest)

foreach(SCRIPT ${SCRIPTS})
    get_filename_component(SCRIPT_NAME "${SCRIPT}" NAME)
    add_custom_command(
        OUTPUT "${CMAKE_CFG_INTDIR}/Scripts/${SCRIPT_NAME}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/${SCRIPT}" "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/Scripts/${SCRIPT_NAME}"
        COMMENT "Copying ${SCRIPT_NAME}"
        MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/${SCRIPT}")
endforeach()

set_property(TARGET ConsoleApp PROPERTY FOLDER Apps)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SCRIPTS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ConsoleApp)
