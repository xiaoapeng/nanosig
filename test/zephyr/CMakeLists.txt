cmake_minimum_required(VERSION 3.20.0)

# 把仓库根目录加入额外 module 列表，使仓库根 zephyr/module.yml 生效。
# 变量名与 Zephyr 文档一致（EXTRA_ZEPHYR_MODULES）；也可用同名环境变量
# （等价于 .zephyrrc）或命令行 -DEXTRA_ZEPHYR_MODULES=<repo root>。
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(nanosig_zephyr_tests)

target_sources(app PRIVATE src/main.c)
