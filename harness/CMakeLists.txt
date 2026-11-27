cmake_minimum_required(VERSION 3.20)
project(windows_dump_lab LANGUAGES CXX)

# 30 fault scenarios used to exercise the windows-dump-analysis skill.
# Build artifacts go to <repo>/output/bin so the debugger always finds the
# matching CrashHarness.pdb next to CrashHarness.exe.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# RelWithDebInfo normally adds /O2 /Ob1 which fights with the /Od /Ob0 below
# (cl warns D9025 and the last flag wins); strip them so the intent is explicit.
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/Zi /Ob0 /Od /DNDEBUG")
endif()

# Q_OBJECT classes live in .cpp files, so AUTOMOC must generate and include them.
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ---------------------------------------------------------------- Qt discovery
find_package(Qt6 COMPONENTS Core Gui Widgets QUIET)
if(Qt6_FOUND)
    set(LAB_QT_VERSION 6)
    set(LAB_QT_TARGETS Qt6::Core Qt6::Gui Qt6::Widgets)
else()
    find_package(Qt5 COMPONENTS Core Gui Widgets REQUIRED)
    set(LAB_QT_VERSION 5)
    set(LAB_QT_TARGETS Qt5::Core Qt5::Gui Qt5::Widgets)
endif()
message(STATUS "windows-dump-lab: using Qt${LAB_QT_VERSION} (${Qt${LAB_QT_VERSION}_DIR})")

# ---------------------------------------------------------------- target
add_executable(CrashHarness
    main.cpp
    core/DumpHelper.cpp
    cases/01_NullPtrCases.cpp
    cases/02_UafAndMemoryCases.cpp
    cases/03_QtFrameworkCases.cpp
    cases/04_ConcurrencyCases.cpp
    cases/05_ResourceLeakCases.cpp
)

target_include_directories(CrashHarness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(CrashHarness PRIVATE ${LAB_QT_TARGETS} dbghelp)

# Symbols without optimisation: the lab exists to produce walkable stacks, so
# inlining, frame reuse and tail calls are all switched off on purpose.
target_compile_options(CrashHarness PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/Od /Ob0 /Zi /GS /Gy- /utf-8 /W3 /wd4996 /EHsc /MD>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O0 -g>
)
target_link_options(CrashHarness PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/DEBUG /INCREMENTAL:NO /OPT:NOREF /OPT:NOICF>
)

# output/bin, not a build subdirectory: dump paths are derived from the exe.
set(LAB_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../output")
set_target_properties(CrashHarness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${LAB_OUTPUT_DIR}/bin"
    PDB_OUTPUT_DIRECTORY "${LAB_OUTPUT_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${LAB_OUTPUT_DIR}/bin"
    PDB_OUTPUT_DIRECTORY_DEBUG "${LAB_OUTPUT_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${LAB_OUTPUT_DIR}/bin"
    PDB_OUTPUT_DIRECTORY_RELEASE "${LAB_OUTPUT_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${LAB_OUTPUT_DIR}/bin"
    PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${LAB_OUTPUT_DIR}/bin"
)

# ---------------------------------------------------------------- Qt deployment
# Locate windeployqt next to qmake and copy platforms/qwindows.dll and friends so
# QApplication can start from output/bin without a Qt installation on PATH.
if(LAB_QT_VERSION EQUAL 6)
    get_target_property(_lab_qmake Qt6::qmake IMPORTED_LOCATION)
else()
    get_target_property(_lab_qmake Qt5::qmake IMPORTED_LOCATION)
endif()
get_filename_component(_lab_qt_bin "${_lab_qmake}" DIRECTORY)
set(LAB_WINDEPLOYQT "${_lab_qt_bin}/windeployqt.exe")

if(EXISTS "${LAB_WINDEPLOYQT}")
    add_custom_command(TARGET CrashHarness POST_BUILD
        COMMAND "${LAB_WINDEPLOYQT}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --no-compiler-runtime
                "$<TARGET_FILE:CrashHarness>"
        COMMENT "windeployqt: deploying Qt runtime into output/bin"
    )
else()
    message(WARNING "windeployqt.exe not found next to qmake (${LAB_WINDEPLOYQT}); "
                    "QApplication may fail to start. Add Qt's bin directory or pass -DCMAKE_PREFIX_PATH.")
endif()

# dumps directory is created by DumpHelper at runtime; create it here as well so
# the very first run never races on directory creation.
add_custom_command(TARGET CrashHarness POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LAB_OUTPUT_DIR}/dumps"
)
