# Web 应用与 openGauss

本目录只包含 Flask Web 控制台和 openGauss 服务。浏览器请求由 Flask 处理，再通过 psycopg2 连接 openGauss；数据库本身不由本目录中的 C++ MiniDB 提供。

## 启动

需要 Docker Desktop、Docker Compose。首次启动：

```powershell
Copy-Item .env.example .env
docker compose up --build -d
```

访问 http://127.0.0.1:8080 。停止服务并保留数据库卷：`docker compose down`。谨慎删除数据库卷：`docker compose down -v`。

默认 schema 是 `web/schema.sql` 的学生示例。八实体 Web 验证 SQL 位于 `schema_multi.sql`，查询示例在 `demo_queries.sql`；原页面的示例快捷按钮使用 students.class_name，切换八实体 schema 后应手动粘贴示例查询。

## Web 范围

Flask 页面与 API 支持健康检查、表清单、列定义、单条 SQL 执行和结果显示。SQL 语句边界检查拒绝一次请求携带多条语句；SQL 解析和执行由 openGauss 完成。Web 数据保存在 openGauss 命名卷。

测试记录：八实体 Web 首轮 45/45、容器重建后 44/44、Web API 回归 28/28；完整 JSON 和日志在 evidence。
