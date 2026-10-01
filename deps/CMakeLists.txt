cmake_minimum_required(VERSION 3.20)
project(WinPopupToxDependencies LANGUAGES C)
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Build these dependencies with MSVC x64")
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(CMAKE_C_STANDARD 11)
set(TOX_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/toxcore-full")
set(PTHREAD_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/pthreads/pthreads4w-code-07053a521b0a9deb6db2a649cde1f828f2eb1f4f")
set(SODIUM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/sodium-msvc/libsodium")
# Use the exact upstream production source list, excluding optional audio/video.
file(READ "${TOX_SOURCE}/CMakeLists.txt" TOX_CMAKE)
string(REGEX MATCH "set\\(toxcore_SOURCES[^)]*\\)" TOX_SOURCE_COMMAND "${TOX_CMAKE}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/tox-sources.cmake" "${TOX_SOURCE_COMMAND}")
include("${CMAKE_CURRENT_BINARY_DIR}/tox-sources.cmake")
list(TRANSFORM toxcore_SOURCES PREPEND "${TOX_SOURCE}/")
list(APPEND toxcore_SOURCES "${TOX_SOURCE}/toxencryptsave/toxencryptsave.c")
add_library(toxcore STATIC ${toxcore_SOURCES})
target_compile_definitions(toxcore PRIVATE SODIUM_STATIC __PTW32_STATIC_LIB CMP_NO_FLOAT=1 MIN_LOGGER_LEVEL=LOGGER_LEVEL_WARNING _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
target_compile_options(toxcore PRIVATE /O2 /Ob2 /Gy /W3 /wd4244 /wd4267 /wd4018 /wd4996)
target_compile_options(toxcore PRIVATE /experimental:deterministic
  "/pathmap:${CMAKE_CURRENT_SOURCE_DIR}=deps"
  "/pathmap:${CMAKE_CURRENT_BINARY_DIR}=deps/build")
target_include_directories(toxcore PRIVATE "${SODIUM_SOURCE}/include" "${PTHREAD_SOURCE}")
install(TARGETS toxcore ARCHIVE DESTINATION lib)
install(FILES
  "${TOX_SOURCE}/toxcore/tox.h"
  "${TOX_SOURCE}/toxcore/tox_options.h"
  "${TOX_SOURCE}/toxcore/tox_log_level.h"
  "${TOX_SOURCE}/toxencryptsave/toxencryptsave.h"
  DESTINATION include/tox)
install(DIRECTORY "${SODIUM_SOURCE}/include/" DESTINATION include)
install(FILES "${PTHREAD_SOURCE}/pthread.h" "${PTHREAD_SOURCE}/sched.h" "${PTHREAD_SOURCE}/semaphore.h" "${PTHREAD_SOURCE}/_ptw32.h" DESTINATION include)
install(FILES "${SODIUM_SOURCE}/x64/Release/v143/static/libsodium.lib" DESTINATION lib)
install(FILES "${PTHREAD_SOURCE}/libpthreadVC3.lib" DESTINATION lib RENAME pthreadVC3.lib)
install(FILES "${TOX_SOURCE}/LICENSE" DESTINATION licenses RENAME toxcore-GPL-3.0.txt)
install(FILES "${TOX_SOURCE}/third_party/cmp/LICENSE" DESTINATION licenses RENAME cmp-MIT.txt)
install(FILES "${PTHREAD_SOURCE}/LICENSE" DESTINATION licenses RENAME pthreads4w-Apache-2.0.txt)
install(FILES "${PTHREAD_SOURCE}/NOTICE" DESTINATION licenses RENAME pthreads4w-NOTICE.txt)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/libsodium-1.0.22/LICENSE" DESTINATION licenses RENAME libsodium-ISC.txt)
