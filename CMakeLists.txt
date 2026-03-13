cmake_minimum_required(VERSION 3.21)

project(PeanutButterUltima LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(PEANUT_BUTTER_ULTIMA_BUILD_APP "Build PeanutButterUltima Qt application." ON)

if(PEANUT_BUTTER_ULTIMA_BUILD_APP)
  find_package(Qt6 QUIET COMPONENTS Widgets)
  if(Qt6_FOUND)
    set(PB_QT_TARGET Qt6::Widgets)
  else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(PB_QT_TARGET Qt5::Widgets)
  endif()
endif()

set(PB_WINDOWS_RC_SOURCE "")
if(WIN32)
  set(PB_WINDOWS_ICON
      "${CMAKE_SOURCE_DIR}/icon.ico"
      CACHE FILEPATH "Path to .ico file embedded into Windows executables.")
  if(EXISTS "${PB_WINDOWS_ICON}")
    file(TO_CMAKE_PATH "${PB_WINDOWS_ICON}" PB_WINDOWS_ICON_CMAKE_PATH)
    configure_file("${CMAKE_SOURCE_DIR}/src/WindowsIcon.rc.in"
                   "${CMAKE_BINARY_DIR}/WindowsIcon.rc"
                   @ONLY)
    set(PB_WINDOWS_RC_SOURCE "${CMAKE_BINARY_DIR}/WindowsIcon.rc")
  else()
    message(STATUS "PB_WINDOWS_ICON not found at '${PB_WINDOWS_ICON}'; Windows executables will use default icon.")
  endif()
endif()

if(PEANUT_BUTTER_ULTIMA_BUILD_APP)
  set(PB_SOURCES
      src/main_qt.cpp
      src/AppShell_ArchiveFormat.cpp
      src/AppShell_Bundle.cpp
      src/AppShell_Common.cpp
      src/AppShell_Extended_Bundle.cpp
      src/AppShell_Extended_Types.cpp
      src/AppShell_Sanity.cpp
      src/AppShell_Types.cpp
      src/AppShell_Unbundle.cpp
      src/Encryption/Crypt.cpp
      src/IO/FileSystem.cpp
      src/IO/LocalFileSystem.cpp
      ${PB_WINDOWS_RC_SOURCE}
  )

  if(APPLE)
    add_executable(PeanutButterUltima MACOSX_BUNDLE ${PB_SOURCES})
  else()
    add_executable(PeanutButterUltima ${PB_SOURCES})
  endif()

  target_include_directories(PeanutButterUltima PRIVATE src)
  target_link_libraries(PeanutButterUltima PRIVATE ${PB_QT_TARGET})
  target_compile_definitions(
      PeanutButterUltima
      PRIVATE
        $<$<CONFIG:Debug>:PEANUT_BUTTER_ULTIMA_DEBUG_BUILD>
        $<$<CONFIG:Release>:PEANUT_BUTTER_ULTIMA_RELEASE_BUILD>
  )
endif()
