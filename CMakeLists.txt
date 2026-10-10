cmake_minimum_required(VERSION 3.28...4.0)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# Activamos Qt y la API de interfaz de OBS (necesarias para crear un Dock)
set(ENABLE_FRONTEND_API ON CACHE BOOL "Use obs-frontend-api for UI functionality" FORCE)
set(ENABLE_QT ON CACHE BOOL "Use Qt functionality" FORCE)

include(compilerconfig)
include(defaults)
include(helpers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Para que un simple aviso del compilador no detenga la compilación
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Widgets)

set_target_properties(
  ${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /Zc:__cplusplus>)

# ---------------------------------------------------------------------------
# TagLib: librería para leer Título / Artista / Álbum / Duración de los archivos
# (se descarga y se compila sola, no tienes que instalar nada)
# ---------------------------------------------------------------------------
include(FetchContent)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
set(WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

FetchContent_Declare(
  taglib
  GIT_REPOSITORY https://github.com/taglib/taglib.git
  GIT_TAG v2.0.2
  GIT_SHALLOW TRUE
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(taglib)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE tag)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TAGLIB_STATIC)
target_include_directories(
  ${CMAKE_PROJECT_NAME}
  PRIVATE ${taglib_SOURCE_DIR}/taglib ${taglib_SOURCE_DIR}/taglib/toolkit
          ${taglib_BINARY_DIR})

target_sources(
  ${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.cpp src/music-dock.hpp
                                src/music-dock.cpp)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME
                                                                 ${_name})
