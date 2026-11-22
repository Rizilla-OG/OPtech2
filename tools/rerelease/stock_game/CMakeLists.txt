# optech2: builds id's stock re-release game library (quake2-rerelease-dll,
# which ships only an MSVC project) natively on Linux, out of tree, without
# touching its source. Gives game_x64.so exporting GetGameAPI + GetCGameAPI.
#
#   cmake -S tools/rerelease/stock_game -B <build dir> \
#     -DRR_SOURCE_DIR=<quake2-rerelease-dll>/rerelease -DCMAKE_BUILD_TYPE=Release
#   cmake --build <build dir>
cmake_minimum_required(VERSION 3.24)
project(optech2_rerelease_stock_game CXX)

set(RR_SOURCE_DIR "" CACHE PATH "quake2-rerelease-dll/rerelease folder")
if(NOT EXISTS "${RR_SOURCE_DIR}/g_main.cpp")
	message(FATAL_ERROR "RR_SOURCE_DIR must point at quake2-rerelease-dll/rerelease")
endif()

include(FetchContent)
set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_CMAKE_PACKAGE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_OBJECT_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
FetchContent_Declare(jsoncpp GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git GIT_TAG 1.9.6 GIT_SHALLOW TRUE)
FetchContent_Declare(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG 10.2.1 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(jsoncpp fmt)

file(GLOB GAME_SOURCES
	${RR_SOURCE_DIR}/*.cpp ${RR_SOURCE_DIR}/bots/*.cpp ${RR_SOURCE_DIR}/ctf/*.cpp
	${RR_SOURCE_DIR}/rogue/*.cpp ${RR_SOURCE_DIR}/xatrix/*.cpp)

add_library(game SHARED ${GAME_SOURCES})
set_target_properties(game PROPERTIES OUTPUT_NAME game_x64 PREFIX ""
	CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(game PRIVATE ${RR_SOURCE_DIR})
target_compile_definitions(game PRIVATE KEX_Q2_GAME NO_FMT_SOURCE)
target_compile_options(game PRIVATE -w
	-include ${CMAKE_CURRENT_SOURCE_DIR}/../linux_game_prelude.h)
target_link_libraries(game PRIVATE jsoncpp_static fmt::fmt)
