cmake_minimum_required(VERSION 3.20)
project(FormatWheelWindows LANGUAGES CXX)

add_executable(FormatWheel WIN32 core.cpp main.cpp)
target_compile_features(FormatWheel PRIVATE cxx_std_17)
target_compile_definitions(FormatWheel PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_link_libraries(FormatWheel PRIVATE ole32 oleaut32 windowscodecs comdlg32 shell32 bcrypt gdi32 user32)

if(MSVC)
  target_compile_options(FormatWheel PRIVATE /W4 /EHsc /utf-8)
else()
  target_compile_options(FormatWheel PRIVATE -Wall -Wextra)
endif()
