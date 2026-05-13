CREATE DATABASE IF NOT EXISTS test_db
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

USE test_db;

SET FOREIGN_KEY_CHECKS = 0;

DROP TABLE IF EXISTS orders;
DROP TABLE IF EXISTS users;

SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE users (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    email VARCHAR(255) NOT NULL,
    name VARCHAR(100) NOT NULL,
    status ENUM('active', 'disabled') NOT NULL DEFAULT 'active',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    PRIMARY KEY (id),
    UNIQUE KEY uq_users_email (email),
    CHECK (CHAR_LENGTH(name) >= 2)
) ENGINE=InnoDB;

CREATE TABLE orders (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    user_id BIGINT UNSIGNED NOT NULL,
    reviewed_by_user_id BIGINT UNSIGNED NULL,
    order_number VARCHAR(32) NOT NULL,
    amount_cents INT UNSIGNED NOT NULL,
    status ENUM('pending', 'paid', 'cancelled') NOT NULL DEFAULT 'pending',
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

    PRIMARY KEY (id),
    UNIQUE KEY uq_orders_order_number (order_number),
    KEY idx_orders_user_id (user_id),
    KEY idx_orders_reviewed_by_user_id (reviewed_by_user_id),

    CONSTRAINT fk_orders_user
        FOREIGN KEY (user_id)
        REFERENCES users(id)
        ON DELETE CASCADE
        ON UPDATE RESTRICT,

    CONSTRAINT fk_orders_reviewed_by_user
        FOREIGN KEY (reviewed_by_user_id)
        REFERENCES users(id)
        ON DELETE SET NULL
        ON UPDATE RESTRICT,

    CHECK (amount_cents > 0)
) ENGINE=InnoDB;

INSERT INTO users (email, name, status)
VALUES
    ('alice@example.com', 'Alice', 'active'),
    ('bob@example.com', 'Bob', 'active'),
    ('disabled@example.com', 'Disabled User', 'disabled');

INSERT INTO orders (
    user_id,
    reviewed_by_user_id,
    order_number,
    amount_cents,
    status
)
VALUES
    (1, 2, 'ORD-0001', 1299, 'pending'),
    (1, NULL, 'ORD-0002', 4999, 'paid'),
    (2, 1, 'ORD-0003', 2500, 'cancelled');
