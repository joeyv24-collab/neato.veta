cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(neato C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Neato Saltreach")
set(VITA_TITLEID  "NEAT00001")
set(VITA_VERSION  "00.01")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -std=gnu99 -ffast-math -mfpu=neon -mcpu=cortex-a9 -Wall -Wno-misleading-indentation")

add_executable(neato
  src/main_vita.c
  src/platform_vita.c
  src/gfx_gl.c
  src/game.c
  src/world.c
  src/enemy.c
  src/model.c
  src/anim.c
)

target_link_libraries(neato
  vitaGL
  vitashark
  SceShaccCgExt
  mathneon
  taihen_stub
  SceShaccCg_stub
  SceKernelDmacMgr_stub
  SceGxm_stub
  SceDisplay_stub
  SceCtrl_stub
  SceTouch_stub
  ScePower_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceCommonDialog_stub
  SceIme_stub
  SceSysmodule_stub
  SceAudio_stub
  SceLibKernel_stub
  stdc++
  m
)

vita_create_self(eboot.bin neato UNSAFE)

# every file in assets/ goes into the vpk
file(GLOB GAME_ASSETS RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/assets/*)
set(ASSET_ARGS "")
foreach(a ${GAME_ASSETS})
  list(APPEND ASSET_ARGS FILE ${CMAKE_SOURCE_DIR}/${a} ${a})
endforeach()

vita_create_vpk(neato.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  ${ASSET_ARGS}
)
