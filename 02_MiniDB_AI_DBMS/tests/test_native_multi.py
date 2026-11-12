"""Eight-entity native MiniDB tests, using only its implemented SQL subset."""
from pathlib import Path
import argparse, datetime, json, os, queue, subprocess, tempfile, threading, unittest

ROOT=Path(__file__).resolve().parents[1]
EXE=None

class Session:
    def __init__(self,case,db):
        self.p=subprocess.Popen([str(EXE),'--db',str(db),'--lock-timeout-ms','5000','--continue-on-error'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',bufsize=1)
        self.out=queue.Queue();self.err=queue.Queue();self.threads=[]
        for stream,target in [(self.p.stdout,self.out),(self.p.stderr,self.err)]:
            def pump(s=stream,q=target):
                for line in s: q.put(line)
                q.put(None)
            t=threading.Thread(target=pump,daemon=True);t.start();self.threads.append(t)
        case.addCleanup(self.close);self.request('HELP;','Types: INT')
    def send(self,sql): self.p.stdin.write(sql+'\n');self.p.stdin.flush()
    def until(self,token):
        lines=[]
        while True:
            line=self.out.get(timeout=10)
            if line is None: raise AssertionError('process exited: '+''.join(lines))
            lines.append(line)
            if token in line: return ''.join(lines)
    def request(self,sql,token): self.send(sql);return self.until(token)
    def blocked(self,case):
        with case.assertRaises(queue.Empty): self.out.get(timeout=.2)
        case.assertIsNone(self.p.poll())
    def close(self):
        if not self.p.stdin.closed:self.p.stdin.close()
        try:self.p.wait(timeout=5)
        except subprocess.TimeoutExpired:self.p.kill();self.p.wait(timeout=5)
        for t in self.threads:t.join(timeout=1)
        self.p.stdout.close();self.p.stderr.close()

class NativeMulti(unittest.TestCase):
    def setUp(self):
        (ROOT/'tests/tmp').mkdir(parents=True,exist_ok=True)
        self.tmp=tempfile.TemporaryDirectory(prefix='native-',dir=ROOT/'tests/tmp')
        self.addCleanup(self.tmp.cleanup);self.db=Path(self.tmp.name)/'multi.db'
        self.sql((ROOT/'examples/schema_multi.sql').read_text(encoding='utf-8'))
    def sql(self,sql,code=0,env=None,db=None,timeout=5000):
        p=subprocess.run([str(EXE),'--db',str(db or self.db),'--lock-timeout-ms',str(timeout),'--continue-on-error'],input=sql,text=True,encoding='utf-8',capture_output=True,timeout=20,env=env)
        self.assertEqual(p.returncode,code,p.stdout+p.stderr)
        return p.stdout,p.stderr
    def test_04_01_two_readers_and_cross_table_writer_timeout(self):
        a,b=Session(self,self.db),Session(self,self.db)
        a.request('BEGIN READ ONLY;','shared lock');b.request('BEGIN READ ONLY;','shared lock')
        self.assertIn('计算机学院',a.request('SELECT name FROM departments WHERE id=1;','1 row(s)'))
        self.assertIn('赵老师',b.request('SELECT name FROM teachers WHERE id=1;','1 row(s)'))
        _,err=self.sql("UPDATE teachers SET title='blocked' WHERE id=1;",1,timeout=0)
        self.assertIn('lock timeout',err)
        a.request('COMMIT;','committed');b.request('ROLLBACK;','rolled back')
    def test_04_01_no_dirty_read_and_commit_reload(self):
        a,b=Session(self,self.db),Session(self,self.db)
        a.request('BEGIN;','exclusive lock');a.request("UPDATE teachers SET title='已提交' WHERE id=1;",'updated')
        b.send('SELECT title FROM teachers WHERE id=1;');b.blocked(self)
        a.request('COMMIT;','committed');self.assertIn('已提交',b.until('1 row(s)'))
    def test_04_02_independent_indexes_and_plan(self):
        self.sql('CREATE INDEX idx_teacher_id ON teachers(id);')
        out,_=self.sql('EXPLAIN SELECT * FROM teachers WHERE id=2; EXPLAIN SELECT * FROM students WHERE class_id=1;')
        self.assertIn('IndexLookup(idx_teacher_id)\t1\t3',out)
        self.assertIn('IndexLookup(idx_students_class)\t2\t6',out)
        self.sql('DROP INDEX idx_teacher_id;')
        out,_=self.sql('EXPLAIN SELECT * FROM teachers WHERE id=2;');self.assertIn('TableScan\t3\t3',out)
    def test_04_02_large_tree_split_and_duplicates(self):
        values=','.join(f"({i},'批量{i}',1,80)" for i in range(1000,1600))
        self.sql('INSERT INTO students(id,name,class_id,score) VALUES'+values+';')
        out,_=self.sql('SELECT COUNT(*) FROM students WHERE class_id=1; SELECT name FROM students WHERE id=1599;')
        self.assertIn('count(*)\n602\n',out);self.assertIn('批量1599',out)
    def test_04_03_wal_recovers_entire_multitable_snapshot(self):
        env=os.environ.copy();env['MINIDB_TEST_CRASH_AFTER_WAL']='1'
        p=subprocess.run([str(EXE),'--db',str(self.db)],input="BEGIN; UPDATE students SET score=99 WHERE id=1; UPDATE teachers SET title='WAL恢复' WHERE id=1; COMMIT;",env=env,text=True,encoding='utf-8',capture_output=True,timeout=10)
        self.assertNotEqual(p.returncode,0);wal=Path(str(self.db)+'.wal');self.assertTrue(wal.exists())
        out,_=self.sql('SELECT score FROM students WHERE id=1; SELECT title FROM teachers WHERE id=1; SHOW TABLES;')
        self.assertIn('score\n99\n',out);self.assertIn('WAL恢复',out);self.assertIn('8 table(s)',out);self.assertFalse(wal.exists())
    def test_04_03_corrupted_multitable_file_rejected(self):
        bad=Path(self.tmp.name)/'corrupted.db';data=bytearray(self.db.read_bytes());data[-1]^=1;bad.write_bytes(data)
        _,err=self.sql('SHOW TABLES;',2,db=bad);self.assertIn('checksum mismatch',err.lower());self.assertEqual(bad.read_bytes(),data)
    def test_04_04_snapshot_larger_than_buffer_capacity(self):
        values=','.join(f"({i},'{('数据'*60)+str(i)}',1,80)" for i in range(1000,1600))
        self.sql('INSERT INTO students(id,name,class_id,score) VALUES'+values+';')
        self.assertGreater(self.db.stat().st_size,4096*8)
        out,_=self.sql('SELECT COUNT(*) FROM students; SELECT name FROM departments WHERE id=2;')
        self.assertIn('count(*)\n606\n',out);self.assertIn('数学学院',out)
    def test_04_05_strings_comments_precedence(self):
        out,_=self.sql("-- comment ;\nInSeRt INTO teachers(id,name) VALUES(900,'泛化;老师''甲'); SELECT name FROM teachers WHERE id=900; SELECT COUNT(*) FROM students WHERE id=1 OR id=2 AND class_id=999;")
        self.assertIn("泛化;老师'甲",out);self.assertIn('count(*)\n1\n',out)
    def test_04_05_bad_syntax_does_not_modify_tables(self):
        _,err=self.sql('SELECT @ FROM courses;',1);self.assertTrue(err)
        out,_=self.sql('SHOW TABLES;');self.assertIn('8 table(s)',out)
    def test_04_06_join_group_sort_limit(self):
        out,_=self.sql('SELECT students.name,enrollments.course_id FROM students JOIN enrollments ON students.id=enrollments.student_id ORDER BY students.id LIMIT 3; SELECT course_id,COUNT(*) FROM enrollments GROUP BY course_id ORDER BY course_id;')
        self.assertIn('张三\t1\n张三\t2\n李四\t1',out)
        self.assertIn('1\t3\n2\t2\n3\t2\n4\t2',out)
    def test_04_06_null_logic_and_name_binding(self):
        out,_=self.sql('SELECT COUNT(*) FROM scores WHERE score IS NULL; SELECT COUNT(*) FROM scores WHERE score=NULL;')
        self.assertIn('count(*)\n1\n',out);self.assertIn('count(*)\n0\n',out)
        self.sql('SELECT unknown_column FROM courses;',1)
    def test_04_09_cross_table_rollback_and_ddl(self):
        before=self.db.read_bytes()
        self.sql("BEGIN; UPDATE teachers SET title='回滚' WHERE id=1; DELETE FROM courses WHERE id=4; CREATE TABLE scratch(id INT); ROLLBACK;")
        self.assertEqual(self.db.read_bytes(),before)
        out,_=self.sql('SHOW TABLES; SELECT COUNT(*) FROM courses;');self.assertIn('8 table(s)',out);self.assertIn('count(*)\n4\n',out)
    def test_04_09_cross_table_commit_persists(self):
        self.sql("BEGIN; UPDATE teachers SET title='跨表提交' WHERE id=2; UPDATE courses SET credit=3.5 WHERE id=3; COMMIT;")
        out,_=self.sql('SELECT title FROM teachers WHERE id=2; SELECT credit FROM courses WHERE id=3;')
        self.assertIn('跨表提交',out);self.assertIn('credit\n3.5\n',out)
    def test_04_09_failed_statement_preserves_other_changes(self):
        self.sql("BEGIN; UPDATE teachers SET title='保留成功语句' WHERE id=1; INSERT INTO teachers VALUES(1,'重复',NULL); COMMIT;",1)
        out,_=self.sql('SELECT title FROM teachers WHERE id=1;');self.assertIn('保留成功语句',out)
    def test_04_13_eight_schemas_and_constraints(self):
        out,_=self.sql('SHOW TABLES; DESCRIBE courses; DESCRIBE scores;');self.assertIn('8 table(s)',out);self.assertIn('credit\tREAL',out)
        for sql in ["INSERT INTO teachers(id,name) VALUES(1,'重复');","INSERT INTO students(id,name,class_id,email) VALUES(900,'重复',1,'zhangsan@example.com');",'INSERT INTO teachers(id) VALUES(900);',"UPDATE courses SET credit='bad' WHERE id=1;",'INSERT INTO scores(id,enrollment_id) VALUES(900,1);']:
            self.sql(sql,1)
    def test_04_13_schema_alter_index_dependency_and_restart(self):
        self.sql('ALTER TABLE courses ADD COLUMN note TEXT; ALTER TABLE courses RENAME COLUMN note TO memo; CREATE INDEX idx_course_name ON courses(name); ALTER TABLE courses RENAME COLUMN name TO course_name;')
        self.sql('ALTER TABLE courses DROP COLUMN course_name;',1)
        out,_=self.sql('DESCRIBE courses; EXPLAIN SELECT * FROM courses WHERE course_name=\'数据库原理\';')
        self.assertIn('memo\tTEXT',out);self.assertIn('IndexLookup(idx_course_name)',out)
        self.sql('DROP INDEX idx_course_name; ALTER TABLE courses DROP COLUMN memo;')
    def test_04_14_crud_across_nonstudent_entities(self):
        self.sql("INSERT INTO teachers(id,name) VALUES(900,'新增老师'); UPDATE teachers SET title='教授' WHERE id=900;")
        out,_=self.sql('SELECT title FROM teachers WHERE id=900;');self.assertIn('教授',out)
        self.sql('DELETE FROM teachers WHERE id=900;');out,_=self.sql('SELECT COUNT(*) FROM teachers;');self.assertIn('count(*)\n3\n',out)
    def test_04_14_multirow_insert_atomic(self):
        before=self.db.read_bytes()
        self.sql("INSERT INTO teachers(id,name) VALUES(900,'临时'),(1,'重复');",1)
        self.assertEqual(before,self.db.read_bytes())
    def test_04_14_empty_projection_and_limit(self):
        out,_=self.sql('SELECT name FROM teachers WHERE id=999; SELECT id FROM courses ORDER BY id DESC LIMIT 2;')
        self.assertIn('0 row(s)',out);self.assertIn('id\n4\n3\n',out)

class Recorder(unittest.TextTestResult):
    records=[]
    def addSuccess(self,test):super().addSuccess(test);self.records.append({'name':test.id(),'passed':True})
    def addFailure(self,test,err):super().addFailure(test,err);self.records.append({'name':test.id(),'passed':False,'error':self._exc_info_to_string(err,test)})
    def addError(self,test,err):super().addError(test,err);self.records.append({'name':test.id(),'passed':False,'error':self._exc_info_to_string(err,test)})

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args();EXE=args.exe.resolve()
    (ROOT/'tests/tmp').mkdir(parents=True,exist_ok=True)
    result=unittest.TextTestRunner(verbosity=2,resultclass=Recorder).run(unittest.defaultTestLoader.loadTestsFromTestCase(NativeMulti))
    args.output.write_text(json.dumps({'passed':sum(x['passed'] for x in result.records),'total':result.testsRun,'checks':result.records},ensure_ascii=False,indent=2),encoding='utf-8')
    raise SystemExit(0 if result.wasSuccessful() else 1)
