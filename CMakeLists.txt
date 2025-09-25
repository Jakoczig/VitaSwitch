cmake_minimum_required(VERSION 3.5)
project(vita-switcher)

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

# --- App info ---
set(VITA_APP_NAME "VitaSwitch")
set(VITA_TITLEID  "SWCH00001")
set(VITA_VERSION  "01.21")

# --- Source files ---
add_executable(${PROJECT_NAME}
  main.c
)

# --- Link Vita stubs ---
target_link_libraries(${PROJECT_NAME}
    vita2d
    SceLibKernel_stub
    SceAppMgr_stub
    SceCommonDialog_stub
    SceDisplay_stub
    SceGxm_stub
    SceCtrl_stub
    ScePgf_stub
    ScePower_stub
    m
)

# --- Build SELF ---
vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)

# --- Build VPK ---
# Include the full sce_sys folder (icon0.png, bg.png, livearea, etc.)
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys sce_sys
)