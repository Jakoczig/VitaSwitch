cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ENV{VITASDK})
  message(FATAL_ERROR "Set VITASDK to the VitaSDK installation directory")
endif()

project(vita-switcher C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaSwitch")
set(VITA_TITLEID "SWCH00001")
set(VITA_VERSION "01.22")

add_executable(${PROJECT_NAME} main.c)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(${PROJECT_NAME}
  SceIofilemgr_stub
  SceKernelThreadMgr_stub
  ScePower_stub
  SceLibKernel_stub
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys sce_sys
)
