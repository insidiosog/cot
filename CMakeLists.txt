cmake_minimum_required(VERSION 3.5...4.0)
project(cot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()

find_package(wxWidgets REQUIRED COMPONENTS core base xml)
include(${wxWidgets_USE_FILE})

find_package(SQLite3 REQUIRED)
include_directories(${SQLite3_INCLUDE_DIRS} src)

set(SOURCES
    src/app.cpp
    src/frame.cpp
    src/parser.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})
target_link_libraries(${PROJECT_NAME} PUBLIC ${wxWidgets_LIBRARIES} ${SQLite3_LIBRARIES})
