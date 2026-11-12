# 基于 AI 智能体辅助软件工程的简易 DBMS MiniDB

这是独立运行的 C++17 教学型关系数据库。它通过命令行读取 SQL，在自己的 `.db` / `.wal` 文件中存储数据。该目录不包含 Flask Web 或 openGauss 部署服务。

## 构建与运行

Windows x64 且已安装 g++：

```powershell
.\build.ps1
.\bin\minidb.exe --db data/demo.db
```

也可以直接运行 `run.cmd`，或用 `run_demo.cmd` 执行演示。输入 SQL 以分号结束。常用语句：

```sql
CREATE TABLE students (id INT PRIMARY KEY, name TEXT NOT NULL, score REAL);
INSERT INTO students VALUES (1, '张三', 88.5);
SELECT * FROM students ORDER BY score DESC LIMIT 10;
BEGIN;
UPDATE students SET score = 90 WHERE id = 1;
ROLLBACK;
```

## 实现结构

`src/sql.cpp` 负责 SQL 词法分析与解析；`src/engine.cpp` 执行语句；`src/model.cpp` 定义数据库、表、列和记录；`src/storage.cpp` 负责序列化、校验和及 WAL 快照恢复；`src/bptree.cpp` 和 `src/page_buffer.cpp` 实现内存索引与分页缓冲；`src/lock_manager.cpp` 实现跨进程数据库级 S/X 锁。

## 能力与边界

支持建表、删表、改表、CRUD、主键/唯一/非空、条件查询、排序、LIMIT、COUNT、GROUP BY、单次两表等值 JOIN、索引、EXPLAIN、事务、持久化及恢复。SQL 是教学子集，不实现完整 SQL 标准；C++ 内核未实现外键、复合唯一约束、多表级联或任意数量 JOIN。数据以整库快照持久化，B+ 树是内存索引。

## 验证

已有原集成检查 40/40、多进程并发检查 17/17；B+ 树与分页缓冲池独立检查 2/2；八实体 SQL 子集检查 19/19。验证过程不会把 openGauss 的外键能力归为 C++ 引擎能力。原始日志与 JSON 位于 evidence。

完整源码可用 `cmake -S . -B build`、`cmake --build build --config Release` 构建；Linux/macOS 需要相应 C++17 工具链。
