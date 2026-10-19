cmake_minimum_required(VERSION 3.16)
project(notepadSharp VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Static CRT. Windows 7 still needs the Universal C Runtime update (KB2999226).
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Vendored Boost.Regex 1.90.0, header-only standalone; no runtime DLL or download.
add_library(boost_regex INTERFACE)
target_include_directories(boost_regex SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/third_party/boost_regex/include)
target_compile_definitions(boost_regex INTERFACE BOOST_REGEX_STANDALONE)

include(cmake/Icu.cmake)

set(SCINTILLA_DIR ${CMAKE_SOURCE_DIR}/third_party/scintilla)
set(LEXILLA_DIR ${CMAKE_SOURCE_DIR}/third_party/lexilla)

set(SCINTILLA_CORE
    AutoComplete.cxx CallTip.cxx CaseConvert.cxx CaseFolder.cxx CellBuffer.cxx
    ChangeHistory.cxx CharacterCategoryMap.cxx CharacterType.cxx CharClassify.cxx
    ContractionState.cxx DBCS.cxx Decoration.cxx Document.cxx EditModel.cxx Editor.cxx
    EditView.cxx Geometry.cxx Indicator.cxx KeyMap.cxx LineMarker.cxx MarginView.cxx
    PerLine.cxx PositionCache.cxx RESearch.cxx RunStyles.cxx ScintillaBase.cxx
    Selection.cxx Style.cxx UndoHistory.cxx UniConversion.cxx UniqueString.cxx
    ViewStyle.cxx XPM.cxx)
list(TRANSFORM SCINTILLA_CORE PREPEND ${SCINTILLA_DIR}/src/)

file(GLOB LEXILLA_LEXERS CONFIGURE_DEPENDS ${LEXILLA_DIR}/lexers/Lex*.cxx)
file(GLOB LEXILLA_LEXLIB CONFIGURE_DEPENDS ${LEXILLA_DIR}/lexlib/*.cxx)

add_library(lexilla STATIC
    ${LEXILLA_DIR}/src/Lexilla.cxx
    ${LEXILLA_LEXERS}
    ${LEXILLA_LEXLIB})
target_include_directories(lexilla PUBLIC
    ${LEXILLA_DIR}/include
    ${LEXILLA_DIR}/lexlib
    ${SCINTILLA_DIR}/include)
target_compile_definitions(lexilla PUBLIC LEXILLA_NO_EXPORT)
if(MSVC)
    target_compile_options(lexilla PRIVATE /utf-8 /W3 /EHsc /D_CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(lexilla PRIVATE -Wall -Wno-unused-parameter)
endif()

if(WIN32)
    add_library(scintilla STATIC
        ${SCINTILLA_CORE}
        ${SCINTILLA_DIR}/win32/HanjaDic.cxx
        ${SCINTILLA_DIR}/win32/PlatWin.cxx
        ${SCINTILLA_DIR}/win32/ListBox.cxx
        ${SCINTILLA_DIR}/win32/SurfaceGDI.cxx
        ${SCINTILLA_DIR}/win32/SurfaceD2D.cxx
        ${SCINTILLA_DIR}/win32/ScintillaWin.cxx)
    target_include_directories(scintilla PUBLIC ${SCINTILLA_DIR}/include ${SCINTILLA_DIR}/src)
    target_compile_definitions(scintilla PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(scintilla PRIVATE /utf-8 /W3 /EHsc)
    else()
        target_compile_definitions(scintilla PRIVATE _WIN32_WINNT=0x0601)
        target_compile_options(scintilla PRIVATE -Wall -Wno-unused-parameter)
    endif()
    target_link_libraries(scintilla PUBLIC user32 gdi32 imm32 ole32 oleaut32 advapi32 d2d1 dwrite)

    add_executable(notepadSharp WIN32
        src/App.cpp
        platform/win32/WinMain.cpp
        platform/win32/ToolbarIcons.cpp
        platform/win32/SearchDialog.cpp
        platform/win32/notepadsharp.rc)
    set_property(SOURCE platform/win32/notepadsharp.rc APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/platform/win32/app.manifest")
    target_compile_definitions(notepadSharp PRIVATE
        _WIN32_WINNT=0x0601
        WINVER=0x0601
        UNICODE
        _UNICODE)
    if(MSVC)
        target_compile_options(notepadSharp PRIVATE /utf-8 /W4 /EHsc)
        target_link_options(notepadSharp PRIVATE "/SUBSYSTEM:WINDOWS,6.01")
    else()
        target_compile_options(notepadSharp PRIVATE -Wall -Wextra)
        # Keep libstdc++ inside the exe. A libstdc++-6.dll earlier on PATH
        # lacks this compiler's std::filesystem symbols and the process fails to start.
        target_link_options(notepadSharp PRIVATE
            -municode
            -static
            -static-libgcc
            -static-libstdc++
            -Wl,--major-subsystem-version,6,--minor-subsystem-version,1)
    endif()
    if(MINGW)
        # Some WinLibs GCC releases unconditionally inject a second manifest.
        # Keep our PerMonitorV2 manifest as the executable's only manifest.
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpspecs OUTPUT_VARIABLE gcc_specs)
        string(REGEX MATCH "\\*endfile:[\r\n]+([^\r\n]+)" endfile_spec "${gcc_specs}")
        if(endfile_spec MATCHES "default-manifest")
            string(REPLACE "%{!shared:%:if-exists(default-manifest.o%s)}" "" endfile_spec "${endfile_spec}")
            file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/app-manifest.specs" "${endfile_spec}\n")
            target_link_options(notepadSharp PRIVATE "-specs=${CMAKE_CURRENT_BINARY_DIR}/app-manifest.specs")
        endif()
    endif()
    target_link_libraries(notepadSharp PRIVATE scintilla lexilla comctl32 comdlg32 shell32 gdiplus)
elseif(APPLE)
    enable_language(OBJCXX)
    file(GLOB SCINTILLA_COCOA_MM CONFIGURE_DEPENDS ${SCINTILLA_DIR}/cocoa/*.mm)
    add_library(scintilla STATIC ${SCINTILLA_CORE} ${SCINTILLA_COCOA_MM})
    set_source_files_properties(${SCINTILLA_COCOA_MM} PROPERTIES LANGUAGE OBJCXX COMPILE_FLAGS -fobjc-arc)
    target_include_directories(scintilla PUBLIC
        ${SCINTILLA_DIR}/include
        ${SCINTILLA_DIR}/src
        ${SCINTILLA_DIR}/cocoa)
    target_compile_options(scintilla PRIVATE -Wall -Wno-unused-parameter -fobjc-arc)
    target_link_libraries(scintilla PUBLIC "-framework Cocoa" "-framework QuartzCore")

    add_executable(notepadSharp
        src/App.cpp
        platform/cocoa/Main.mm)
    set_source_files_properties(platform/cocoa/Main.mm PROPERTIES LANGUAGE OBJCXX COMPILE_FLAGS -fobjc-arc)
    target_compile_options(notepadSharp PRIVATE -Wall -Wextra -fobjc-arc)
    target_include_directories(notepadSharp PRIVATE ${SCINTILLA_DIR}/include ${SCINTILLA_DIR}/cocoa)
    target_link_libraries(notepadSharp PRIVATE scintilla lexilla "-framework Cocoa" "-framework CoreText")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
    pkg_check_modules(GMODULE REQUIRED IMPORTED_TARGET gmodule-no-export-2.0)
    add_library(scintilla STATIC
        ${SCINTILLA_CORE}
        ${SCINTILLA_DIR}/gtk/PlatGTK.cxx
        ${SCINTILLA_DIR}/gtk/ScintillaGTK.cxx
        ${SCINTILLA_DIR}/gtk/ScintillaGTKAccessible.cxx
        ${SCINTILLA_DIR}/gtk/scintilla-marshal.c)
    target_include_directories(scintilla PUBLIC
        ${SCINTILLA_DIR}/include
        ${SCINTILLA_DIR}/src
        ${SCINTILLA_DIR}/gtk)
    target_compile_definitions(scintilla PUBLIC GTK)
    target_compile_options(scintilla PRIVATE -Wall -Wno-unused-parameter)
    target_link_libraries(scintilla PUBLIC PkgConfig::GTK3 PkgConfig::GMODULE)

    add_executable(notepadSharp
        src/App.cpp
        platform/gtk/Main.cpp)
    target_compile_options(notepadSharp PRIVATE -Wall -Wextra)
    target_include_directories(notepadSharp PRIVATE ${SCINTILLA_DIR}/include)
    target_link_libraries(notepadSharp PRIVATE scintilla lexilla PkgConfig::GTK3 PkgConfig::FONTCONFIG)
endif()

target_link_libraries(notepadSharp PRIVATE boost_regex icu_regex)
if(WIN32)
    target_sources(notepadSharp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/icu-data.rc")
endif()
target_sources(notepadSharp PRIVATE src/TextEncoding.cpp src/Session.cpp src/EditorExtras.cpp src/CodeTools.cpp src/FolderSearch.cpp src/ReferenceFeatures.cpp src/FileTracking.cpp src/SearchOptions.cpp src/UnicodeRegex.cpp src/IcuRegex.cpp src/Search.cpp)
target_include_directories(notepadSharp PRIVATE src ${SCINTILLA_DIR}/include)

# Offline font distribution: embedded on Windows, beside the executable elsewhere.
add_custom_command(TARGET notepadSharp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/fonts" "$<TARGET_FILE_DIR:notepadSharp>/fonts")
add_custom_command(TARGET notepadSharp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:notepadSharp>/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/boost_regex/LICENSE_1_0.txt"
        "$<TARGET_FILE_DIR:notepadSharp>/licenses/Boost-LICENSE_1_0.txt")
add_custom_command(TARGET notepadSharp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/icu/LICENSE"
        "$<TARGET_FILE_DIR:notepadSharp>/licenses/ICU-LICENSE.txt")
if(NOT WIN32)
    add_custom_command(TARGET notepadSharp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:notepadSharp>/unicode"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/icu/icudt78l.dat"
            "$<TARGET_FILE_DIR:notepadSharp>/unicode/icudt78l.dat")
endif()
include(CTest)
if(BUILD_TESTING AND WIN32)
    add_executable(editor_tests tests/EditorTests.cpp src/App.cpp src/TextEncoding.cpp src/Session.cpp src/EditorExtras.cpp src/CodeTools.cpp src/FolderSearch.cpp src/ReferenceFeatures.cpp src/FileTracking.cpp src/SearchOptions.cpp src/UnicodeRegex.cpp src/IcuRegex.cpp src/Search.cpp)
    target_include_directories(editor_tests PRIVATE src ${SCINTILLA_DIR}/include)
    target_link_libraries(editor_tests PRIVATE scintilla lexilla boost_regex icu_regex)
    target_sources(editor_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/icu-data.rc")
    target_compile_definitions(editor_tests PRIVATE UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(editor_tests PRIVATE /utf-8)
    else()
        target_link_options(editor_tests PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME editor_behavior COMMAND editor_tests)
endif()
