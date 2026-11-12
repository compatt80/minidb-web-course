-- Run once on a new database, then open two CLI sessions on the same file.
CREATE TABLE accounts(id INT PRIMARY KEY, balance INT NOT NULL);
INSERT INTO accounts VALUES(1,1000),(2,800);
SELECT * FROM accounts ORDER BY id;
