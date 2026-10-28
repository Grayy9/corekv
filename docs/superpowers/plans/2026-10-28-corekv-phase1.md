# CoreKV 第一阶段实施计划

**目标：** 完成可恢复的单机 C++20 KV 引擎及可复现的测试和演示。

**架构：** WAL 先于 MemTable 更新；内存表刷入不可变 SST。重启按 SST 编号和 WAL 顺序恢复。压实以新 SST 发布作为提交点。

**技术：** C++20 标准库、CMake、Python 3 测试辅助。

**设计：** `docs/superpowers/specs/2026-10-28-corekv-design.md`

## 任务

1. **WAL 与校验**：先写记录编解码测试，验证普通记录、不完整尾部与校验失败；再实现 `src/format.cpp`、`src/wal.cpp`。
2. **SST 与索引**：先写有序读写、删除标记、损坏文件测试；再实现 `src/sst.cpp`，值从文件按偏移读取。
3. **数据库 API 与恢复**：先写跨重启 Put/Get/Delete、自动刷盘、WAL 尾部恢复测试；再实现 `src/db.cpp` 和公共头文件。
4. **压实与故障顺序**：先写多 SST 覆盖、删除不复活、压实后重启测试；再实现 `Compact()`。
5. **交付**：补 CMake、CLI、README、简单压测与 CI；运行完整测试，记录限制，提交本地 Git 历史。
