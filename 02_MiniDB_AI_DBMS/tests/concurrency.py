"""Real multi-process S/X locking tests. Only Python's standard library is used.
Usage: python tests/concurrency.py path/to/minidb[.exe]
"""
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import unittest
from concurrent.futures import ThreadPoolExecutor

EXE = Path(sys.argv.pop(1)).resolve()


class Session:
    def __init__(self, case, db, timeout=3000):
        self.case = case
        self.p = subprocess.Popen([str(EXE), '--db', str(db), '--lock-timeout-ms', str(timeout),
                                   '--continue-on-error'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True, encoding='utf-8', bufsize=1)
        self.out, self.err = queue.Queue(), queue.Queue()
        self.threads = []
        for stream, target in ((self.p.stdout, self.out), (self.p.stderr, self.err)):
            thread = threading.Thread(target=self.pump, args=(stream, target), daemon=True)
            thread.start()
            self.threads.append(thread)
        case.addCleanup(self.close)
        self.request('HELP;', 'Types: INT')

    @staticmethod
    def pump(stream, target):
        for line in stream:
            target.put(line)
        target.put(None)

    def send(self, sql):
        self.p.stdin.write(sql + '\n')
        self.p.stdin.flush()

    def until(self, token, errors=False):
        target = self.err if errors else self.out
        lines = []
        while True:
            try:
                line = target.get(timeout=8)
            except queue.Empty:
                raise AssertionError(f'timed out waiting for {token}: {lines}')
            if line is None:
                raise AssertionError(f'process exited while waiting for {token}: {lines}')
            lines.append(line)
            if token in line:
                return ''.join(lines)

    def request(self, sql, token):
        self.send(sql)
        return self.until(token)

    def blocked(self):
        # Output queues were drained through the previous statement's trailer.
        # The holder stays locked until the assertion, so completion is impossible.
        with self.case.assertRaises(queue.Empty):
            self.out.get(timeout=0.2)
        self.case.assertIsNone(self.p.poll())

    def close(self):
        if self.p.stdin and not self.p.stdin.closed:
            try:
                self.p.stdin.close()
            except OSError:
                pass
        try:
            self.p.wait(timeout=8)
        except subprocess.TimeoutExpired:
            self.p.kill()
            self.p.wait(timeout=5)
        for thread in self.threads:
            thread.join(timeout=2)
        self.p.stdout.close()
        self.p.stderr.close()


class Concurrency(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='minidb-concurrency-')
        self.addCleanup(self.temp.cleanup)
        self.db = Path(self.temp.name) / 'test.db'
        self.run_sql('CREATE TABLE t(id INT PRIMARY KEY, score INT); INSERT INTO t VALUES(1,10);')

    def run_sql(self, sql, code=0, timeout=3000, db=None):
        p = subprocess.run([str(EXE), '--db', str(db or self.db), '--lock-timeout-ms', str(timeout),
                            '--continue-on-error'], input=sql, text=True, encoding='utf-8',
                           capture_output=True, timeout=12)
        self.assertEqual(p.returncode, code, p.stdout + p.stderr)
        return p.stdout, p.stderr

    def session(self, timeout=3000):
        return Session(self, self.db, timeout)

    def test_two_read_only_transactions_share_lock(self):
        a, b = self.session(), self.session()
        a.request('BEGIN READ ONLY;', 'shared lock')
        b.request('BEGIN TRANSACTION READ ONLY;', 'shared lock')
        for session in (a, b):
            self.assertIn('score\n10\n', session.request('SELECT score FROM t;', '1 row(s)'))
        _, err = self.run_sql('UPDATE t SET score=99;', 1, timeout=0)
        self.assertIn('lock timeout', err)
        a.request('COMMIT;', 'committed')
        b.request('ROLLBACK;', 'rolled back')

    def test_writer_waits_until_all_shared_holders_finish(self):
        a, b, writer = self.session(), self.session(), self.session()
        a.request('BEGIN READ ONLY;', 'shared lock')
        b.request('BEGIN READ ONLY;', 'shared lock')
        writer.send('BEGIN;')
        writer.blocked()
        a.request('COMMIT;', 'committed')
        writer.blocked()
        self.assertIn('score\n10\n', b.request('SELECT score FROM t;', '1 row(s)'))
        b.request('COMMIT;', 'committed')
        writer.until('exclusive lock')
        writer.request('ROLLBACK;', 'rolled back')

    def test_writer_prevents_dirty_read_then_commit_visible(self):
        writer = self.session()
        writer.request('BEGIN READ WRITE;', 'exclusive lock')
        writer.request('UPDATE t SET score=99;', 'updated')
        reader = self.session()  # Opening while busy must not reject the session.
        reader.send('SELECT score FROM t;')
        reader.blocked()
        writer.request('COMMIT;', 'committed')
        self.assertIn('score\n99\n', reader.until('1 row(s)'))

    def test_rollback_releases_writer_without_exposing_changes(self):
        writer, reader = self.session(), self.session()
        writer.request('BEGIN;', 'exclusive lock')
        writer.request('UPDATE t SET score=99;', 'updated')
        reader.send('SELECT score FROM t;')
        reader.blocked()
        writer.request('ROLLBACK;', 'rolled back')
        self.assertIn('score\n10\n', reader.until('1 row(s)'))

    def test_serialized_read_modify_write_prevents_lost_update(self):
        a, b = self.session(), self.session()
        a.request('BEGIN;', 'exclusive lock')
        self.assertIn('score\n10\n', a.request('SELECT score FROM t;', '1 row(s)'))
        b.send('BEGIN;')
        b.blocked()
        a.request('UPDATE t SET score=11;', 'updated')
        a.request('COMMIT;', 'committed')
        b.until('exclusive lock')
        self.assertIn('score\n11\n', b.request('SELECT score FROM t;', '1 row(s)'))
        b.request('UPDATE t SET score=12;', 'updated')
        b.request('COMMIT;', 'committed')
        out, _ = self.run_sql('SELECT score FROM t;')
        self.assertIn('score\n12\n', out)

    def test_idle_session_reloads_after_another_commit(self):
        idle = self.session()
        self.run_sql('UPDATE t SET score=77;')
        self.assertIn('score\n77\n', idle.request('SELECT score FROM t;', '1 row(s)'))
        idle.request('INSERT INTO t VALUES(2,20);', 'inserted')
        out, _ = self.run_sql('SELECT id,score FROM t ORDER BY id;')
        self.assertIn('1\t77\n2\t20\n', out)

    def test_parallel_autocommit_inserts_preserve_all_rows(self):
        start = threading.Barrier(4)
        def insert_batch(worker):
            sql = ''.join(f'INSERT INTO t VALUES({100 + worker * 20 + row},20);' for row in range(20))
            start.wait(timeout=5)
            return self.run_sql(sql, timeout=5000)
        with ThreadPoolExecutor(max_workers=4) as workers:
            futures = [workers.submit(insert_batch, worker) for worker in range(4)]
            for future in futures:
                future.result(timeout=12)
        out, _ = self.run_sql('SELECT COUNT(*) FROM t; SELECT score FROM t WHERE id=1;')
        self.assertIn('count(*)\n81\n', out)
        self.assertIn('score\n10\n', out)

    def test_timeout_has_no_partial_write_and_can_retry(self):
        holder, waiting = self.session(), self.session(timeout=100)
        holder.request('BEGIN;', 'exclusive lock')
        before = self.db.read_bytes()
        waiting.send('BEGIN;')
        waiting.until('lock timeout', errors=True)
        waiting.send('COMMIT;')
        waiting.until('No active transaction', errors=True)
        self.assertEqual(self.db.read_bytes(), before)
        holder.request('ROLLBACK;', 'rolled back')
        waiting.request('BEGIN;', 'exclusive lock')
        waiting.request('UPDATE t SET score=55;', 'updated')
        waiting.request('COMMIT;', 'committed')
        out, _ = self.run_sql('SELECT score FROM t;')
        self.assertIn('score\n55\n', out)

    def test_read_only_rejects_dml_and_ddl_without_upgrade(self):
        before = self.db.read_bytes()
        out, err = self.run_sql('BEGIN READ ONLY; UPDATE t SET score=99; CREATE TABLE blocked(id INT);'
                               'SELECT score FROM t; COMMIT;', 1)
        self.assertEqual(err.count('Cannot write in a read-only transaction'), 2)
        self.assertIn('score\n10\n', out)
        self.assertEqual(self.db.read_bytes(), before)
        self.run_sql('UPDATE t SET score=20;')

    def test_read_only_commit_does_not_write_snapshot(self):
        Path(str(self.db) + '.tmp').mkdir()
        out, _ = self.run_sql('BEGIN READ ONLY; SELECT score FROM t; COMMIT;')
        self.assertIn('Transaction committed', out)

    def test_autocommit_writer_waits_then_reloads(self):
        a, b = self.session(), self.session()
        a.request('BEGIN;', 'exclusive lock')
        a.request('INSERT INTO t VALUES(2,20);', 'inserted')
        b.send('INSERT INTO t VALUES(3,30);')
        b.blocked()
        a.request('COMMIT;', 'committed')
        b.until('inserted')
        out, _ = self.run_sql('SELECT COUNT(*) FROM t;')
        self.assertIn('count(*)\n3\n', out)

    def test_independent_databases_do_not_block_each_other(self):
        holder = self.session()
        holder.request('BEGIN;', 'exclusive lock')
        other = Path(self.temp.name) / 'other.db'
        self.run_sql('CREATE TABLE independent(id INT);', timeout=0, db=other)
        holder.request('ROLLBACK;', 'rolled back')

    def test_process_kill_discards_transaction_and_releases_lock(self):
        holder, reader = self.session(), self.session()
        holder.request('BEGIN;', 'exclusive lock')
        holder.request('DELETE FROM t;', 'deleted')
        reader.send('SELECT COUNT(*) FROM t;')
        reader.blocked()
        holder.p.kill()
        holder.p.wait(timeout=5)
        self.assertIn('count(*)\n1\n', reader.until('1 row(s)'))
        self.run_sql('UPDATE t SET score=66;')

    def test_failed_commit_keeps_lock_until_rollback(self):
        holder, reader = self.session(), self.session()
        holder.request('BEGIN;', 'exclusive lock')
        holder.request('UPDATE t SET score=99;', 'updated')
        Path(str(self.db) + '.tmp').mkdir()
        holder.send('COMMIT;')
        holder.until('Cannot write database temporary file', errors=True)
        reader.send('SELECT score FROM t;')
        reader.blocked()
        holder.request('ROLLBACK;', 'rolled back')
        self.assertIn('score\n10\n', reader.until('1 row(s)'))

    def test_ddl_waits_for_reader_and_refreshes_catalog(self):
        reader, writer = self.session(), self.session()
        reader.request('BEGIN READ ONLY;', 'shared lock')
        writer.send('ALTER TABLE t ADD COLUMN note TEXT;')
        writer.blocked()
        self.assertNotIn('note', reader.request('DESCRIBE t;', 'No explicit indexes'))
        reader.request('COMMIT;', 'committed')
        writer.until('Column added')
        self.assertIn('note\tTEXT', reader.request('DESCRIBE t;', 'No explicit indexes'))

    def test_recovery_then_shared_transactions(self):
        wal = Path(str(self.db) + '.wal')
        wal.write_bytes(self.db.read_bytes())
        a, b = self.session(), self.session()
        self.assertFalse(wal.exists())
        a.request('BEGIN READ ONLY;', 'shared lock')
        b.request('BEGIN READ ONLY;', 'shared lock')
        self.assertIn('score\n10\n', b.request('SELECT score FROM t;', '1 row(s)'))
        a.request('ROLLBACK;', 'rolled back')
        b.request('COMMIT;', 'committed')

    def test_lock_timeout_option_validation(self):
        for value in ('-1', 'abc', '600001', '1.5', '999999999999999999999'):
            p = subprocess.run([str(EXE), '--lock-timeout-ms', value], capture_output=True,
                               text=True, encoding='utf-8', timeout=5)
            self.assertEqual(p.returncode, 2)
            self.assertIn('requires an integer', p.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
