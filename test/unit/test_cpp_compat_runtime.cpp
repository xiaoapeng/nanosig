/**
 * @file test_cpp_compat_runtime.cpp
 * @brief C++ 侧 `NS_LOOP_CONFIG_DEFAULT()` 的**运行时**契约测试。
 * @date 2026-10-24
 *
 * `test_cpp_compat_compile.cpp` 只做 `-fsyntax-only` 检查且没有 `main`，因此那里
 * 对 `NS_LOOP_CONFIG_DEFAULT()` 的任何断言都不会执行。本文件补上真正会运行的
 * 那一半：C++ 分支是**位置初始化**（C++11 没有指定初始化器），字段顺序或默认值
 * 一旦错位，编译器不会诊断，只有运行时逐字段断言才能抓到。
 *
 * 覆盖范围与已知盲区：
 * - 能抓到：字段**顺序被调整**导致值落到别的成员、某个字段的**默认值被改错**。
 * - 抓不到：`ns_loop_config_t` 新增字段但忘了同步 C++ 位置初始化 ——
 *   漏写的成员会被值初始化为 0，而本测试期望的默认值恰好也是 0，断言仍然通过。
 *   那一类问题由 `cmake/ApiCompileChecks.cmake` 的
 *   `-Werror=missing-field-initializers` 负责。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <cstdio>

#include <nanosig/nanosig.h>
#include <nanosig/nanosig_loop.h>

namespace {

int failures = 0;

void expect(bool cond, const char *what)
{
    if(!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

/* 逐字段断言 C++ 分支的默认配置。
   字段顺序被调整时，`NS_LOOP_CONFIG_DEFAULT()` 的位置初始化会把值写进别的成员，
   对应的 expect 就会失败。 */
void test_loop_config_default_cpp_branch()
{
    const ns_loop_config_t cfg = NS_LOOP_CONFIG_DEFAULT();

    expect(cfg.queue_byte_capacity == NS_CAPACITY_1024, "queue_byte_capacity == NS_CAPACITY_1024");
    expect(cfg.flags == 0u, "flags == 0");
    expect(cfg.debug_name == nullptr, "debug_name == nullptr");
    expect(cfg.stack_size == 0u, "stack_size == 0 (means: use backend default)");
}

/* 默认配置必须能真正被 ns_loop_create 接受（即"默认值"是合法输入，不是占位符）。 */
void test_loop_config_default_is_accepted()
{
    ns_loop_config_t cfg = NS_LOOP_CONFIG_DEFAULT();
    ns_loop_t *loop = nullptr;

    if(ns_init() != NS_OK) {
        std::fprintf(stderr, "FAIL: ns_init()\n");
        ++failures;
        return;
    }

    expect(ns_loop_create(&loop, &cfg) == NS_OK, "ns_loop_create(NS_LOOP_CONFIG_DEFAULT()) == NS_OK");
    expect(loop != nullptr, "loop != nullptr");

    if(loop != nullptr) expect(ns_loop_destroy(loop) == NS_OK, "ns_loop_destroy()");
    expect(ns_shutdown() == NS_OK, "ns_shutdown()");
}

}  // namespace

int main()
{
    test_loop_config_default_cpp_branch();
    test_loop_config_default_is_accepted();

    if(failures != 0) {
        std::fprintf(stderr, "%d C++ runtime contract check(s) failed\n", failures);
        return 1;
    }

    return 0;
}
