# Cross-compile a Windows x64 DLL on Linux with clang-cl + lld-link, against the
# MSVC CRT and Windows SDK downloaded by xwin (`xwin splat`).
#   XWIN_DIR: the splat output folder (default ~/.local/share/xwin/sdk)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER lld-link)
set(CMAKE_AR llvm-lib)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)

if(DEFINED ENV{XWIN_DIR})
  set(XWIN_DIR "$ENV{XWIN_DIR}")
else()
  set(XWIN_DIR "$ENV{HOME}/.local/share/xwin/sdk")
endif()

set(_xwin_includes
  "/imsvc${XWIN_DIR}/crt/include"
  "/imsvc${XWIN_DIR}/sdk/include/ucrt"
  "/imsvc${XWIN_DIR}/sdk/include/um"
  "/imsvc${XWIN_DIR}/sdk/include/shared"
  "/imsvc${XWIN_DIR}/sdk/include/winrt")
string(JOIN " " _xwin_includes ${_xwin_includes})

set(_target_flags "--target=x86_64-pc-windows-msvc -fms-compatibility-version=19.40 ${_xwin_includes}")
set(CMAKE_C_FLAGS_INIT "${_target_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_target_flags} /EHsc")

set(_xwin_libs
  "/libpath:${XWIN_DIR}/crt/lib/x86_64"
  "/libpath:${XWIN_DIR}/sdk/lib/um/x86_64"
  "/libpath:${XWIN_DIR}/sdk/lib/ucrt/x86_64")
string(JOIN " " _xwin_libs ${_xwin_libs})
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_xwin_libs} /manifest:no")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_xwin_libs} /manifest:no")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_xwin_libs} /manifest:no")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
