-- Run one statement at a time in the existing Web console.
SELECT * FROM teachers ORDER BY id;

SELECT s.name AS student, c.name AS course, sc.score
FROM students s
JOIN enrollments e ON s.id = e.student_id
JOIN courses c ON e.course_id = c.id
JOIN scores sc ON sc.enrollment_id = e.id
ORDER BY e.id;

SELECT s.name, cl.name AS class_name, m.name AS major_name, d.name AS department_name
FROM students s
JOIN classes cl ON s.class_id = cl.id
JOIN majors m ON cl.major_id = m.id
JOIN departments d ON m.department_id = d.id
ORDER BY s.id;

SELECT cl.name AS class_name, COUNT(*) AS student_count
FROM classes cl JOIN students s ON s.class_id = cl.id
GROUP BY cl.name ORDER BY cl.name;

EXPLAIN SELECT * FROM enrollments WHERE student_id = 1;
