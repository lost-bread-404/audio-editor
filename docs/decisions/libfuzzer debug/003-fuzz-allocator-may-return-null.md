# 003: fuzz 运行必须带 allocator_may_return_null=1

日期：2026-08-14
相关 PR：待提交（分支 `worktree-fuzz-coverage`）

## 决策

跑 fuzz target 时设这个环境变量：

```bash
ASAN_OPTIONS=allocator_may_return_null=1 ./build-fuzz/fuzz/fuzz_track corpus/
```

理由是要把两种在 ASan 报告里长得几乎一样、但性质完全不同的情况区分开：真 bug 要崩，
不是 bug 的不能崩。

## 理由

harness 加了"极端 pos/len"这类 op 之后，`calloc` 处冒出两种 ASan abort：

**一种是真 bug（已修）**：

```
AddressSanitizer: calloc parameters overflow: count * size (-1 * 2)
```

`ss_track_write(t, 0, SIZE_MAX, buf)` 让样本数换算成字节数时乘法溢出。这是货真价实的
整数溢出，会导致分配到远小于预期的缓冲区然后越界写。已经在 `ss_track_write` 和
`tr_create_original_node` 两处加了"样本数能否换算成字节数"的校验。

**另一种不是 bug**：

```
AddressSanitizer: requested allocation size 0xfffffffffffffffe exceeds
maximum supported size of 0x10000000000
```

`calloc(SIZE_MAX/2, 2)` —— 乘法不溢出，只是要的内存大到荒谬。ASan 默认对超过自己
上限的请求直接 abort，而**正常 allocator 会返回 NULL**，库随即正确返回
`SS_ERR_NO_MEMORY`。这里库的行为是对的，崩的是 ASan 的策略，不是代码的缺陷。

开了 `allocator_may_return_null=1` 之后，第二种变成返回 NULL，库的 OOM 分支被真实走到
（顺带覆盖了一部分本来到不了的分支），而第一种因为是乘法溢出、和这个开关无关，仍然会
正常报错。两者就分开了。

## 放弃的方案

- **在库里加一个"单次分配上限"常量**：能挡住 ASan abort，但那是个凭空拍的数字，而且
  把 allocator 的职责搬进了库。库该做的是保证自己算出来的 size 是合法的，能不能满足
  由 allocator 决定。
- **不加开关，把这类输入从 corpus 里删掉**：等于让 fuzzer 绕开一整类边界输入，而
  `pos`/`len` 的边界恰恰是已经出过两个真 bug 的地方。
- **全局在 CI 里无脑打开**：应该打开，但要连同上面的理由一起记下来，否则以后有人看到
  这个开关会以为是在掩盖 OOM 问题。

## 什么情况下要推翻

- 库真的引入了显式的最大 track 长度策略，那时超限应该在库里就被拒绝，不再依赖
  allocator 返回 NULL。
- 换用不做上限检查的 sanitizer，或者 ASan 改变默认行为。
- 如果将来加了分配失败注入（见 004），需要重新确认这个开关和注入器的交互。
