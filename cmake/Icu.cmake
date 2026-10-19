# ICU 78.1 / Unicode 17.0, static common + regular-expression components.
set(ICU_VENDOR "${CMAKE_SOURCE_DIR}/third_party/icu")
file(STRINGS "${ICU_VENDOR}/common/sources.txt" ICU_COMMON_SOURCES)
list(TRANSFORM ICU_COMMON_SOURCES PREPEND "${ICU_VENDOR}/common/")
add_library(icu_common STATIC ${ICU_COMMON_SOURCES} "${ICU_VENDOR}/stubdata.cpp")
add_library(icu_regex STATIC
    "${ICU_VENDOR}/i18n/regexcmp.cpp" "${ICU_VENDOR}/i18n/regeximp.cpp"
    "${ICU_VENDOR}/i18n/regexst.cpp" "${ICU_VENDOR}/i18n/regextxt.cpp"
    "${ICU_VENDOR}/i18n/rematch.cpp" "${ICU_VENDOR}/i18n/repattrn.cpp"
    "${ICU_VENDOR}/i18n/ucln_in.cpp" "${ICU_VENDOR}/i18n/uregex.cpp" "${ICU_VENDOR}/i18n/uregexc.cpp")
foreach(icu_target icu_common icu_regex)
    target_include_directories(${icu_target} SYSTEM PUBLIC "${ICU_VENDOR}/common" "${ICU_VENDOR}/i18n")
    target_compile_definitions(${icu_target} PUBLIC U_STATIC_IMPLEMENTATION
        UCONFIG_NO_COLLATION=1 UCONFIG_NO_FORMATTING=1 UCONFIG_NO_TRANSLITERATION=1
        U_ENABLE_DYLOAD=0)
    if(MSVC)
        target_compile_options(${icu_target} PRIVATE /utf-8 /EHsc)
    endif()
endforeach()
target_compile_definitions(icu_common PRIVATE U_COMMON_IMPLEMENTATION)
target_compile_definitions(icu_regex PRIVATE U_I18N_IMPLEMENTATION)
target_link_libraries(icu_regex PUBLIC icu_common)
if(WIN32)
    target_link_libraries(icu_common PUBLIC advapi32)
    configure_file("${CMAKE_SOURCE_DIR}/cmake/icu-data.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/icu-data.rc" @ONLY)
    set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/icu-data.rc" APPEND PROPERTY OBJECT_DEPENDS "${ICU_VENDOR}/icudt78l.dat")
else()
    find_package(Threads REQUIRED)
    target_link_libraries(icu_common PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()
