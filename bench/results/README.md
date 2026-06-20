# nanosig bench 结果

> P8 产出物。原始 stdout 输出按 bench / 日期 / 平台归档到本目录。

## 最近一次运行（2026-06-20，macOS arm64）

| Bench | Iterations / Duration | avg | p50 | p99 | min | max | 备注 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `same_thread_latency` | 100000 iters | 358 ns | 0 ns | 1.0 µs | 0 ns | 35.0 µs | emit → dispatch → slot 全链路 |
| `cross_thread_latency` | 50000 iters | 3.448 µs | 3.0 µs | 18.0 µs | 0 ns | 129.0 µs | emit → wakeup → 远端 slot 往返 |
| `throughput` | 1 s | — | — | — | — | — | emit rate 2.89 M/s，drain rate 2.20 M/s |

> 表格里的 `0 ns` 是 `clock_gettime(CLOCK_MONOTONIC)` 在两次相邻调用间返回同一刻度的产物，不是真实 0 ns；这一现象影响 `min` / `p50` 精度，不影响 `avg` / `p99` / `max` 解释。

### 运行环境

- 平台：macOS Darwin 25.5.0（arm64）
- CPU：Apple M4，10 物理核 / 10 逻辑核
- 内存：16 GB
- 编译器：Apple clang（`/usr/bin/cc`）
- 构建：`Release`（`-O2` 等价物）、无 sanitizer、`NANOSIG_BUILD_BENCH=ON`
- CMake preset：`macos-release`

### 详细原始数据

- `same_thread_2026-06-20_macos-arm64.txt`
- `cross_thread_2026-06-20_macos-arm64.txt`
- `throughput_2026-06-20_macos-arm64.txt`

## 复现命令

```sh
cmake --preset macos-release -DNANOSIG_BUILD_BENCH=ON
cmake --build --preset macos-release \
    --target nanosig_bench_same_thread \
           nanosig_bench_cross_thread \
           nanosig_bench_throughput

cd build
./nanosig_bench_same_thread    > ../bench/results/same_thread_<date>_macos-arm64.txt
./nanosig_bench_cross_thread   > ../bench/results/cross_thread_<date>_macos-arm64.txt
./nanosig_bench_throughput     > ../bench/results/throughput_<date>_macos-arm64.txt
```

`bench_throughput` 单次运行约 1.5 s，CMake 已经把它的 `TIMEOUT` 设为 30 s。
