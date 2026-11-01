/**
 * @file no_watcher.c
 * @brief 负向链接测试：裸机后端**不提供** watcher API。
 * @date 2026-10-31
 *
 * 本文件只在配置期通过 `try_compile` 编译+链接一次，且**必须失败**。
 * 若它能链接成功，说明裸机后端意外引入了 watcher/broker 路径
 * （src/ns_broker.c 被编入，或平台层提供了本该缺席的符号）。
 *
 * 注意：必须 `#include` 真实公开头。若手工重新声明原型，失败会发生在编译期
 * 而非链接期，测不到"符号不存在"这件事。
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#include <nanosig/nanosig_broker.h>

int main(void)
{
    ns_watcher_t watcher;

    return ns_watcher_init(&watcher, NULL, NS_WAITABLE_EVENT_IN, 0, NULL);
}
