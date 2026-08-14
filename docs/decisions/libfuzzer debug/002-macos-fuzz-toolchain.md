# 002: macOS 上用 Homebrew LLVM 构建 fuzz target

日期：2026-08-14
相关 PR：待提交（分支 `worktree-fuzz-coverage`）

## 决策

在 macOS 本地开 `-DSOUND_SEG_FUZZ=ON` 时，编译器指定为 Homebrew 的 clang：

```bash
cmake -S . -B build-fuzz -DSOUND_SEG_FUZZ=ON \
      -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
      -DCMAKE_BUILD_TYPE=Debug
```

CI（`.github/workflows/fuzz.yml`，ubuntu-latest）保持用系统 clang 不变，这条只针对
本地 macOS。

## 理由

Command Line Tools 带的 Apple clang 不提供 libFuzzer 运行时。链接直接失败：

```
ld: library 'libclang_rt.fuzzer_osx.a' not found
```

Apple clang 15 的 `lib/clang/15.0.0/lib/darwin/` 下只有 asan / ubsan 的运行时，没有
fuzzer 的。Homebrew llvm 22.1.8 的对应目录下三个都齐：

```
/opt/homebrew/Cellar/llvm/22.1.8/lib/clang/22/lib/darwin/libclang_rt.fuzzer_osx.a
```

README 里只写了"clang only"，没写清楚 macOS 上系统 clang 不算数，踩过一次值得记下来。

## 放弃的方案

- **装完整 Xcode**：体积大，而且没有证据表明 Xcode 附带的工具链就一定带 fuzzer 运行时
  （Apple 一贯不发布 libFuzzer）。为一个不确定的结果装十几 G 不划算。
- **把 libFuzzer 源码 vendored 进仓库**：能摆脱工具链依赖，但要跟着 LLVM 版本维护，
  收益和维护成本不成比例。
- **只在 Linux/CI 上跑 fuzz，本地不跑**：可以，但本地复现崩溃的循环会变得很慢，调试
  aliasing 类 bug 需要快速反复跑。

## 什么情况下要推翻

- Apple 开始在 Command Line Tools 里附带 fuzzer 运行时。
- 开发主力平台换成 Linux，本地 macOS 构建不再需要。
- 换用 Homebrew 之外的 LLVM 分发方式（比如官方 release tarball、nix），路径写死的
  `/opt/homebrew/opt/llvm` 需要改成可配置。
