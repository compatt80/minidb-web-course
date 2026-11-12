CREATE TABLE IF NOT EXISTS students (
    id INTEGER PRIMARY KEY,
    name VARCHAR(80) NOT NULL,
    class_name VARCHAR(40) NOT NULL,
    score NUMERIC(5, 2),
    email VARCHAR(120) UNIQUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_students_class ON students(class_name);

INSERT INTO students (id, name, class_name, score, email)
SELECT seed.id, seed.name, seed.class_name, seed.score, seed.email
FROM (
    SELECT 1 AS id, '张三' AS name, '计241' AS class_name,
           88.50 AS score, 'zhangsan@example.com' AS email
    UNION ALL
    SELECT 2, '李四', '计241', 92.00, 'lisi@example.com'
    UNION ALL
    SELECT 3, '王五', '计242', 79.50, 'wangwu@example.com'
) AS seed
WHERE NOT EXISTS (SELECT 1 FROM students WHERE students.id = seed.id);
