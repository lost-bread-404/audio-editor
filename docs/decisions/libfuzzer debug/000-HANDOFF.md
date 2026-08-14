# 000: libFuzzer 调试交接说明

日期：2026-08-14
相关 PR：待提交（分支 `worktree-fuzz-coverage`）
状态：**未完成**，见文末「下一步」

> 这份不是决策记录，是给接手的人看的现场说明。同目录下 001–004 才是按 CLAUDE.md
> 规矩写的决策记录，先读那四份再动手。

## 现在是什么状态

- 已修 5 个 fuzz 发现的内存安全 bug（3 个 use-after-free + 2 个整数范围错误）。
- 分支覆盖率 `src/` 62% → **88.3%**，31 个函数全部执行到。
- 最后一轮 20 分钟 **695,320 次执行零崩溃**；5 个回归输入全过；5 个单元测试全过。
- **剩下的 12% 全部不可达**，除非加分配失败注入。原因见 004。
- **已知未修的 bug**：`tr_split_one` / `tr_split` 里 7 处分配不检查返回值。见下。

## 环境（踩过的坑，别重踩）

macOS 系统 clang **没有** libFuzzer 运行时，链接会失败。必须用 Homebrew LLVM（详见 002）：

```bash
cmake -S . -B build-fuzz -DSOUND_SEG_FUZZ=ON \
      -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
      -DCMAKE_BUILD_TYPE=Debug
cmake --build build-fuzz -j
```

跑的时候**必须**带这个环境变量，否则会被一堆不是 bug 的 ASan abort 淹掉（详见 003）：

```bash
ASAN_OPTIONS=allocator_may_return_null=1 \
  ./build-fuzz/fuzz/fuzz_track -max_total_time=900 \
  -artifact_prefix=build-fuzz/findings/ build-fuzz/corpus
```

几个容易浪费时间的点：

- **语料在 `build-fuzz/corpus/`**，不在仓库根的 `corpus/`。
- **给 fuzz_track 传目录 = 开始 fuzz**，不是回放。只想回放要显式 `-runs=1` 并把文件
  一个个列出来。我第一次传了 `fuzz/regressions/` 目录，结果它往里写了 500 多个语料
  文件，得清理。
- 回归输入现在**已经进版本库**了（`.gitignore` 里为 `fuzz/regressions/` 加了例外）。
  原来的 `crash-*` 规则会把它们全忽略掉，导致换个 checkout 就没法验证修复。

## 回放回归输入

```bash
ASAN_OPTIONS=allocator_may_return_null=1 ./build-fuzz/fuzz/fuzz_track -runs=1 \
  fuzz/regressions/crash-169d60f3032c49ea4c070ef8080fb8438fed6ae2 \
  fuzz/regressions/crash-58541baef0aff6261a7a17e67e0acdfd4e8bc0bc \
  fuzz/regressions/crash-640f0fb27430785ce7dc145407d101e8385e0ce5 \
  fuzz/regressions/crash-655f2b71ddfafbcbd5af517f02eb9386a2a7a2a1 \
  fuzz/regressions/crash-278a8e297036c50cec1e8ad438ba49cc9573b456
```

五个文件分别对应下面五个 bug，顺序一致。

## 量覆盖率

覆盖率要单独编一份（不带 ASan，带 profile 插桩）：

```bash
/opt/homebrew/opt/llvm/bin/clang -g -O0 \
  -fprofile-instr-generate -fcoverage-mapping -fsanitize=fuzzer \
  -I include -I src src/sound_seg.c src/sound_seg_extra.c fuzz/fuzz_track.c \
  -o /tmp/fuzz_cov

LLVM_PROFILE_FILE=/tmp/cov.profraw /tmp/fuzz_cov -runs=0 build-fuzz/corpus
/opt/homebrew/opt/llvm/bin/llvm-profdata merge -sparse /tmp/cov.profraw -o /tmp/cov.profdata
/opt/homebrew/opt/llvm/bin/llvm-cov report /tmp/fuzz_cov -instr-profile=/tmp/cov.profdata
```

看具体哪条分支没覆盖：

```bash
/opt/homebrew/opt/llvm/bin/llvm-cov show /tmp/fuzz_cov \
  -instr-profile=/tmp/cov.profdata -show-branches=count \
  -sources src/sound_seg.c | grep -B4 "True: 0,"
```

## 已修的 5 个 bug

前三个是同一个根因的不同侧面：节点被释放了，别的 track 还指着它。关键背景是
`ss_track_insert` 允许把一个 track 的片段插回它**自己**，于是单个 track 内部也会有
别名，「引用者一定在别的 track 里」这个隐含假设不成立。

| # | 位置 | 触发方式 | 性质 |
|---|---|---|---|
| 1 | `tr_transfer_ownership` | 自插入造出别名链后销毁 | 移交只跳一层，落在同样将被释放的节点上 |
| 2 | `ss_track_destroy` | 同上 | 在自己还挂在注册表上时找继承者，从将死的 track 里挑了继承者 |
| 3 | `ss_track_destroy` | 同上 | 「持有数据」≠「会活下来」，爬链停在将死 track 的节点上 |
| 4 | `ss_track_write` | `write(t, 0, SIZE_MAX, buf)` | 只校验加法回绕，没校验结果是否可能存在，乘法在 allocator 里溢出 |
| 5 | `ss_track_write` | 先 append 若干样本，再 `write(t, SIZE_MAX, 1, buf)` | 守卫检查 `pos - total`，实际相加的是 `pos + len` |

3 的最终形态是销毁时分两趟移交（先别名、后所有权），顺序是关键 —— 完整理由在 001。

## 还没修的：split 路径的分配失败

`src/sound_seg_extra.c` 里 7 处 `calloc`/`realloc` 之后直接解引用，没有 NULL 检查：

```
tr_split_one:  154, 158, 163, 180, 183
tr_split:      209, 227
```

```c
struct sound_seg *new_node = calloc(1, sizeof(struct sound_seg));
new_node->len = length;          // calloc 失败 = 空指针解引用
```

而且两个函数都返回 `void`，**结构上没有报告失败的能力**，调用方
`ss_track_delete` / `ss_track_insert` 也无从检查。

**为什么没顺手修**：`tr_split` 是边遍历边改图的，中途失败会留下「一半分裂完、一半没
分裂」的链表，长度字段和 parent 指针不一致。直接 return 等于把这个坏结构交给调用方，
比当场崩更难查；要回滚就得再写一套逆向逻辑；想预分配也不行，因为要分裂多少节点取决于
遍历中发现多少引用者，事先不知道。完整论证在 004。

**这也是覆盖率卡在 88% 的原因** —— 剩下的分支全是分配失败、I/O 失败（`fseek`/`fclose`）
和内部调用者走不到的防御性判断，输入变异到不了，得靠故障注入。

## harness 结构

`fuzz/fuzz_track.c` 把输入字节读成一串操作，`op % 14` 分发：

| op | 作用 |
|---|---|
| 0–4 | append / 覆写 / 读 / 删 / 插入（插入是别名的来源） |
| 5 | 销毁并重建 track，制造注册表churn |
| 6 | identify |
| 7 | 只读查询 + 所有错误码字符串 |
| 8 | NULL / 零长度 / 越界参数，走各种校验分支 |
| 9 | WAV 存取往返，含必须被拒的畸形文件 |
| 10 | 第二个 context，所有跨 context 调用都必须失败 |
| 11 | identify 的静音 ad 和非零样本两条路径 |
| 12 | 接近 `SIZE_MAX` 的 pos/len（bug 4、5 就是这里抓到的） |
| 13 | 同时持有多个 track，把注册表撑过初始容量 |

7–13 是这次加的；原来只有 0–6，`src/` 的 WAV I/O、错误码、跨 context 完全没被碰过。

## 下一步（按优先级）

1. **决定 split 路径怎么办**。三个选项在 004 里列了：维持现状 / 只加故障注入证明问题 /
   完整重构成可失败。重构的难点是回滚，不是加 if —— 想清楚失败语义再动手。
   一个可能方向：先把新链表完整构造好，成功后再原子替换，失败就整体丢弃。
2. **跑更久**。目前最长单轮 20 分钟。建议挂几小时，或用 `-fork=N` 并行。
3. **`(int)` 截断**。`ss_track_read` / `ss_track_write` 里 `tr_find(&node, (int)pos)`
   把 `size_t` 截成 `int`。track 超过 `INT_MAX` 个样本就会出错。fuzz 到不了（需要 4GB+
   分配），但它是真的，属于设计层面的限制。
4. **把 fuzz 接进 CI 的回归回放**。现在 `.github/workflows/fuzz.yml` 每晚跑 5 分钟随机
   fuzz，但**不回放 `fuzz/regressions/`**。回归输入现在进版本库了，加一步回放很便宜。
