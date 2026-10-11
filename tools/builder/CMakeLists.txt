cmake_minimum_required(VERSION 3.16)
project(tcoaal_builder C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(ENGINE_PBP "${CMAKE_CURRENT_SOURCE_DIR}/../../build/psp/EBOOT.PBP" CACHE FILEPATH "engine EBOOT.PBP to embed")
set(PSP_ART "${CMAKE_CURRENT_SOURCE_DIR}/art/psp.png")

add_executable(tcoaal_embed tools/embed.c)

set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN})
add_custom_command(OUTPUT ${GEN}/engine_blob.c
  COMMAND tcoaal_embed ${ENGINE_PBP} ${GEN}/engine_blob.c embedded_engine
  DEPENDS tcoaal_embed ${ENGINE_PBP})
add_custom_command(OUTPUT ${GEN}/art_blob.c
  COMMAND tcoaal_embed ${PSP_ART} ${GEN}/art_blob.c embedded_psp_art
  DEPENDS tcoaal_embed ${PSP_ART})

file(GLOB BUILDER_SRC CONFIGURE_DEPENDS src/*.c)
add_executable(tcoaal-builder ${BUILDER_SRC} third_party/miniz.c ${GEN}/engine_blob.c ${GEN}/art_blob.c)
target_include_directories(tcoaal-builder PRIVATE src third_party)
target_compile_definitions(tcoaal-builder PRIVATE _DEFAULT_SOURCE _FILE_OFFSET_BITS=64 MINIZ_NO_STDIO)
if(WIN32)
  target_link_libraries(tcoaal-builder PRIVATE ole32 shell32 user32 gdi32 advapi32 uuid)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(tcoaal-builder PRIVATE Threads::Threads m)
endif()

set(LIB_SRC ${BUILDER_SRC})
list(FILTER LIB_SRC EXCLUDE REGEX "/main\\.c$")
add_executable(tcoaal-builder-tests tests/test.c ${LIB_SRC} third_party/miniz.c)
target_include_directories(tcoaal-builder-tests PRIVATE src third_party)
target_compile_definitions(tcoaal-builder-tests PRIVATE _DEFAULT_SOURCE MINIZ_NO_STDIO)
if(WIN32)
  target_link_libraries(tcoaal-builder-tests PRIVATE ole32 shell32 user32 gdi32 advapi32 uuid)
else()
  target_link_libraries(tcoaal-builder-tests PRIVATE Threads::Threads m)
endif()
enable_testing()
add_test(NAME builder COMMAND tcoaal-builder-tests)
