"""Web console for the openGauss-backed part of the MiniDB project."""

from __future__ import annotations

import os
import re
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterator

from flask import Flask, jsonify, render_template, request


BASE_DIR = Path(__file__).resolve().parent.parent
SCHEMA_FILE = BASE_DIR / "web" / "schema.sql"
IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
MAX_SQL_LENGTH = 20_000
MAX_RESULT_ROWS = 500
SQL_WHITESPACE = " \t\r\n\f\v"
DOLLAR_QUOTE = re.compile(r"\$(?:[A-Za-z_\u0080-\U0010ffff][A-Za-z_0-9\u0080-\U0010ffff]*)?\$")


def _config() -> dict[str, Any]:
    return {
        "host": os.getenv("DB_HOST", "127.0.0.1"),
        "port": int(os.getenv("DB_PORT", "5432")),
        "dbname": os.getenv("DB_NAME", "postgres"),
        "user": os.getenv("DB_USER", "gaussdb"),
        "password": os.getenv("DB_PASSWORD", "openGauss@123"),
        "connect_timeout": int(os.getenv("DB_CONNECT_TIMEOUT", "3")),
        "application_name": "minidb-web",
        # Metadata endpoints query public; use it for unqualified SQL as well.
        "options": "-c search_path=public -c standard_conforming_strings=on",
    }


def get_connection() -> Any:
    """Create one short-lived database connection per request."""
    try:
        import psycopg2
    except ImportError as exc:  # pragma: no cover - exercised in deployment
        raise RuntimeError("psycopg2 is not installed; run pip install -r web/requirements.txt") from exc
    return psycopg2.connect(**_config())


@contextmanager
def connection_scope() -> Iterator[Any]:
    connection = get_connection()
    try:
        yield connection
    finally:
        connection.close()


def _identifier_part(char: str) -> bool:
    return char.isalnum() or char in "_$" or ord(char) >= 128


def _continued_string_start(sql: str, index: int) -> int | None:
    """Find a quoted continuation separated by whitespace containing a newline."""
    newline = False
    while index < len(sql):
        if sql[index] in SQL_WHITESPACE:
            newline = newline or sql[index] in "\r\n"
            index += 1
        elif sql.startswith("--", index):
            index += 2
            while index < len(sql) and sql[index] not in "\r\n":
                index += 1
        else:
            break
    return index if newline and index < len(sql) and sql[index] == "'" else None


def split_sql_statements(sql: str) -> list[str]:
    """Count openGauss statements, preserving SQL and hints.

    Connections use standard_conforming_strings=on. Backslash quoting applies
    to E strings only, including their newline-separated quoted continuations.
    This detects boundaries; the database still validates SQL grammar.
    """
    statements: list[str] = []
    start = index = block_depth = 0
    state = "normal"
    escape_string = has_sql = False
    while index < len(sql):
        char = sql[index]
        if state == "line":
            if char in "\r\n":
                state = "normal"
            index += 1
            continue
        if block_depth:
            if sql.startswith("/*", index):
                block_depth += 1
                index += 2
            elif sql.startswith("*/", index):
                block_depth -= 1
                index += 2
            else:
                index += 1
            continue
        if state in {"single", "double"}:
            quote = "'" if state == "single" else '"'
            if state == "single" and escape_string and char == "\\":
                index += 2
                continue
            if char == quote:
                if index + 1 < len(sql) and sql[index + 1] == quote:
                    index += 2
                    continue
                continuation = _continued_string_start(sql, index + 1) if state == "single" else None
                if continuation is not None:
                    index = continuation + 1
                    continue
                state = "normal"
            index += 1
            continue
        if sql.startswith("--", index):
            state = "line"
            index += 2
        elif sql.startswith("/*", index):
            block_depth = 1
            index += 2
        elif char in "'\"":
            state = "single" if char == "'" else "double"
            escape_string = (index > 0 and sql[index - 1] in "eE"
                             and (index < 2 or not _identifier_part(sql[index - 2])))
            has_sql = True
            index += 1
        elif char == "$" and (index == 0 or not _identifier_part(sql[index - 1])) and (match := DOLLAR_QUOTE.match(sql, index)):
            delimiter = match.group()
            closing = sql.find(delimiter, match.end())
            if closing < 0:
                raise ValueError("unterminated dollar-quoted string")
            has_sql = True
            index = closing + len(delimiter)
        elif char == ";":
            if has_sql:
                statements.append(sql[start:index].strip())
            index += 1
            start = index
            has_sql = False
        else:
            has_sql = has_sql or char not in SQL_WHITESPACE
            index += 1
    if block_depth:
        raise ValueError("unterminated block comment")
    if state in {"single", "double"}:
        raise ValueError("unterminated string" if state == "single" else "unterminated quoted identifier")
    if has_sql:
        statements.append(sql[start:].strip())
    return statements


def _validate_sql(sql: Any) -> str:
    if not isinstance(sql, str) or not sql.strip():
        raise ValueError("sql must be a non-empty string")
    if len(sql) > MAX_SQL_LENGTH:
        raise ValueError(f"sql exceeds {MAX_SQL_LENGTH} characters")
    statements = split_sql_statements(sql)
    if len(statements) != 1:
        raise ValueError("one request may execute exactly one SQL statement")
    return statements[0]


def _json_error(message: str, status: int = 400):
    return jsonify({"ok": False, "error": message}), status


def _rows_from_cursor(cursor: Any) -> tuple[list[str], list[list[Any]]]:
    if cursor.description is None:
        return [], []
    headers = [str(column[0]) for column in cursor.description]
    rows = [list(row) for row in cursor.fetchmany(MAX_RESULT_ROWS)]
    return headers, rows


def _json_value(value: Any) -> Any:
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    return str(value)


def _result_payload(headers: list[str], rows: list[list[Any]], message: str = "") -> dict[str, Any]:
    return {
        "ok": True,
        "columns": headers,
        "rows": [[_json_value(value) for value in row] for row in rows],
        "row_count": len(rows),
        "message": message,
    }


def initialize_schema() -> None:
    statements = split_sql_statements(SCHEMA_FILE.read_text(encoding="utf-8"))
    with connection_scope() as connection:
        with connection.cursor() as cursor:
            for statement in statements:
                cursor.execute(statement)
        connection.commit()


def wait_for_database(timeout_seconds: int = 60) -> None:
    deadline = time.monotonic() + timeout_seconds
    last_error = "database is not ready"
    while time.monotonic() < deadline:
        try:
            with connection_scope() as connection:
                with connection.cursor() as cursor:
                    cursor.execute("SELECT 1")
                    cursor.fetchone()
            return
        except Exception as exc:  # pragma: no cover - timing depends on Docker
            last_error = str(exc)
            time.sleep(2)
    raise RuntimeError(f"timed out waiting for openGauss: {last_error}")


def create_app() -> Flask:
    app = Flask(__name__, template_folder="templates", static_folder="static")

    @app.get("/")
    def index():
        return render_template("index.html")

    @app.get("/api/health")
    def health():
        try:
            with connection_scope() as connection:
                with connection.cursor() as cursor:
                    cursor.execute("SELECT current_database(), current_user, version()")
                    database, user, version = cursor.fetchone()
            return jsonify({"ok": True, "database": database, "user": user, "version": version})
        except Exception as exc:
            return _json_error(f"openGauss connection failed: {exc}", 503)

    @app.get("/api/tables")
    def tables():
        sql = """
            SELECT table_name
            FROM information_schema.tables
            WHERE table_schema = 'public' AND table_type = 'BASE TABLE'
            ORDER BY table_name
        """
        try:
            with connection_scope() as connection:
                with connection.cursor() as cursor:
                    cursor.execute(sql)
                    names = [str(row[0]) for row in cursor.fetchall()]
            return jsonify({"ok": True, "tables": names})
        except Exception as exc:
            return _json_error(f"cannot load table list: {exc}", 503)

    @app.get("/api/tables/<table_name>")
    def table_info(table_name: str):
        if not IDENTIFIER.fullmatch(table_name):
            return _json_error("invalid table name")
        columns_sql = """
            SELECT column_name, data_type, is_nullable, column_default
            FROM information_schema.columns
            WHERE table_schema = 'public' AND table_name = %s
            ORDER BY ordinal_position
        """
        try:
            with connection_scope() as connection:
                with connection.cursor() as cursor:
                    cursor.execute(columns_sql, (table_name,))
                    columns = [list(row) for row in cursor.fetchall()]
                    cursor.execute(
                        "SELECT COUNT(*) FROM information_schema.tables "
                        "WHERE table_schema = 'public' AND table_name = %s",
                        (table_name,),
                    )
                    exists = cursor.fetchone()[0] == 1
            if not exists:
                return _json_error(f"table does not exist: {table_name}", 404)
            return jsonify({"ok": True, "table": table_name, "columns": columns})
        except Exception as exc:
            return _json_error(f"cannot load table schema: {exc}", 503)

    @app.post("/api/query")
    def query():
        connection = None
        try:
            body = request.get_json(silent=True)
            if not isinstance(body, dict):
                raise ValueError("JSON body must be an object")
            sql = _validate_sql(body.get("sql"))
        except ValueError as exc:
            return _json_error(str(exc))
        try:
            with connection_scope() as connection:
                with connection.cursor() as cursor:
                    cursor.execute(sql)
                    headers, rows = _rows_from_cursor(cursor)
                    affected = cursor.rowcount if cursor.rowcount >= 0 else 0
                connection.commit()
            message = f"{len(rows)} row(s) returned" if headers else f"{affected} row(s) affected"
            return jsonify(_result_payload(headers, rows, message))
        except Exception as exc:
            try:
                if connection is not None:
                    connection.rollback()
            except Exception:
                pass
            return _json_error(f"SQL execution failed: {exc}")

    return app


app = create_app()


if __name__ == "__main__":  # pragma: no cover - manual local entry point
    wait_for_database()
    initialize_schema()
    app.run(host="0.0.0.0", port=int(os.getenv("APP_PORT", "8080")), debug=False)
