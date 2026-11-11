# Cross-compiling the Windows app from macOS or Linux with mingw-w64
# (brew install mingw-w64, or apt install mingw-w64):
#   cmake -S . -B build/win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
# Debian/Ubuntu ship two flavours; the -posix one has std::thread everywhere.
find_program(MINGW_CC NAMES x86_64-w64-mingw32-gcc-posix x86_64-w64-mingw32-gcc REQUIRED)
find_program(MINGW_CXX NAMES x86_64-w64-mingw32-g++-posix x86_64-w64-mingw32-g++ REQUIRED)
set(CMAKE_C_COMPILER ${MINGW_CC})
set(CMAKE_CXX_COMPILER ${MINGW_CXX})
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
