-- Eight entities; native fixture uses the implemented SQL subset.

CREATE TABLE departments (
    id INT PRIMARY KEY,
    name TEXT NOT NULL UNIQUE
);

CREATE TABLE majors (
    id INT PRIMARY KEY,
    name TEXT NOT NULL,
    department_id INT NOT NULL
);

CREATE TABLE classes (
    id INT PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    major_id INT NOT NULL
);

CREATE TABLE students (
    id INT PRIMARY KEY,
    name TEXT NOT NULL,
    class_id INT NOT NULL,
    score REAL,
    email TEXT UNIQUE,
    created_at TEXT
);

CREATE TABLE teachers (
    id INT PRIMARY KEY,
    name TEXT NOT NULL,
    title TEXT
);

CREATE TABLE courses (
    id INT PRIMARY KEY,
    name TEXT NOT NULL,
    credit REAL NOT NULL,
    teacher_id INT NOT NULL
);

CREATE TABLE enrollments (
    id INT PRIMARY KEY,
    student_id INT NOT NULL,
    course_id INT NOT NULL
);

CREATE TABLE scores (
    id INT PRIMARY KEY,
    enrollment_id INT NOT NULL UNIQUE,
    score REAL,
    exam_date TEXT
);

CREATE INDEX idx_students_class ON students(class_id);

CREATE INDEX idx_enrollments_student ON enrollments(student_id);

CREATE INDEX idx_enrollments_course ON enrollments(course_id);

INSERT INTO departments (id, name) VALUES (1, '计算机学院');

INSERT INTO departments (id, name) VALUES (2, '数学学院');

INSERT INTO majors (id, name, department_id) VALUES (1, '计算机科学', 1);

INSERT INTO majors (id, name, department_id) VALUES (2, '软件工程', 1);

INSERT INTO majors (id, name, department_id) VALUES (3, '应用数学', 2);

INSERT INTO classes (id, name, major_id) VALUES (1, '计241', 1);

INSERT INTO classes (id, name, major_id) VALUES (2, '软241', 2);

INSERT INTO classes (id, name, major_id) VALUES (3, '数241', 3);

INSERT INTO students (id, name, class_id, score, email) VALUES (1, '张三', 1, 88.5, 'zhangsan@example.com');

INSERT INTO students (id, name, class_id, score, email) VALUES (2, '李四', 1, 92.0, 'lisi@example.com');

INSERT INTO students (id, name, class_id, score, email) VALUES (3, '王五', 2, 79.5, 'wangwu@example.com');

INSERT INTO students (id, name, class_id, score, email) VALUES (4, '赵六', 2, NULL, NULL);

INSERT INTO students (id, name, class_id, score, email) VALUES (5, '钱七', 3, 91.0, 'qianqi@example.com');

INSERT INTO students (id, name, class_id, score, email) VALUES (6, '孙八', 3, 84.0, NULL);

INSERT INTO teachers (id, name, title) VALUES (1, '赵老师', '教授');

INSERT INTO teachers (id, name, title) VALUES (2, '刘老师', '副教授');

INSERT INTO teachers (id, name, title) VALUES (3, '陈老师', '讲师');

INSERT INTO courses (id, name, credit, teacher_id) VALUES (1, '数据库原理', 3.0, 1);

INSERT INTO courses (id, name, credit, teacher_id) VALUES (2, '操作系统', 4.0, 1);

INSERT INTO courses (id, name, credit, teacher_id) VALUES (3, '软件工程', 3.0, 2);

INSERT INTO courses (id, name, credit, teacher_id) VALUES (4, '高等数学', 4.0, 3);

INSERT INTO enrollments (id, student_id, course_id) VALUES (1, 1, 1);

INSERT INTO enrollments (id, student_id, course_id) VALUES (2, 1, 2);

INSERT INTO enrollments (id, student_id, course_id) VALUES (3, 2, 1);

INSERT INTO enrollments (id, student_id, course_id) VALUES (4, 2, 2);

INSERT INTO enrollments (id, student_id, course_id) VALUES (5, 3, 1);

INSERT INTO enrollments (id, student_id, course_id) VALUES (6, 3, 3);

INSERT INTO enrollments (id, student_id, course_id) VALUES (7, 4, 3);

INSERT INTO enrollments (id, student_id, course_id) VALUES (8, 5, 4);

INSERT INTO enrollments (id, student_id, course_id) VALUES (9, 6, 4);

INSERT INTO scores (id, enrollment_id, score, exam_date) VALUES (1, 1, 90.0, '2026-06-30');

INSERT INTO scores (id, enrollment_id, score, exam_date) VALUES (2, 2, 86.0, '2026-06-30');

INSERT INTO scores (id, enrollment_id, score, exam_date) VALUES (3, 3, 85.0, '2026-06-30');

INSERT INTO scores (id, enrollment_id, score, exam_date) VALUES (4, 4, 93.0, '2026-06-30');

INSERT INTO scores (id, enrollment_id, score, exam_date) VALUES (5, 5, 78.0, '2026-06-30');

INSERT INTO scores (id, enrollment_id, score, exam_date) VALUES (6, 6, 82.0, '2026-06-30');

INSERT INTO scores (id, enrollment_id, score, exam_date) VALUES (7, 7, NULL, '2026-06-30');

INSERT INTO scores (id, enrollment_id, score, exam_date) VALUES (8, 8, 91.0, '2026-06-30');

INSERT INTO scores (id, enrollment_id, score, exam_date) VALUES (9, 9, 84.0, '2026-06-30');
