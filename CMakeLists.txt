cmake_minimum_required(VERSION 3.20)

project(PeanutButterUltima LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_executable(PeanutButterUltima
  MACOSX_BUNDLE
  main_qt.cpp
  src/QtAppController.cpp
)
target_compile_definitions(
  PeanutButterUltima
  PRIVATE
  $<$<CONFIG:Debug>:PEANUT_BUTTER_ULTIMA_DEBUG_BUILD>
  $<$<CONFIG:Release>:PEANUT_BUTTER_ULTIMA_RELEASE_BUILD>
  $<$<CONFIG:RelWithDebInfo>:PEANUT_BUTTER_ULTIMA_RELEASE_BUILD>
  $<$<CONFIG:MinSizeRel>:PEANUT_BUTTER_ULTIMA_RELEASE_BUILD>
)

add_executable(PeanutButterUltimaShell
  main_shell.cpp
)

add_executable(PeanutButterUltimaGenerateExample
  main_generate_example.cpp
)

set(PEANUT_BUTTER_ULTIMA_CORE_SOURCES
  src/Encryption/Crypt.cpp
  src/IO/FileSystem.cpp
  src/IO/LocalFileSystem.cpp
  src/AppCore_Helpers.cpp
  src/AppCore_Fences.cpp
  src/AppCore_Pack.cpp
  src/AppCore_DecodeShared.cpp
  src/AppCore_Unpack.cpp
  src/AppCore_Recover.cpp
  src/AppCore_Validate.cpp
  src/AppCore.cpp
)

add_library(PeanutButterUltimaCore STATIC ${PEANUT_BUTTER_ULTIMA_CORE_SOURCES})
add_library(PeanutButterUltimaCoreTest STATIC ${PEANUT_BUTTER_ULTIMA_CORE_SOURCES})

target_include_directories(PeanutButterUltimaCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(PeanutButterUltimaCoreTest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(PeanutButterUltimaCoreTest PUBLIC PEANUT_BUTTER_ULTIMA_TEST_BUILD)

target_include_directories(PeanutButterUltima PRIVATE src)
target_link_libraries(PeanutButterUltima PRIVATE PeanutButterUltimaCore Qt${QT_VERSION_MAJOR}::Widgets)
target_include_directories(PeanutButterUltimaShell PRIVATE src)
target_link_libraries(PeanutButterUltimaShell PRIVATE PeanutButterUltimaCore)
target_include_directories(PeanutButterUltimaGenerateExample PRIVATE src)
target_link_libraries(PeanutButterUltimaGenerateExample PRIVATE PeanutButterUltimaCore)

add_executable(PeanutButterUltimaQuickSmoke
  quick_tests/QuickSmoke.cpp
)
target_include_directories(PeanutButterUltimaQuickSmoke PRIVATE src)
target_link_libraries(PeanutButterUltimaQuickSmoke PRIVATE PeanutButterUltimaCore)

set(PEANUT_BUTTER_ULTIMA_TEST_EXECUTOR_SOURCES
  tests/common/Test_Utils.cpp
  tests/common/Test_GeneratedFenceCaseData.cpp
  tests/common/Test_Wrappers.cpp
  tests/common/Test_Execute_Bundle.cpp
  tests/common/Test_Execute_Smoke.cpp
  tests/common/Test_Execute_FenceRoundTrip.cpp
  tests/common/Test_Execute_Unbundle.cpp
  tests/common/Test_Execute_Recover.cpp
  tests/mocks/MockHardDrive.cpp
  tests/mocks/MockFileSystem.cpp
)

add_library(PeanutButterUltimaTestExecutor STATIC ${PEANUT_BUTTER_ULTIMA_TEST_EXECUTOR_SOURCES})
target_include_directories(PeanutButterUltimaTestExecutor PRIVATE src tests/common tests/mocks)
target_link_libraries(PeanutButterUltimaTestExecutor PRIVATE PeanutButterUltimaCoreTest)

function(peanut_butter_add_test_suite pSuiteName pSuiteLabel pGlobPattern)
  file(GLOB pSuiteSources CONFIGURE_DEPENDS ${pGlobPattern})
  foreach(pSuiteSource IN LISTS pSuiteSources)
    get_filename_component(pSuiteStem "${pSuiteSource}" NAME_WE)
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" pSuiteStemSanitized "${pSuiteStem}")
    set(pSuiteTarget "PeanutButterUltima${pSuiteName}_${pSuiteStemSanitized}")

    add_executable(${pSuiteTarget} ${pSuiteSource})
    target_include_directories(${pSuiteTarget} PRIVATE src tests/common tests/mocks)
    target_compile_definitions(
      ${pSuiteTarget}
      PRIVATE
      PEANUT_BUTTER_ULTIMA_TEST_BUILD
      PEANUT_BUTTER_ULTIMA_TEST_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/data\"
    )
    target_link_libraries(${pSuiteTarget} PRIVATE PeanutButterUltimaTestExecutor)

    add_test(NAME ${pSuiteTarget} COMMAND ${pSuiteTarget})
    set_tests_properties(${pSuiteTarget} PROPERTIES LABELS "${pSuiteLabel}")
  endforeach()
endfunction()

enable_testing()
add_test(NAME PeanutButterUltimaQuickSmoke COMMAND PeanutButterUltimaQuickSmoke)
set_tests_properties(PeanutButterUltimaQuickSmoke PROPERTIES LABELS "quick_smoke")

peanut_butter_add_test_suite("Codex" "codex" "${CMAKE_CURRENT_SOURCE_DIR}/tests/codex/*.cpp")
peanut_butter_add_test_suite("Generated" "generated" "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/*.cpp")
peanut_butter_add_test_suite("GeneratedGold" "generated_gold" "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated_gold/*.cpp")
