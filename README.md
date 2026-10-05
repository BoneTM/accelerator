# Accelerator

为 Linux 游戏进程生成 Breakpad 崩溃转储和文本堆栈报告。

## 输出目录与文件

初始化时通过 `/proc/self/exe` 定位游戏可执行文件，固定输出到其旁边的
`breakpad/` 目录，不依赖启动时的工作目录或后续 `chdir`。
CS2 对应 `<server_root>/game/bin/linuxsteamrt64/breakpad/`，与 parasite 的
`services/server/lifecycle.rs::breakpad_dir` 保持一致。

每次崩溃保留 Breakpad 生成的唯一文件名：

- `<uuid>.dmp`：原始转储，文本解析成功或失败都不会删除。
- `<uuid>.dmp.txt`：辅助文本报告，与原始转储一一对应；解析失败时可能缺失，写入失败时可能不完整。
- `crash_error.log`：转储生成失败时的诊断信息。

文本报告生成失败不会把已经成功生成的 dmp 判为失败。
原始 dmp 可以使用独立工具重新解析：

```sh
./minidump_processor /path/to/breakpad/<uuid>.dmp
```

工具会在同目录生成或覆盖 `<uuid>.dmp.txt`，保留原始 dmp。
补充分析建议在下载的崩溃包副本上执行，避免改变服务器端文件的修改时间。

## 与 parasite 的分工

Accelerator 只生成本次崩溃的文件，不在启动时重新处理历史 dmp，也不自动删除旧文件。
这样不会在新一轮运行中改动上一轮文件的时间戳，或删除等待补传的现场。

parasite 负责按运行基线收集新增的 `.dmp`、`.txt`、`.log`，等待文件稳定后打包为
`crash.zip` 并上传。当前 parasite 策略为：成功上报后本地保留 7 天，上传失败的
已关联现场保留并重试；没有运行记录认领的陈旧文件也由 parasite 清理。

没有启用 parasite 退出上报时，需要自行归档和清理 `breakpad/`，Accelerator
不再提供原先的 30 天自动清理。
