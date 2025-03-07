--
-- Copyright (c) 2019-2024 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
--
-- Distributed under the Boost Software License, Version 1.0. (See accompanying
-- file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
--

-- Connection system variables
SET NAMES utf8;

-- Database
DROP DATABASE IF EXISTS correlations;
CREATE DATABASE correlations;
USE correlations;

-- Tables
CREATE TABLE correlations(
    id INT NOT NULL PRIMARY KEY AUTO_INCREMENT,
    subject VARCHAR(200) NOT NULL
);

-- Sample values
-- See https://www.tylervigen.com/spurious-correlations
INSERT INTO correlations (subject) VALUES
    ("Wind power generated in Taiwan vs. Google searches for 'I am tired'"),
    ("Pirate attacks globally vs. Google searches for 'download firefox'"),
    ("Per capita consumption of margarine vs. The divorce rate in Spain")
;

-- User
DROP USER IF EXISTS 'me'@'%';
CREATE USER 'me'@'%' IDENTIFIED BY 'secret';
GRANT ALL PRIVILEGES ON correlations.* TO 'me'@'%';
FLUSH PRIVILEGES;
