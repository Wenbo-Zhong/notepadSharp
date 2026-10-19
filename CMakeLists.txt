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

find_package(wxWidgets REQUIRED COMPONENTS core base stc aui)
include(${wxWidgets_USE_FILE})

if(WIN32)
    add_executable(notepadSharp WIN32 platform/wx/Main.cpp src/App.cpp)
    target_compile_definitions(notepadSharp PRIVATE _WIN32_WINNT=0x0601 WINVER=0x0601 UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(notepadSharp PRIVATE /utf-8 /W4 /EHsc)
        target_link_options(notepadSharp PRIVATE "/SUBSYSTEM:WINDOWS,6.01")
    else()
        target_compile_options(notepadSharp PRIVATE -Wall -Wextra)
        target_link_options(notepadSharp PRIVATE -static -static-libgcc -static-libstdc++
            -Wl,--major-subsystem-version,6,--minor-subsystem-version,1)
    endif()
    if(MINGW)
        # WinLibs GCC injects a default manifest; keep the application's DPI manifest alone.
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} -dumpspecs OUTPUT_VARIABLE gcc_specs)
        string(REGEX MATCH "\\*endfile:[\r\n]+([^\r\n]+)" endfile_spec "${gcc_specs}")
        if(endfile_spec MATCHES "default-manifest")
            string(REPLACE "%{!shared:%:if-exists(default-manifest.o%s)}" "" endfile_spec "${endfile_spec}")
            file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/app-manifest.specs" "${endfile_spec}\n")
            target_link_options(notepadSharp PRIVATE "-specs=${CMAKE_CURRENT_BINARY_DIR}/app-manifest.specs")
        endif()
    endif()
    target_sources(notepadSharp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/icu-data.rc")
    target_sources(notepadSharp PRIVATE platform/win32/notepadsharp.rc)
    set_property(SOURCE platform/win32/notepadsharp.rc APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/platform/win32/app.manifest")
    target_link_libraries(notepadSharp PRIVATE comctl32 comdlg32 shell32 gdiplus)
elseif(APPLE)
    add_executable(notepadSharp MACOSX_BUNDLE platform/wx/Main.cpp src/App.cpp)
else()
    add_executable(notepadSharp platform/wx/Main.cpp src/App.cpp)
endif()
target_link_libraries(notepadSharp PRIVATE ${wxWidgets_LIBRARIES} lexilla)
target_include_directories(notepadSharp PRIVATE ${wxWidgets_INCLUDE_DIRS} ${SCINTILLA_DIR}/include ${LEXILLA_DIR}/include)
if(MSVC)
    target_compile_options(notepadSharp PRIVATE /utf-8 /W4 /EHsc)
else()
    target_compile_options(notepadSharp PRIVATE -Wall -Wextra)
endif()

target_link_libraries(notepadSharp PRIVATE boost_regex icu_regex)
target_sources(notepadSharp PRIVATE src/TextEncoding.cpp src/Session.cpp src/EditorExtras.cpp src/CodeTools.cpp src/FolderSearch.cpp src/ReferenceFeatures.cpp src/FileTracking.cpp src/SearchOptions.cpp src/UnicodeRegex.cpp src/IcuRegex.cpp src/Search.cpp)
target_include_directories(notepadSharp PRIVATE src ${SCINTILLA_DIR}/include)

# Offline font distribution: embedded on Windows, beside the executable elsewhere.
if(APPLE)
    set(NOTEPADSHARP_RESOURCE_DIR "$<TARGET_BUNDLE_CONTENT_DIR:notepadSharp>/Resources")
else()
    set(NOTEPADSHARP_RESOURCE_DIR "$<TARGET_FILE_DIR:notepadSharp>")
endif()
add_custom_command(TARGET notepadSharp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/fonts" "${NOTEPADSHARP_RESOURCE_DIR}/fonts")
add_custom_command(TARGET notepadSharp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NOTEPADSHARP_RESOURCE_DIR}/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/boost_regex/LICENSE_1_0.txt"
        "${NOTEPADSHARP_RESOURCE_DIR}/licenses/Boost-LICENSE_1_0.txt")
add_custom_command(TARGET notepadSharp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/icu/LICENSE"
        "${NOTEPADSHARP_RESOURCE_DIR}/licenses/ICU-LICENSE.txt")
if(NOT WIN32)
    add_custom_command(TARGET notepadSharp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${NOTEPADSHARP_RESOURCE_DIR}/unicode"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/icu/icudt78l.dat"
            "${NOTEPADSHARP_RESOURCE_DIR}/unicode/icudt78l.dat")
endif()
include(CTest)
