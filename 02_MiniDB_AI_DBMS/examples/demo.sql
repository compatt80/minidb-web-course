-- Student records demonstrate SQL and the database internals.
CREATE TABLE students (id INT PRIMARY KEY, name TEXT NOT NULL, major TEXT, score REAL);
INSERT INTO students VALUES
  (1, '张三', '计算机', 88.5),
  (2, '李四', '软件工程', 92.0),
  (3, '王五', '计算机', 76.0),
  (4, '赵六', NULL, 85.0);
SHOW TABLES;
DESCRIBE students;
SELECT name, score FROM students WHERE score >= 80 ORDER BY score DESC LIMIT 3;
EXPLAIN SELECT * FROM students WHERE id = 2;
CREATE INDEX idx_students_id ON students (id);
EXPLAIN SELECT * FROM students WHERE id = 2;
SELECT * FROM students WHERE id = 2;
SELECT COUNT(*) FROM students WHERE major = '计算机' OR major IS NULL;
ALTER TABLE students ADD COLUMN email TEXT;
UPDATE students SET email = 'zhangsan@example.com' WHERE id = 1;
ALTER TABLE students RENAME COLUMN email TO contact;
BEGIN;
UPDATE students SET score = 0 WHERE id = 1;
SELECT id, name, score FROM students WHERE id = 1;
ROLLBACK;
SELECT id, name, score FROM students WHERE id = 1;
BEGIN;
INSERT INTO students VALUES (5, '陈七', '软件工程', 95.0, NULL);
DELETE FROM students WHERE id = 3;
COMMIT;
SELECT * FROM students ORDER BY id;
CREATE TABLE scratch (id INT);
ALTER TABLE scratch RENAME TO scratch_new;
DROP TABLE scratch_new;
SHOW TABLES;
