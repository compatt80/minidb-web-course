-- Eight entities; 1:N, M:N and optional 1:1; idempotent openGauss initialization.

CREATE TABLE IF NOT EXISTS departments (
    id INTEGER PRIMARY KEY,
    name VARCHAR(80) NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS majors (
    id INTEGER PRIMARY KEY,
    name VARCHAR(80) NOT NULL,
    department_id INTEGER NOT NULL REFERENCES departments(id)
);

CREATE TABLE IF NOT EXISTS classes (
    id INTEGER PRIMARY KEY,
    name VARCHAR(40) NOT NULL UNIQUE,
    major_id INTEGER NOT NULL REFERENCES majors(id)
);

CREATE TABLE IF NOT EXISTS students (
    id INTEGER PRIMARY KEY,
    name VARCHAR(80) NOT NULL,
    class_id INTEGER NOT NULL REFERENCES classes(id),
    score NUMERIC(5,2),
    email VARCHAR(120) UNIQUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS teachers (
    id INTEGER PRIMARY KEY,
    name VARCHAR(80) NOT NULL,
    title VARCHAR(40)
);

CREATE TABLE IF NOT EXISTS courses (
    id INTEGER PRIMARY KEY,
    name VARCHAR(80) NOT NULL,
    credit NUMERIC(3,1) NOT NULL,
    teacher_id INTEGER NOT NULL REFERENCES teachers(id)
);

CREATE TABLE IF NOT EXISTS enrollments (
    id INTEGER PRIMARY KEY,
    student_id INTEGER NOT NULL REFERENCES students(id),
    course_id INTEGER NOT NULL REFERENCES courses(id),
    UNIQUE (student_id, course_id)
);

CREATE TABLE IF NOT EXISTS scores (
    id INTEGER PRIMARY KEY,
    enrollment_id INTEGER NOT NULL UNIQUE REFERENCES enrollments(id),
    score NUMERIC(5,2),
    exam_date DATE
);

CREATE INDEX IF NOT EXISTS idx_students_class ON students(class_id);

CREATE INDEX IF NOT EXISTS idx_enrollments_student ON enrollments(student_id);

CREATE INDEX IF NOT EXISTS idx_enrollments_course ON enrollments(course_id);

INSERT INTO departments (id, name)
SELECT 1, '计算机学院'
WHERE NOT EXISTS (SELECT 1 FROM departments WHERE id = 1);

INSERT INTO departments (id, name)
SELECT 2, '数学学院'
WHERE NOT EXISTS (SELECT 1 FROM departments WHERE id = 2);

INSERT INTO majors (id, name, department_id)
SELECT 1, '计算机科学', 1
WHERE NOT EXISTS (SELECT 1 FROM majors WHERE id = 1);

INSERT INTO majors (id, name, department_id)
SELECT 2, '软件工程', 1
WHERE NOT EXISTS (SELECT 1 FROM majors WHERE id = 2);

INSERT INTO majors (id, name, department_id)
SELECT 3, '应用数学', 2
WHERE NOT EXISTS (SELECT 1 FROM majors WHERE id = 3);

INSERT INTO classes (id, name, major_id)
SELECT 1, '计241', 1
WHERE NOT EXISTS (SELECT 1 FROM classes WHERE id = 1);

INSERT INTO classes (id, name, major_id)
SELECT 2, '软241', 2
WHERE NOT EXISTS (SELECT 1 FROM classes WHERE id = 2);

INSERT INTO classes (id, name, major_id)
SELECT 3, '数241', 3
WHERE NOT EXISTS (SELECT 1 FROM classes WHERE id = 3);

INSERT INTO students (id, name, class_id, score, email)
SELECT 1, '张三', 1, 88.5, 'zhangsan@example.com'
WHERE NOT EXISTS (SELECT 1 FROM students WHERE id = 1);

INSERT INTO students (id, name, class_id, score, email)
SELECT 2, '李四', 1, 92.0, 'lisi@example.com'
WHERE NOT EXISTS (SELECT 1 FROM students WHERE id = 2);

INSERT INTO students (id, name, class_id, score, email)
SELECT 3, '王五', 2, 79.5, 'wangwu@example.com'
WHERE NOT EXISTS (SELECT 1 FROM students WHERE id = 3);

INSERT INTO students (id, name, class_id, score, email)
SELECT 4, '赵六', 2, NULL, NULL
WHERE NOT EXISTS (SELECT 1 FROM students WHERE id = 4);

INSERT INTO students (id, name, class_id, score, email)
SELECT 5, '钱七', 3, 91.0, 'qianqi@example.com'
WHERE NOT EXISTS (SELECT 1 FROM students WHERE id = 5);

INSERT INTO students (id, name, class_id, score, email)
SELECT 6, '孙八', 3, 84.0, NULL
WHERE NOT EXISTS (SELECT 1 FROM students WHERE id = 6);

INSERT INTO teachers (id, name, title)
SELECT 1, '赵老师', '教授'
WHERE NOT EXISTS (SELECT 1 FROM teachers WHERE id = 1);

INSERT INTO teachers (id, name, title)
SELECT 2, '刘老师', '副教授'
WHERE NOT EXISTS (SELECT 1 FROM teachers WHERE id = 2);

INSERT INTO teachers (id, name, title)
SELECT 3, '陈老师', '讲师'
WHERE NOT EXISTS (SELECT 1 FROM teachers WHERE id = 3);

INSERT INTO courses (id, name, credit, teacher_id)
SELECT 1, '数据库原理', 3.0, 1
WHERE NOT EXISTS (SELECT 1 FROM courses WHERE id = 1);

INSERT INTO courses (id, name, credit, teacher_id)
SELECT 2, '操作系统', 4.0, 1
WHERE NOT EXISTS (SELECT 1 FROM courses WHERE id = 2);

INSERT INTO courses (id, name, credit, teacher_id)
SELECT 3, '软件工程', 3.0, 2
WHERE NOT EXISTS (SELECT 1 FROM courses WHERE id = 3);

INSERT INTO courses (id, name, credit, teacher_id)
SELECT 4, '高等数学', 4.0, 3
WHERE NOT EXISTS (SELECT 1 FROM courses WHERE id = 4);

INSERT INTO enrollments (id, student_id, course_id)
SELECT 1, 1, 1
WHERE NOT EXISTS (SELECT 1 FROM enrollments WHERE id = 1);

INSERT INTO enrollments (id, student_id, course_id)
SELECT 2, 1, 2
WHERE NOT EXISTS (SELECT 1 FROM enrollments WHERE id = 2);

INSERT INTO enrollments (id, student_id, course_id)
SELECT 3, 2, 1
WHERE NOT EXISTS (SELECT 1 FROM enrollments WHERE id = 3);

INSERT INTO enrollments (id, student_id, course_id)
SELECT 4, 2, 2
WHERE NOT EXISTS (SELECT 1 FROM enrollments WHERE id = 4);

INSERT INTO enrollments (id, student_id, course_id)
SELECT 5, 3, 1
WHERE NOT EXISTS (SELECT 1 FROM enrollments WHERE id = 5);

INSERT INTO enrollments (id, student_id, course_id)
SELECT 6, 3, 3
WHERE NOT EXISTS (SELECT 1 FROM enrollments WHERE id = 6);

INSERT INTO enrollments (id, student_id, course_id)
SELECT 7, 4, 3
WHERE NOT EXISTS (SELECT 1 FROM enrollments WHERE id = 7);

INSERT INTO enrollments (id, student_id, course_id)
SELECT 8, 5, 4
WHERE NOT EXISTS (SELECT 1 FROM enrollments WHERE id = 8);

INSERT INTO enrollments (id, student_id, course_id)
SELECT 9, 6, 4
WHERE NOT EXISTS (SELECT 1 FROM enrollments WHERE id = 9);

INSERT INTO scores (id, enrollment_id, score, exam_date)
SELECT 1, 1, 90.0, '2026-06-30'
WHERE NOT EXISTS (SELECT 1 FROM scores WHERE id = 1);

INSERT INTO scores (id, enrollment_id, score, exam_date)
SELECT 2, 2, 86.0, '2026-06-30'
WHERE NOT EXISTS (SELECT 1 FROM scores WHERE id = 2);

INSERT INTO scores (id, enrollment_id, score, exam_date)
SELECT 3, 3, 85.0, '2026-06-30'
WHERE NOT EXISTS (SELECT 1 FROM scores WHERE id = 3);

INSERT INTO scores (id, enrollment_id, score, exam_date)
SELECT 4, 4, 93.0, '2026-06-30'
WHERE NOT EXISTS (SELECT 1 FROM scores WHERE id = 4);

INSERT INTO scores (id, enrollment_id, score, exam_date)
SELECT 5, 5, 78.0, '2026-06-30'
WHERE NOT EXISTS (SELECT 1 FROM scores WHERE id = 5);

INSERT INTO scores (id, enrollment_id, score, exam_date)
SELECT 6, 6, 82.0, '2026-06-30'
WHERE NOT EXISTS (SELECT 1 FROM scores WHERE id = 6);

INSERT INTO scores (id, enrollment_id, score, exam_date)
SELECT 7, 7, NULL, '2026-06-30'
WHERE NOT EXISTS (SELECT 1 FROM scores WHERE id = 7);

INSERT INTO scores (id, enrollment_id, score, exam_date)
SELECT 8, 8, 91.0, '2026-06-30'
WHERE NOT EXISTS (SELECT 1 FROM scores WHERE id = 8);

INSERT INTO scores (id, enrollment_id, score, exam_date)
SELECT 9, 9, 84.0, '2026-06-30'
WHERE NOT EXISTS (SELECT 1 FROM scores WHERE id = 9);
