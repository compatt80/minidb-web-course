"""Black-box tests: SQL behavior, restart, interrupted transactions and file safety.
Usage: python tests/integration.py path/to/minidb[.exe]
Only the Python standard library is required.
"""
import pathlib
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import contextmanager

EXE = pathlib.Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else pathlib.Path('bin/minidb.exe').resolve()


class Integration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='minidb-test-')
        self.db = pathlib.Path(self.temp.name) / 'test.db'

    def tearDown(self):
        self.temp.cleanup()

    def run_sql(self, sql, code=0, keep=False, db=None):
        args = [str(EXE), '--db', str(db or self.db)]
        if keep:
            args.append('--continue-on-error')
        r = subprocess.run(args, input=sql, text=True, encoding='utf-8', capture_output=True, timeout=15)
        self.assertEqual(r.returncode, code, r.stdout + r.stderr)
        return r.stdout, r.stderr

    def seed(self):
        self.run_sql("CREATE TABLE t (id INT PRIMARY KEY, name TEXT NOT NULL, score REAL, note TEXT UNIQUE);"
                     "INSERT INTO t VALUES (1,'张三',80,NULL),(2,'李四',90.5,'x'),(3,'王五',NULL,NULL);")

    @contextmanager
    def deny_file_read(self, path):
        """Temporarily deny reads of a test file without changing system ACLs."""
        if os.name == 'nt':
            import ctypes
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                          ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32,
                                          ctypes.c_void_p]
            kernel.CreateFileW.restype = ctypes.c_void_p
            kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            # Keep the existing file open for writing, sharing writes/deletes but not reads.
            handle = kernel.CreateFileW(str(path), 0x40000000, 2 | 4, None, 3, 0x80, None)
            if handle == ctypes.c_void_p(-1).value:
                raise ctypes.WinError(ctypes.get_last_error())
            try:
                yield
            finally:
                kernel.CloseHandle(handle)
        else:
            mode = path.stat().st_mode & 0o7777
            try:
                path.chmod(0)
                if os.access(path, os.R_OK):
                    self.skipTest('read-denial test requires a non-root account')
                yield
            finally:
                path.chmod(mode)

    def test_crud_and_restart(self):
        self.seed()
        out, _ = self.run_sql("UPDATE t SET score=95 WHERE id=1; DELETE FROM t WHERE id=2; SELECT id,score FROM t ORDER BY id;")
        self.assertIn('1\t95\n3\tNULL\n', out)
        out, _ = self.run_sql('SELECT COUNT(*) FROM t;')
        self.assertIn('count(*)\n2\n', out)

    def test_schema_alter_and_drop(self):
        self.seed()
        out, _ = self.run_sql('ALTER TABLE t ADD COLUMN extra TEXT; ALTER TABLE t RENAME COLUMN extra TO contact;'
                              'ALTER TABLE t DROP COLUMN contact; ALTER TABLE t RENAME TO students; DESCRIBE students;')
        self.assertIn('id\tINT\tYES\tYES\tYES', out)
        self.run_sql('DROP TABLE students;')
        out, _ = self.run_sql('SHOW TABLES;')
        self.assertIn('0 table(s)', out)

    def test_duplicate_primary_key_statement_atomicity(self):
        self.seed()
        out, err = self.run_sql("INSERT INTO t VALUES(4,'new',0,NULL),(1,'bad',0,NULL); SELECT COUNT(*) FROM t;", 1, True)
        self.assertIn('UNIQUE constraint: id', err)
        self.assertIn('count(*)\n3\n', out)
        out, _ = self.run_sql('SELECT COUNT(*) FROM t;')
        self.assertIn('count(*)\n3\n', out)

    def test_update_unique_atomicity(self):
        self.seed()
        out, _ = self.run_sql("UPDATE t SET note='same'; SELECT COUNT(*) FROM t WHERE note IS NULL;", 1, True)
        self.assertIn('count(*)\n2\n', out)

    def test_not_null_and_strict_types(self):
        self.seed()
        for sql in ["INSERT INTO t VALUES(4,NULL,0,NULL);", "UPDATE t SET id=1.2 WHERE id=1;",
                    "UPDATE t SET score='90';", "INSERT INTO t (id) VALUES(4);"]:
            self.run_sql(sql, 1)
        out, _ = self.run_sql('SELECT COUNT(*) FROM t;')
        self.assertIn('count(*)\n3\n', out)

    def test_unique_allows_multiple_nulls(self):
        self.seed()
        self.run_sql("INSERT INTO t VALUES(4,'n',0,NULL);")
        self.run_sql("INSERT INTO t VALUES(5,'n',0,'x');", 1)

    def test_partial_insert_and_value_count(self):
        self.seed()
        out, _ = self.run_sql("INSERT INTO t(name,id) VALUES('甲',4); SELECT id,score,note FROM t WHERE id=4;")
        self.assertIn('4\tNULL\tNULL', out)
        for sql in ["INSERT INTO t VALUES(5);", "INSERT INTO t(id,id) VALUES(5,5);", "INSERT INTO t(missing) VALUES(5);"]:
            self.run_sql(sql, 1)

    def test_null_three_valued_logic(self):
        self.seed()
        for where, count in [('score = NULL', 0), ('score <> NULL', 0), ('score IS NULL', 1),
                             ('score IS NOT NULL', 2), ('score = NULL OR id = 1', 1),
                             ('score = NULL AND id = 1', 0), ('id = NULL', 0)]:
            out, _ = self.run_sql(f'SELECT COUNT(*) FROM t WHERE {where};')
            self.assertIn(f'count(*)\n{count}\n', out, where)

    def test_and_or_precedence_and_parentheses(self):
        self.seed()
        out, _ = self.run_sql('SELECT id FROM t WHERE id=1 OR id=2 AND score<90 ORDER BY id;')
        self.assertIn('id\n1\n1 row(s)', out)
        out, _ = self.run_sql('SELECT COUNT(*) FROM t WHERE (id=1 OR id=2) AND score>=90;')
        self.assertIn('count(*)\n1\n', out)

    def test_comparison_operators(self):
        self.seed()
        for operator, count in [('=',1), ('!=',2), ('<>',2), ('<',1), ('<=',2), ('>',1), ('>=',2)]:
            out, _ = self.run_sql(f'SELECT COUNT(*) FROM t WHERE id {operator} 2;')
            self.assertIn(f'count(*)\n{count}\n', out)

    def test_sort_and_limit(self):
        self.seed()
        out, _ = self.run_sql('SELECT id FROM t ORDER BY score DESC LIMIT 2;')
        self.assertIn('id\n2\n1\n2 row(s)', out)
        out, _ = self.run_sql('SELECT id FROM t ORDER BY score ASC LIMIT 1;')
        self.assertIn('id\n3\n1 row(s)', out)
        out, _ = self.run_sql('SELECT * FROM t LIMIT 0; SELECT COUNT(*) FROM t LIMIT 0;')
        self.assertEqual(out.count('0 row(s)'), 2)
        self.run_sql('SELECT * FROM t LIMIT -1;', 1)

    def test_index_plan_and_reconstruction(self):
        self.seed()
        out, _ = self.run_sql('EXPLAIN SELECT * FROM t WHERE id=2; CREATE INDEX ix ON t(id); EXPLAIN SELECT * FROM t WHERE id=2;')
        self.assertIn('TableScan\t3\t3', out)
        self.assertIn('IndexLookup(ix)\t1\t3', out)
        out, _ = self.run_sql('SELECT * FROM t WHERE id=2;')
        self.assertIn('scanned 1, plan IndexLookup(ix)', out)
        self.run_sql("UPDATE t SET id=4 WHERE id=2; DELETE FROM t WHERE id=1;")
        out, _ = self.run_sql('SELECT id FROM t WHERE id=4; SELECT id FROM t WHERE id=2;')
        self.assertIn('id\n4\n1 row(s)', out)
        self.assertIn('0 row(s), scanned 0', out)

    def test_index_or_fallback_and_and_filter(self):
        self.seed()
        self.run_sql('CREATE INDEX ix ON t(id);')
        out, _ = self.run_sql('SELECT id FROM t WHERE id=1 OR id=3 ORDER BY id;')
        self.assertIn('id\n1\n3\n', out)
        self.assertIn('plan TableScan', out)
        out, _ = self.run_sql('SELECT id FROM t WHERE id=1 AND score>90;')
        self.assertIn('0 row(s), scanned 1, plan IndexLookup(ix)', out)

    def test_group_by_count_and_inner_join(self):
        self.run_sql("CREATE TABLE students(id INT PRIMARY KEY, name TEXT, class TEXT);"
                     "CREATE TABLE scores(student_id INT, score INT);"
                     "INSERT INTO students VALUES(1,'张三','计241'),(2,'李四','计241'),(3,'王五','计242');"
                     "INSERT INTO scores VALUES(1,90),(2,80),(3,95);")
        out, _ = self.run_sql('SELECT class, COUNT(*) FROM students GROUP BY class ORDER BY class;')
        self.assertIn('class\tcount(*)\n计241\t2\n计242\t1\n', out)
        out, _ = self.run_sql('SELECT students.name, scores.score FROM students JOIN scores '
                              'ON students.id = scores.student_id ORDER BY students.id;')
        self.assertIn('students.name\tscores.score\n张三\t90\n李四\t80\n王五\t95\n', out)
        out, _ = self.run_sql('EXPLAIN SELECT students.name FROM students JOIN scores '
                              'ON students.id = scores.student_id;')
        self.assertIn('NestedLoopJoin\t3\t3', out)
        out, _ = self.run_sql('SELECT students.name FROM students INNER JOIN scores '
                              'ON students.id = scores.student_id ORDER BY students.id;')
        self.assertIn('students.name\n张三\n李四\n王五\n', out)
        out, _ = self.run_sql('SELECT students.name FROM students INNER JOIN scores '
                              'ON scores.student_id = students.id ORDER BY students.id;')
        self.assertIn('students.name\n张三\n李四\n王五\n', out)
        self.run_sql('SELECT * FROM students INNER JOIN scores ON bogus.id = scores.student_id;', 1)

    def test_multiple_indexes_choose_fewer_candidates(self):
        self.seed()
        out, _ = self.run_sql("CREATE INDEX ix_id ON t(id); CREATE INDEX ix_name ON t(name);"
                              "EXPLAIN SELECT * FROM t WHERE name='张三' AND id=999;")
        self.assertIn('IndexLookup(ix_id)\t0\t3', out)

    def test_index_schema_dependency(self):
        self.seed()
        self.run_sql('CREATE INDEX ix ON t(id);')
        self.run_sql('ALTER TABLE t DROP COLUMN id;', 1)
        out, _ = self.run_sql('ALTER TABLE t RENAME COLUMN id TO key_id; EXPLAIN SELECT * FROM t WHERE key_id=2;')
        self.assertIn('IndexLookup(ix)\t1\t3', out)
        self.run_sql('DROP INDEX ix; ALTER TABLE t DROP COLUMN key_id;')

    def test_transactions_commit_rollback_and_ddl(self):
        self.seed()
        original = self.db.read_bytes()
        self.run_sql("BEGIN; UPDATE t SET score=0; CREATE TABLE scratch(id INT); ROLLBACK;")
        self.assertEqual(self.db.read_bytes(), original)
        out, _ = self.run_sql('SHOW TABLES; SELECT score FROM t WHERE id=1;')
        self.assertIn('1 table(s)', out)
        self.assertIn('score\n80\n', out)
        self.run_sql('BEGIN; UPDATE t SET score=100 WHERE id=1; COMMIT;')
        out, _ = self.run_sql('SELECT score FROM t WHERE id=1;')
        self.assertIn('score\n100\n', out)

    def test_failed_statement_inside_transaction(self):
        self.seed()
        out, _ = self.run_sql("BEGIN; UPDATE t SET score=99 WHERE id=1; INSERT INTO t VALUES(1,'bad',0,NULL);"
                              "COMMIT; SELECT score FROM t WHERE id=1;", 1, True)
        self.assertIn('Transaction committed', out)
        self.assertIn('score\n99\n', out)

    def test_exit_discards_transaction(self):
        self.seed()
        before = self.db.read_bytes()
        _, err = self.run_sql('BEGIN; DELETE FROM t; QUIT;')
        self.assertIn('uncommitted transaction discarded', err)
        self.assertEqual(self.db.read_bytes(), before)

    def test_killed_process_discards_transaction(self):
        self.seed()
        before = self.db.read_bytes()
        p = subprocess.Popen([str(EXE),'--db',str(self.db)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE,text=True,encoding='utf-8')
        try:
            p.stdin.write('BEGIN;\nDELETE FROM t;\n')
            p.stdin.flush()
            self.assertEqual(p.stdout.readline().strip(), 'Transaction started (exclusive lock)')
            self.assertEqual(p.stdout.readline().strip(), '3 row(s) deleted')
            p.kill()
            p.communicate(timeout=5)
        finally:
            if p.poll() is None:
                p.kill()
                p.communicate(timeout=5)
        self.assertEqual(self.db.read_bytes(), before)
        out, _ = self.run_sql('SELECT COUNT(*) FROM t;')
        self.assertIn('count(*)\n3\n', out)

    def test_wal_recovers_after_crash_between_prepare_and_replace(self):
        self.seed()
        wal = pathlib.Path(str(self.db) + '.wal')
        env = os.environ.copy()
        env['MINIDB_TEST_CRASH_AFTER_WAL'] = '1'
        r = subprocess.run([str(EXE), '--db', str(self.db)],
                           input="INSERT INTO t VALUES(4,'赵六',85,NULL);",
                           text=True, encoding='utf-8', capture_output=True,
                           timeout=5, env=env)
        self.assertNotEqual(r.returncode, 0)
        self.assertTrue(wal.exists(), 'the durable WAL should survive the simulated crash')
        out, _ = self.run_sql('SELECT COUNT(*) FROM t;')
        self.assertIn('count(*)\n4\n', out)
        self.assertFalse(wal.exists(), 'recovery should remove the applied WAL')

    def test_invalid_transaction_state(self):
        for sql in ['COMMIT;', 'ROLLBACK;', 'BEGIN; BEGIN;']:
            self.run_sql(sql, 1)

    def test_schema_failure_is_atomic(self):
        self.seed()
        for sql in ['ALTER TABLE t ADD COLUMN required INT NOT NULL;', 'ALTER TABLE t ADD COLUMN id TEXT;',
                    'ALTER TABLE t RENAME COLUMN name TO id;', 'CREATE TABLE bad(id INT PRIMARY KEY, x INT PRIMARY KEY);']:
            self.run_sql(sql, 1)
        self.run_sql('CREATE TABLE single(x INT);')
        self.run_sql('ALTER TABLE single DROP COLUMN x;', 1)
        out, _ = self.run_sql('DESCRIBE t;')
        self.assertNotIn('required\t', out)
        self.assertIn('name\tTEXT', out)

    def test_empty_table_still_checks_columns_and_types(self):
        self.run_sql('CREATE TABLE t(id INT);')
        for sql in ['SELECT missing FROM t;', 'SELECT * FROM t WHERE missing=1;',
                    "SELECT * FROM t WHERE id='x';", 'SELECT * FROM t ORDER BY missing;',
                    'UPDATE t SET missing=1;', 'DELETE FROM t WHERE missing=1;']:
            self.run_sql(sql, 1)

    def test_strings_comments_case_and_bom_script(self):
        sql = "-- ; ignored\nCrEaTe TABLE T (id INT, text TEXT);\nINSERT INTO T VALUES (-1, '甲;乙 -- ''引号'''); SELECT text FROM t;"
        out, _ = self.run_sql(sql)
        self.assertIn("甲;乙 -- '引号'", out)
        script = pathlib.Path(self.temp.name) / 'utf8.sql'
        script.write_text('SELECT COUNT(*) FROM t;',encoding='utf-8-sig')
        r = subprocess.run([str(EXE),'--db',str(self.db),'--script',str(script)],capture_output=True,text=True,encoding='utf-8',timeout=5)
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn('count(*)\n1\n',r.stdout)

    def test_parse_errors_and_numeric_overflow(self):
        self.run_sql('CREATE TABLE t(id INT);')
        for sql in ["INSERT INTO t VALUES ('unterminated);", 'INSERT INTO t VALUES (9223372036854775808);',
                    'SELECT * FROM t unexpected;', 'SELECT * FROM t WHERE id ! 1;', 'SELECT @ FROM t;',
                    'CREATE TABLE bad(id VARCHAR);', 'INSERT INTO t VALUES (1e3);']:
            self.run_sql(sql,1)
        out, _ = self.run_sql('SELECT COUNT(*) FROM t;')
        self.assertIn('count(*)\n0\n',out)

    def test_integer_boundaries_and_real_coercion(self):
        self.run_sql('CREATE TABLE t(i INT, r REAL); INSERT INTO t VALUES (-9223372036854775808,1),(9223372036854775807,-.5);')
        out, _ = self.run_sql('SELECT * FROM t ORDER BY i;')
        self.assertIn('-9223372036854775808\t1',out)
        self.assertIn('9223372036854775807\t-0.5',out)

    def test_corrupt_file_is_rejected_without_overwrite(self):
        self.seed()
        data = bytearray(self.db.read_bytes())
        data[20] ^= 0x80
        self.db.write_bytes(data)
        _, err = self.run_sql('SHOW TABLES;',2)
        self.assertIn('checksum mismatch',err)
        self.assertEqual(self.db.read_bytes(),bytes(data))

    def test_truncated_file_is_rejected(self):
        self.db.write_bytes(b'MINIDB01')
        self.run_sql('SHOW TABLES;',2)
        self.assertEqual(self.db.read_bytes(),b'MINIDB01')

    def test_write_failure_restores_memory_and_disk(self):
        self.seed()
        before = self.db.read_bytes()
        pathlib.Path(str(self.db)+'.tmp').mkdir()
        out, err = self.run_sql('DELETE FROM t; SELECT COUNT(*) FROM t;',1,True)
        self.assertIn('Cannot write database temporary file',err)
        self.assertIn('count(*)\n3\n',out)
        self.assertEqual(self.db.read_bytes(),before)

    def test_idle_sessions_do_not_lock_database(self):
        p = subprocess.Popen([str(EXE),'--db',str(self.db)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE,text=True,encoding='utf-8')
        try:
            p.stdin.write('HELP;\n')
            p.stdin.flush()
            self.assertTrue(p.stdout.readline().startswith('CREATE/DROP/ALTER'))
            out, _ = self.run_sql('SHOW TABLES;')
            self.assertIn('0 table(s)',out)
        finally:
            p.communicate('QUIT;\n',timeout=5)
        self.run_sql('SHOW TABLES;')

    def test_batch_fail_fast_and_continue(self):
        out,_ = self.run_sql('UNKNOWN; CREATE TABLE t(id INT);',1)
        self.assertNotIn('Table created',out)
        out,_ = self.run_sql('UNKNOWN; CREATE TABLE t(id INT);',1,True)
        self.assertIn('Table created: t',out)

    def test_large_index_candidates_not_timing_claim(self):
        self.run_sql('CREATE TABLE t(id INT PRIMARY KEY, name TEXT);')
        values = ','.join(f"({i},'row{i}')" for i in range(1000))
        out,_ = self.run_sql(f'INSERT INTO t VALUES {values}; EXPLAIN SELECT * FROM t WHERE id=777;'
                             'CREATE INDEX ix ON t(id); EXPLAIN SELECT * FROM t WHERE id=777; SELECT id FROM t WHERE id=777;')
        self.assertIn('TableScan\t1000\t1000',out)
        self.assertIn('IndexLookup(ix)\t1\t1000',out)
        self.assertIn('id\n777\n1 row(s)',out)
        out,_ = self.run_sql('SELECT id FROM t WHERE id=64;')
        self.assertIn('id\n64\n1 row(s), scanned 1, plan IndexLookup(ix)',out)
        out,_ = self.run_sql('SELECT id FROM t WHERE id=64;')
        self.assertIn('id\n64\n1 row(s), scanned 1, plan IndexLookup(ix)',out)

    def test_group_projection_and_order_validation(self):
        self.run_sql("CREATE TABLE t(g TEXT, score INT); INSERT INTO t VALUES('a',90),('b',10),('b',20);")
        out,_ = self.run_sql('SELECT COUNT(*),g FROM t GROUP BY g ORDER BY g;')
        self.assertIn('count(*)\tg\n1\ta\n2\tb\n',out)
        out,_ = self.run_sql('SELECT COUNT(*),COUNT(*) FROM t;')
        self.assertIn('count(*)\tcount(*)\n3\t3\n',out)
        self.run_sql('SELECT g,COUNT(*) FROM t GROUP BY g ORDER BY score;', 1)

    def test_empty_existing_database_is_rejected(self):
        self.db.write_bytes(b'')
        out, err = self.run_sql('SHOW TABLES;', 2)
        self.assertIn('empty file', err)

    def test_unreadable_existing_database_is_rejected(self):
        self.seed()
        before = self.db.read_bytes()
        with self.deny_file_read(self.db):
            out, err = self.run_sql('SHOW TABLES; CREATE TABLE replacement(id INT);', 2)
            self.assertEqual(out, '')
            self.assertIn('Cannot read database file', err)
        self.assertEqual(self.db.read_bytes(), before)
        out, _ = self.run_sql('SELECT COUNT(*) FROM t;')
        self.assertIn('count(*)\n3\n', out)

    def test_unreadable_wal_is_not_ignored(self):
        self.seed()
        before = self.db.read_bytes()
        wal = pathlib.Path(str(self.db) + '.wal')
        wal.write_bytes(before)
        with self.deny_file_read(wal):
            out, err = self.run_sql('SHOW TABLES;', 2)
            self.assertEqual(out, '')
            self.assertIn('Cannot read database file', err)
        self.assertEqual(self.db.read_bytes(), before)
        self.assertEqual(wal.read_bytes(), before)
        out, _ = self.run_sql('SELECT COUNT(*) FROM t;')
        self.assertIn('count(*)\n3\n', out)
        self.assertFalse(wal.exists())

    def test_join_mixed_numeric_types_and_nulls(self):
        self.run_sql('CREATE TABLE ai(id INT); CREATE TABLE br(id REAL);'
                     'INSERT INTO ai VALUES(1),(2),(-3),(NULL);'
                     'INSERT INTO br VALUES(1.0),(1.5),(-3.0),(NULL);')
        for query in ['SELECT COUNT(*) FROM ai JOIN br ON ai.id=br.id;',
                      'SELECT COUNT(*) FROM ai JOIN br ON br.id=ai.id;',
                      'SELECT COUNT(*) FROM br JOIN ai ON br.id=ai.id;']:
            out, _ = self.run_sql(query)
            self.assertIn('count(*)\n2\n', out)

    def test_join_mixed_numeric_precision_and_boundaries(self):
        self.run_sql('CREATE TABLE ai(id INT); CREATE TABLE br(id REAL);'
                     'INSERT INTO ai VALUES(9007199254740992),(9007199254740993),'
                     '(9223372036854775807),(-9223372036854775808),(-9007199254740993);'
                     'INSERT INTO br VALUES(9007199254740992.0),(9223372036854775808.0),'
                     '(-9223372036854775808.0),(-9007199254740992.0),(-9223372036854777856.0);')
        for on in ['ai.id=br.id', 'br.id=ai.id']:
            out, _ = self.run_sql('SELECT ai.id FROM ai JOIN br ON ' + on + ' ORDER BY ai.id;')
            self.assertIn('ai.id\n-9223372036854775808\n9007199254740992\n2 row(s)', out)

    def test_join_incompatible_types_are_rejected_even_when_empty(self):
        self.run_sql('CREATE TABLE ai(id INT); CREATE TABLE bt(id TEXT);')
        for on in ['ai.id=bt.id', 'bt.id=ai.id']:
            _, err = self.run_sql('SELECT * FROM ai JOIN bt ON ' + on + ';', 1)
            self.assertIn('Incompatible JOIN column types', err)


if __name__ == '__main__':
    unittest.main(verbosity=2)
