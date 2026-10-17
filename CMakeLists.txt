cmake_minimum_required(VERSION 3.16)
project(notepadSharp VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Static CRT. Windows 7 still needs the Universal C Runtime update (KB2999226).
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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
        platform/win32/notepadsharp.rc)
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
    target_link_libraries(notepadSharp PRIVATE scintilla lexilla comctl32 comdlg32 shell32)
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
    target_link_libraries(notepadSharp PRIVATE scintilla lexilla "-framework Cocoa")
else()
    find_package(PkgConfig REQUIRED)
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
    target_link_libraries(notepadSharp PRIVATE scintilla lexilla PkgConfig::GTK3)
endif()

target_include_directories(notepadSharp PRIVATE src ${SCINTILLA_DIR}/include)
