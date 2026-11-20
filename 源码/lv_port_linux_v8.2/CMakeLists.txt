cmake_minimum_required(VERSION 3.1)

# ========== 交叉编译工具链 ==========
set(TOOLCHAIN_DIR "/usr/local/arm/5.4.0/usr")
set(CMAKE_C_COMPILER "${TOOLCHAIN_DIR}/bin/arm-linux-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_DIR}/bin/arm-linux-g++")

# ========== 项目 ==========
project(main LANGUAGES C)

# ========== 编译宏（启用 textarea 和 keyboard） ==========
add_compile_definitions(
    LV_CONF_INCLUDE_SIMPLE
    LV_USE_TEXTAREA=1
    LV_USE_KEYBOARD=1
)

# ========== 包含路径（包含所有需要的头文件目录） ==========
include_directories(
    .
    my_inc
    my_data
    my_scr
    lvgl
    lv_drivers
    lvgl/src
    lvgl/src/extra
    /home/ppl/workspace/output/curl/include
    /home/ppl/workspace/output/openssl/include
    /home/ppl/workspace/output/zlib/include
)

# ========== 子模块 ==========
add_subdirectory(lvgl)
add_subdirectory(lv_drivers)

# ========== 源文件（显式添加需要的 extra 源文件） ==========
set(SOURCES
    main.c
    mouse_cursor_icon.c
    my_scr/my_ai_project.c
    my_scr/cJSON.c
    my_data/login_bg.c
    my_data/chat_bg.c
    my_data/lv_font_hei16_cjk.c
)
# 注意：lv_textarea.c / lv_keyboard.c 不能再列在这里。
#  1) lvgl 的 CMakeLists 已经用 file(GLOB_RECURSE src/*.c) 把整个 src 目录编进 liblvgl.a；
#  2) keyboard 的真实路径是 lvgl/src/extra/widgets/keyboard/lv_keyboard.c，
#     写 lvgl/src/widgets/lv_keyboard.c 会让 CMake 在 generate 阶段直接报
#     "Cannot find source file"，这是当前 configure 失败的头号原因。
# 另外 keyboard/textarea 的开关在 lv_conf.h 里已经是 1，无需再在命令行重复定义。

# ========== 输出路径 ==========
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build)

# ========== 可执行文件 ==========
add_executable(${PROJECT_NAME} ${SOURCES})

# ========== 链接库 ==========
set(CURL_DIR "/home/ppl/workspace/output/curl")
set(OPENSSL_DIR "/home/ppl/workspace/output/openssl")
set(ZLIB_DIR "/home/ppl/workspace/output/zlib")

target_link_libraries(${PROJECT_NAME} PRIVATE
    lvgl
    lv_drivers
    ${CURL_DIR}/lib/libcurl.a
    ${OPENSSL_DIR}/lib/libssl.a
    ${OPENSSL_DIR}/lib/libcrypto.a
    ${ZLIB_DIR}/lib/libz.a
    pthread
    dl
    m
)