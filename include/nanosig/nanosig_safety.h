/**
 * @file nanosig_safety.h
 * @brief nanosig 未来安全/ISR 注解的预留扩展点。
 * @date 2026-05-16
 *
 * @copyright Copyright (c) 2026 nanosig contributors
 */

#ifndef NANOSIG_SAFETY_H
#define NANOSIG_SAFETY_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PD 阶段的安全注解约束。
 *
 * nanosig v1 当前公开 API 不沿用 eventhub_os 的 `__safety` 或 ISR 注解。
 * 该头文件仅作为未来 v2 MCU/ISR 审计的扩展点保留，当前不定义任何会出现在
 * 公开函数声明中的安全注解宏。
 */

#ifdef __cplusplus
}
#endif

#endif /* NANOSIG_SAFETY_H */
