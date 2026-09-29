cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

set(SHORT_NAME prism)
project(${SHORT_NAME} C CXX)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "PRISM")
set(VITA_TITLEID  "PRISM0001")
set(VITA_VERSION  "00.24")
# ATTRIBUTE2=12: large memory mode for big banks
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1 -d ATTRIBUTE2=12")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -Wall -Wno-unused-function -fsigned-char -flto")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto -O3")

include_directories(${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/../munt/build/include)
link_directories(${PROJECT_SOURCE_DIR}/../munt/build)

# user shim of the MIDI kernel module: weak imports bound when the shim loads
set(SKPRX_BUILD ${PROJECT_SOURCE_DIR}/module)
set(SUPRX_BUILD ${PROJECT_SOURCE_DIR}/module)
link_directories(${SUPRX_BUILD}/stubs)

add_executable(${SHORT_NAME}
  main.c
  synth.c
  perf.c
  cfg.c
  fx.c
  mt32.c
  ui.c
  gfx.c
  platform_vita.c
)

set_target_properties(${SHORT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(${SHORT_NAME}
  mt32emu
  VitaUsbMidiUser_stub_weak
  taihen_stub
  SceDisplay_stub
  SceCtrl_stub
  SceTouch_stub
  SceAudio_stub
  ScePower_stub
  m
)

# UNSAFE: the app loads its kernel module (taiLoadStartKernelModule)
vita_create_self(${SHORT_NAME}.self ${SHORT_NAME} UNSAFE)

vita_create_vpk(${SHORT_NAME}.vpk ${VITA_TITLEID} ${SHORT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE ${SKPRX_BUILD}/VitaUsbMidi.skprx module/VitaUsbMidi.skprx
  FILE ${SUPRX_BUILD}/VitaUsbMidiUser.suprx module/VitaUsbMidiUser.suprx
  FILE ${PROJECT_SOURCE_DIR}/sf/GeneralUser-GS.sf2 sf/GeneralUser-GS.sf2
  FILE ${PROJECT_SOURCE_DIR}/sf/LICENSE.txt sf/LICENSE-GeneralUser-GS.txt
  FILE ${PROJECT_SOURCE_DIR}/fonts/src/OFL.txt fonts/OFL-IBM-Plex.txt
  FILE ${PROJECT_SOURCE_DIR}/sce_sys/icon0.png sce_sys/icon0.png
  FILE ${PROJECT_SOURCE_DIR}/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE ${PROJECT_SOURCE_DIR}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE ${PROJECT_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE ${PROJECT_SOURCE_DIR}/credits/CREDITS.txt credits/CREDITS.txt
  FILE ${PROJECT_SOURCE_DIR}/credits/LGPL-2.1-Munt.txt credits/LGPL-2.1-Munt.txt
  FILE ${PROJECT_SOURCE_DIR}/credits/Munt-AUTHORS.txt credits/Munt-AUTHORS.txt
  FILE ${PROJECT_SOURCE_DIR}/credits/MIT-TinySoundFont.txt credits/MIT-TinySoundFont.txt
  FILE ${PROJECT_SOURCE_DIR}/credits/munt-prism-parallel.patch credits/munt-prism-parallel.patch
  FILE ${PROJECT_SOURCE_DIR}/fonts/sans11.fnt fonts/sans11.fnt
  FILE ${PROJECT_SOURCE_DIR}/fonts/sans12.fnt fonts/sans12.fnt
  FILE ${PROJECT_SOURCE_DIR}/fonts/sans13.fnt fonts/sans13.fnt
  FILE ${PROJECT_SOURCE_DIR}/fonts/sansm15.fnt fonts/sansm15.fnt
  FILE ${PROJECT_SOURCE_DIR}/fonts/sanss17.fnt fonts/sanss17.fnt
  FILE ${PROJECT_SOURCE_DIR}/fonts/sanss34.fnt fonts/sanss34.fnt
  FILE ${PROJECT_SOURCE_DIR}/fonts/mono10.fnt fonts/mono10.fnt
  FILE ${PROJECT_SOURCE_DIR}/fonts/mono11.fnt fonts/mono11.fnt
  FILE ${PROJECT_SOURCE_DIR}/fonts/mono12.fnt fonts/mono12.fnt
  FILE ${PROJECT_SOURCE_DIR}/fonts/monom14.fnt fonts/monom14.fnt
  FILE ${PROJECT_SOURCE_DIR}/fonts/monom44.fnt fonts/monom44.fnt
)
