# CoreKV

一个用于学习存储引擎内部机制的 C++20 嵌入式 KV 项目。提供 `Put`、`Get`、`Delete`、`WriteBatch`、`Scan`、`Flush`、`Compact`；适合围绕 WAL、崩溃恢复、SST、范围读取和压实讨论设计取舍。它是单机教学实现，不是 RocksDB/LevelDB 的替代品。

## 数据路径

```text
Put/Delete/WriteBatch ──> WAL（每条记录同步）──> MemTable（std::map）
                                        │ 达到阈值 / 手动 Flush
                                        ▼
                                 不可变有序 SST
                                        │ 达到数量阈值 / 手动 Compact
                                        ▼
                                   合并后的 SST
```

读取先查 MemTable，再从新到旧查 SST。SST 打开时会校验并扫描全部记录，为每个键建立内存中的文件偏移索引；命中后通过已打开的 SST 文件句柄读取值。相同键的新版本覆盖旧版本，删除标记保留到压实文件，避免旧值复活。默认在刷盘使 SST 达到 4 个时同步压实；`Options::compaction_trigger` 可调整阈值，设为 0 则关闭自动压实。重启打开数据库本身不触发压实。

`Scan(begin, end, limit)` 按键升序返回可见键值对。范围是 `[begin, end)`；空 `end` 表示无上界，`limit` 限制返回行数。扫描逐键合并 MemTable 与各 SST 索引，选最新版本并跳过删除标记；不会预先加载所有 SST 的值。多个 SST 较多时，逐键比较及按需打开文件仍有开销。

WAL 和 SST 的单项记录格式均为 `4 字节大端记录长度 | 4 字节 CRC32 | 操作码(1 字节) | 键长(4 字节) | 值长(4 字节) | 键 | 值`，操作码 `1` 为 Put、`2` 为 Delete。WAL 还支持操作码 `3` 的批量记录：记录体为 `3 | 项数(4 字节) | 重复的单项操作码、键长、值长、键、值`，整批共用一个长度和 CRC32 记录头。SST 文件另有 `SST1` 文件头，仍只存单项记录。键最大 64 KiB、值最大 1 MiB，支持二进制内容；空键拒绝。批次最多 1024 项，记录体最多 4 MiB。磁盘格式首版没有版本迁移机制。

写入先追加并同步 WAL，再修改 MemTable。刷盘先将有序记录写入临时 SST、同步并重命名发布，之后清空 WAL。重启按 SST 编号装载，再重放 WAL；WAL 不完整尾部会截断，完整记录校验失败会报错。压实先发布新 SST，再删除旧文件。两次操作都保留删除标记，所以中断后新旧 SST 同时存在时不会让旧值复活。

`WriteBatch` 按传入顺序执行 Put/Delete，同一键以最后一项为准；空批次无操作。整个批次先校验，再作为一条 WAL 记录写入并同步；只有完整且校验正确的记录会在恢复时应用。一个 DB 对象内的读取与写入由互斥锁串行化，所以读取不会看到批次的中间状态。
如果已开始写入 WAL 后发生 I/O 或内存错误，当前 DB 对象会拒绝后续读写；关闭并重开后按 WAL 恢复。无效键值或超限批次在写入前拒绝，不会使 DB 对象进入此状态。

## 构建与测试

需要 C++20 编译器、CMake 3.16+、Python 3。Windows 和 Linux 均在 CI 配置中。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

测试覆盖跨重启读写、二进制键值、范围扫描的覆盖/删除/边界与结果限制、手动压实、目录独占锁、WAL 不完整尾部、WAL/SST 校验失败、批次完整/截断恢复，以及写入返回后进程不执行析构函数直接退出的恢复场景。

## 使用

```bash
./build/corekv_cli data put player:1 online
./build/corekv_cli data get player:1
./build/corekv_cli data delete player:1
./build/corekv_cli data scan 'player:' 'player;' 20
./build/corekv_cli data flush
./build/corekv_cli data compact
```

`get` 找不到键时返回码为 3。`scan` 的命令行结果上限为 1,000,000 行。命令行工具按 `键<TAB>值` 输出，用于文本演示；二进制键值应通过 C++ API 调用。Windows 多配置构建的可执行文件通常位于 `build/Release/`。

批量写入使用 C++ API：

```cpp
#include "corekv/db.hpp"

auto db = corekv::DB::Open("data");
db->WriteBatch({{"player:1", "online"}, {"player:2", "online"},
                {"expired:1", std::nullopt}});
```

## 基准样例

在**新目录**上运行：

```bash
./build/corekv_bench bench-single 1000
./build/corekv_bench bench-batch 1000 64
```

第三个参数是可选批大小，范围为 1–1024；省略时使用单条 `Put`，指定大于 1 时每批执行一次 `WriteBatch` 和 WAL 同步。末尾不足一批的键仍会写入。脚本每个键写入 128 字节值，刷盘后关闭、重开数据库，再读取同样数量的键并校验。JSON 报告批大小、已完成写入/读取数量和各自吞吐；每个实验必须使用新目录。测试包含本机文件系统、系统缓存和进程调度影响；少量样本只用于观察趋势，不应与其他引擎的默认基准数字直接比较。

## 当前边界

- 单写者；同一目录不能被两个进程同时打开。一个 DB 对象的 API 调用由互斥锁串行化。
- 自动压实在写入或手动刷盘的调用线程内全量合并，因此触发压实的写入会有明显延迟；`Compact()` 仍可手动触发。无后台压实、Bloom filter、压缩、通用事务、复制或分布式服务。`WriteBatch` 只保证单批写入的恢复与可见性原子性。
- SST 启动时扫描所有数据建立每键偏移索引，打开时间与索引内存随数据量增长；每个 SST 保持一个文件句柄，SST 数量增多时会占用更多句柄。尚未做缓存与性能调优。
- 已返回写入在进程崩溃后可重放。断电场景还涉及文件系统的目录元数据持久化，本项目尚未实现跨平台的目录同步，因此不声明完整断电持久性。
