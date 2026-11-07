cmake_minimum_required(VERSION 3.14)
project(miniAdmin3)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# The source files contain Hungarian accented letters (UTF-8).
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(miniadmin_core STATIC
    src/game.cpp
    src/net.cpp
    src/status.cpp
    src/commands.cpp
    src/config.cpp
    src/colors.cpp
    src/rconparse.cpp
    src/monitor.cpp
    src/hostinfo.cpp
    src/alias.cpp
    src/settings.cpp
    src/guiddb.cpp
    src/bandb.cpp
    src/gametypes.cpp
    src/cvardesc.cpp
    src/lang.cpp
    src/master.cpp
    src/rights.cpp
    src/shortcuts.cpp)
target_include_directories(miniadmin_core PUBLIC include)
if(WIN32)
    target_link_libraries(miniadmin_core PUBLIC ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(miniadmin_core PUBLIC Threads::Threads)
endif()

# ---- Developer programs: the tests (test_*.exe) and the command line helper (miniadmin.exe).
# They are only needed in Debug: make_release.cmd builds only the program (--target miniadmin_gui); in the Visual Studio
# solution the Release (MinSizeRel, RelWithDebInfo) configuration skips them; with a single-configuration generator
# (e.g. MinGW) they are not part of a Release build.
# Running the tests: ctest -C Debug (this is what make_debug.cmd does).
function(miniadmin_dev_target t)
    if(CMAKE_CONFIGURATION_TYPES)
        set_target_properties(${t} PROPERTIES
            EXCLUDE_FROM_DEFAULT_BUILD_RELEASE TRUE
            EXCLUDE_FROM_DEFAULT_BUILD_MINSIZEREL TRUE
            EXCLUDE_FROM_DEFAULT_BUILD_RELWITHDEBINFO TRUE)
    elseif(CMAKE_BUILD_TYPE MATCHES "^(Release|MinSizeRel|RelWithDebInfo)$")
        set_target_properties(${t} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
endfunction()

# Command line helper for the core library (status / rcon / say / kick / map / list / geo).
add_executable(miniadmin src/main.cpp)
target_link_libraries(miniadmin PRIVATE miniadmin_core)
miniadmin_dev_target(miniadmin)

enable_testing()
function(miniadmin_test name)
    add_executable(test_${name} tests/test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE miniadmin_core)
    add_test(NAME ${name} COMMAND test_${name})
    miniadmin_dev_target(test_${name})
endfunction()

miniadmin_test(core)
miniadmin_test(gui_logic)
miniadmin_test(settings)
miniadmin_test(guiddb)
miniadmin_test(bandb)
miniadmin_test(endpoint)
miniadmin_test(gametypes)
miniadmin_test(cvardesc)
miniadmin_test(master)
miniadmin_test(rights)
miniadmin_test(shortcuts)
# Checks the language files: every key the UI uses is present in all 6 lang\*.lng files.
miniadmin_test(lang)
target_compile_definitions(test_lang PRIVATE MINIADMIN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Graphical user interface (native Win32): miniAdmin3.exe, like the original program. MSVC or MinGW-w64 (posix thread model).
if(WIN32)
    enable_language(RC)
    add_executable(miniadmin_gui WIN32 src/gui_main.cpp src/ftpclient.cpp res/miniadmin.rc)
    set_target_properties(miniadmin_gui PROPERTIES OUTPUT_NAME miniAdmin3)
    target_include_directories(miniadmin_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/res)
    target_link_libraries(miniadmin_gui PRIVATE miniadmin_core comctl32 comdlg32 gdiplus wininet)
    if(MSVC)
        # the .rc contains the manifest, the linker must not generate another one
        target_link_options(miniadmin_gui PRIVATE /MANIFEST:NO)
    endif()
    # the language files next to the exe (lang\*.lng)
    add_custom_command(TARGET miniadmin_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/lang $<TARGET_FILE_DIR:miniadmin_gui>/lang)
endif()
