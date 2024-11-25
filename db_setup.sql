--
-- Copyright (c) 2019-2024 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
--
-- Distributed under the Boost Software License, Version 1.0. (See accompanying
-- file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
--

-- Connection system variables
SET NAMES utf8;

-- Database
DROP DATABASE IF EXISTS usingstdcpp;
CREATE DATABASE usingstdcpp;
USE usingstdcpp;

-- Tables
CREATE TABLE company(
    id CHAR(10) NOT NULL PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    tax_id VARCHAR(50) NOT NULL
);
CREATE TABLE employee(
    id INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name VARCHAR(100) NOT NULL,
    salary INT UNSIGNED,
    company_id CHAR(10) NOT NULL,
    FOREIGN KEY (company_id) REFERENCES company(id)
);

-- Sample values
INSERT INTO company (name, id, tax_id) VALUES
    ("Award Winning Company, Inc.", "AWC", "IE1234567V"),
    ("Sector Global Leader Plc", "SGL", "IE1234568V"),
    ("High Growth Startup, Ltd", "HGS", "IE1234569V")
;
INSERT INTO employee (first_name, last_name, salary, company_id) VALUES
    ("Efficient", "Developer", 30000, "AWC"),
    ("Lazy", "Manager", 80000, "AWC"),
    ("Good", "Team Player", 35000, "HGS"),
    ("Enormous", "Slacker", 45000, "SGL"),
    ("Coffee", "Drinker", 30000, "HGS"),
    ("Underpaid", "Intern", 15000, "AWC")
;
