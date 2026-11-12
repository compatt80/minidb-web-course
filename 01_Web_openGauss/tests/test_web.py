import unittest
from unittest.mock import patch

from web.app import create_app, split_sql_statements


class FakeCursor:
    def __init__(self):
        self.description = None
        self.rowcount = 0
        self._rows = []

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def execute(self, sql, params=None):
        del params
        normalized = " ".join(sql.split()).lower()
        if normalized.startswith("select current_database"):
            self.description = [("current_database",), ("current_user",), ("version",)]
            self._rows = [("postgres", "gaussdb", "openGauss test")]
        elif normalized.startswith("select table_name"):
            self.description = [("table_name",)]
            self._rows = [("students",)]
        elif normalized.startswith("select column_name"):
            self.description = [("column_name",), ("data_type",), ("is_nullable",), ("column_default",)]
            self._rows = [("id", "integer", "NO", None)]
        elif normalized.startswith("select count(*) from information_schema"):
            self.description = [("count",)]
            self._rows = [(1,)]
        elif normalized.startswith("select id"):
            self.description = [("id",), ("name",)]
            self._rows = [(1, "张三"), (2, "李四")]
        elif normalized.startswith("update"):
            self.description = None
            self.rowcount = 2
        else:
            raise RuntimeError("unexpected SQL in fake cursor: " + sql)

    def fetchone(self):
        return self._rows[0]

    def fetchall(self):
        return self._rows

    def fetchmany(self, _size):
        return self._rows


class FakeConnection:
    def __init__(self):
        self.cursor_instance = FakeCursor()
        self.committed = False

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def cursor(self):
        return self.cursor_instance

    def commit(self):
        self.committed = True

    def rollback(self):
        pass

    def close(self):
        pass


class WebApiTest(unittest.TestCase):
    def setUp(self):
        self.app = create_app()
        self.app.testing = True
        self.connection = FakeConnection()
        self.client = self.app.test_client()

    def test_statement_splitter_preserves_quoted_semicolon(self):
        statements = split_sql_statements("SELECT 'a;b'; -- ;\n SELECT /* ; */ 2;")
        self.assertEqual(statements, ["SELECT 'a;b'", "-- ;\n SELECT /* ; */ 2"])

    def test_statement_splitter_accepts_trailing_and_block_comments(self):
        statements = split_sql_statements("SELECT 1; -- trailing comment\n")
        self.assertEqual(statements, ["SELECT 1"])

    def test_health_and_table_routes(self):
        with patch("web.app.get_connection", return_value=self.connection):
            health = self.client.get("/api/health")
            tables = self.client.get("/api/tables")
        self.assertEqual(health.status_code, 200)
        self.assertEqual(health.json["user"], "gaussdb")
        self.assertEqual(tables.json["tables"], ["students"])

    def test_index_page_is_available(self):
        response = self.client.get("/")
        self.assertEqual(response.status_code, 200)
        self.assertIn("SQL Console", response.get_data(as_text=True))

    def test_query_returns_columns_and_rows(self):
        with patch("web.app.get_connection", return_value=self.connection):
            response = self.client.post("/api/query", json={"sql": "SELECT id, name FROM students;"})
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json["columns"], ["id", "name"])
        self.assertEqual(response.json["rows"][0], [1, "张三"])
        self.assertTrue(self.connection.committed)

    def test_query_rejects_multiple_statements(self):
        response = self.client.post("/api/query", json={"sql": "SELECT 1; DROP TABLE students;"})
        self.assertEqual(response.status_code, 400)
        self.assertIn("exactly one", response.json["error"])

    def test_query_rejects_non_object_json(self):
        for body in ([1], "text", 1):
            response = self.client.post("/api/query", json=body)
            self.assertEqual(response.status_code, 400)
            self.assertEqual(response.content_type, "application/json")
            self.assertIn("JSON body must be an object", response.json["error"])

    def test_query_returns_database_error_without_masking_it(self):
        with patch("web.app.get_connection", return_value=self.connection):
            response = self.client.post("/api/query", json={"sql": "SELECT unsupported FROM students;"})
        self.assertEqual(response.status_code, 400)
        self.assertIn("SQL execution failed", response.json["error"])

    def test_invalid_identifier_is_rejected(self):
        response = self.client.get("/api/tables/students%3Bdrop")
        self.assertEqual(response.status_code, 400)


if __name__ == "__main__":
    unittest.main(verbosity=2)
