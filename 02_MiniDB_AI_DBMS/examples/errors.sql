-- Run against the demo database with --continue-on-error.
INSERT INTO students VALUES (1, '重复主键', NULL, 80.0, NULL);
INSERT INTO students VALUES (9, NULL, NULL, 80.0, NULL);
UPDATE students SET score = '不是数字' WHERE id = 1;
SELECT missing FROM students;
ALTER TABLE students DROP COLUMN id;
SELECT COUNT(*) FROM students;
