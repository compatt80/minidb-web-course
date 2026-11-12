"""SQL boundary and API regressions, independent of a running database."""

import unittest
from unittest.mock import MagicMock, patch

from web.app import create_app, split_sql_statements


class SqlBoundaryTest(unittest.TestCase):
    def one_statement(self, sql):
        self.assertEqual(split_sql_statements(sql + ';'), [sql.strip()])

    def test_double_quoted_identifier(self):
        self.one_statement('SELECT 1 AS "a;b"')

    def test_identifier_quotes_and_comment_markers(self):
        self.one_statement('SELECT 1 AS "a"";--/*b"')

    def test_untagged_dollar_string(self):
        self.one_statement("SELECT $$a;b'\"--/* $$")

    def test_tagged_dollar_string(self):
        self.one_statement('SELECT $body$a;$$;--/*\n$other$;b$body$')

    def test_dollar_tags_are_case_sensitive(self):
        self.one_statement('SELECT $Tag$a;$tag$;b$Tag$')

    def test_unicode_and_underscore_dollar_tags(self):
        for tag in ('_', 'tag_2', '中文', 'é'):
            with self.subTest(tag=tag):
                self.one_statement(f'SELECT ${tag}$a;b${tag}$')

    def test_dollars_in_identifier_are_not_quote_openers(self):
        sql = 'SELECT name$tag$ FROM students; SELECT 2;'
        self.assertEqual(split_sql_statements(sql), ['SELECT name$tag$ FROM students', 'SELECT 2'])

    def test_nested_block_comments(self):
        self.one_statement('SELECT /* outer /* inner ; */ outer ; */ 7')

    def test_comments_and_optimizer_hints_are_preserved(self):
        self.one_statement('/* prefix */ SELECT /*+ full(students) */ 1 -- ;\n')

    def test_comments_only_and_empty_statements(self):
        self.assertEqual(split_sql_statements('; /* outer /* ; */ */; -- tail ;'), [])

    def test_line_comment_ends_on_cr_or_lf(self):
        for newline in ('\r', '\n', '\r\n'):
            with self.subTest(newline=newline):
                self.assertEqual(split_sql_statements(f'SELECT 1; -- tail;{newline}SELECT 2;'),
                                 ['SELECT 1', f'-- tail;{newline}SELECT 2'])

    def test_escape_strings(self):
        for sql in (r"SELECT E'a\';b'", r"SELECT e'a\\;b'", "SELECT E'a'';b'"):
            with self.subTest(sql=sql):
                self.one_statement(sql)

    def test_plain_string_backslash_does_not_escape_quote(self):
        self.assertEqual(split_sql_statements(r"SELECT 'a\'; SELECT 2;"),
                         [r"SELECT 'a\'", 'SELECT 2'])

    def test_escape_prefix_requires_token_boundary(self):
        self.assertEqual(split_sql_statements(r"SELECT nameE'\'; SELECT 2;"),
                         [r"SELECT nameE'\'", 'SELECT 2'])

    def test_escape_string_continuation(self):
        for whitespace in ('\n', ' -- tail ;\r\n', '\t\r'):
            sql = "SELECT E'a'" + whitespace + r"'\';b'"
            with self.subTest(sql=sql):
                self.one_statement(sql)

    def test_unterminated_constructs_are_rejected(self):
        for sql in ("SELECT 'a", 'SELECT "a', 'SELECT $$a', 'SELECT $Tag$a$tag$',
                    'SELECT /* a', 'SELECT /* outer /* inner */', r"SELECT E'a\'",
                    "SELECT E'a'\n'\\'"):
            with self.subTest(sql=sql):
                with self.assertRaisesRegex(ValueError, 'unterminated'):
                    split_sql_statements(sql)

    def test_multiple_statements_after_each_construct(self):
        for first in ('SELECT 1 AS "a;b"', 'SELECT $$a;b$$', 'SELECT $t$a;b$t$',
                      'SELECT /* outer /* ; */ ; */ 7', r"SELECT E'a\';b'",
                      "SELECT E'a'\n'\\\';b'"):
            with self.subTest(sql=first):
                self.assertEqual(split_sql_statements(first + '; SELECT 2;'), [first, 'SELECT 2'])

    def test_api_passes_original_single_statement_to_database(self):
        client = create_app().test_client()
        sql = '/* prefix */ SELECT /*+ hint */ $t$a;b$t$ AS "a;b"'
        connection = MagicMock()
        cursor = connection.cursor.return_value.__enter__.return_value
        cursor.description = [('a;b',)]
        cursor.fetchmany.return_value = [('a;b',)]
        cursor.rowcount = 1
        with patch('web.app.get_connection', return_value=connection):
            response = client.post('/api/query', json={'sql': sql + '; -- tail ;'})
        self.assertEqual(response.status_code, 200, response.json)
        cursor.execute.assert_called_once_with(sql)
        connection.commit.assert_called_once()

    def test_api_rejects_before_connecting(self):
        client = create_app().test_client()
        for sql in ('SELECT $$a;b$$; SELECT 2;', 'SELECT 1 AS "a;b"; SELECT 2;',
                    'SELECT /* /* ; */ */ 1; SELECT 2;', "SELECT 'a", 'SELECT "a',
                    'SELECT $t$a', '/* only comments */ -- tail'):
            with self.subTest(sql=sql), patch('web.app.get_connection') as connect:
                response = client.post('/api/query', json={'sql': sql})
                self.assertEqual(response.status_code, 400, response.json)
                connect.assert_not_called()


if __name__ == '__main__':
    unittest.main(verbosity=2)
