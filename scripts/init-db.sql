-- 0323 Cloud Disk — Database Initialization
CREATE DATABASE IF NOT EXISTS new_schema15;
USE new_schema15;

-- User table (SHA-256 password support)
CREATE TABLE IF NOT EXISTS user (
    u_id        BIGINT AUTO_INCREMENT PRIMARY KEY,
    u_name      VARCHAR(45) NOT NULL UNIQUE,
    u_password  CHAR(64) NOT NULL,
    u_tel       BIGINT,
    u_created   DATETIME DEFAULT NOW()
);

-- File table
CREATE TABLE IF NOT EXISTS files (
    f_id        BIGINT AUTO_INCREMENT PRIMARY KEY,
    f_name      VARCHAR(255) NOT NULL,
    f_size      BIGINT NOT NULL,
    f_uploadtime DATETIME DEFAULT NOW(),
    f_sha256    CHAR(64) NOT NULL,
    f_sparse_sha256 CHAR(64) DEFAULT '',     -- L2: sparse fingerprint for pre-check
    f_path      VARCHAR(512),
    fcount      INT DEFAULT 1,
    INDEX idx_sha256 (f_sha256),
    INDEX idx_sparse_sha256 (f_sparse_sha256)
);

-- User-file mapping
CREATE TABLE IF NOT EXISTS user_file (
    u_id BIGINT NOT NULL,
    f_id BIGINT NOT NULL,
    PRIMARY KEY (u_id, f_id)
);

-- Share links
CREATE TABLE IF NOT EXISTS share_links (
    share_code  CHAR(8) PRIMARY KEY,
    f_id        BIGINT NOT NULL,
    u_id        BIGINT NOT NULL,
    created_at  DATETIME DEFAULT NOW(),
    expires_at  DATETIME NULL
);

-- AI preview cache (checked before API call, populated on success)
CREATE TABLE IF NOT EXISTS ai_previews (
    f_id        BIGINT PRIMARY KEY,
    summary     TEXT,
    keywords    VARCHAR(500),
    key_sentences TEXT,
    file_type   VARCHAR(50),
    created_at  DATETIME DEFAULT NOW()
);

-- Semantic search embeddings (float vector → TEXT serialized)
CREATE TABLE IF NOT EXISTS file_embeddings (
    f_id        BIGINT PRIMARY KEY,
    embedding   MEDIUMTEXT NOT NULL,   -- comma-separated float32 values (1536-dim)
    created_at  DATETIME DEFAULT NOW()
);

-- Per-file tags
CREATE TABLE IF NOT EXISTS file_tags (
    f_id        BIGINT NOT NULL,
    tag         VARCHAR(100) NOT NULL,
    created_at  DATETIME DEFAULT NOW(),
    PRIMARY KEY (f_id, tag)
);

-- Tag pool with lifecycle: pending(<3 uses) → active(>=3) → deprecated(30d unused)
CREATE TABLE IF NOT EXISTS tag_pool (
    tag         VARCHAR(100) PRIMARY KEY,
    status      VARCHAR(20) DEFAULT 'pending',
    use_count   INT DEFAULT 1,
    created_at  DATETIME DEFAULT NOW(),
    last_used_at DATETIME DEFAULT NOW()
);

-- Demo user (password: 123456 salted with "0323CloudDisk_SALT_2026", SHA-256 hashed)
-- sha256("123456" + "0323CloudDisk_SALT_2026") = e8003cd0c471bc61eddf947138f15f04ec54ae9333f1ac52cdacef677302f330
-- 注意：seed_demo.sql 会用 demo/demo123（哈希 8b1059ae…）覆盖本用户密码；两者均可用，勿再使用旧盐 "0323CloudDisk"
INSERT IGNORE INTO user(u_name, u_password, u_tel) VALUES
('demo', 'e8003cd0c471bc61eddf947138f15f04ec54ae9333f1ac52cdacef677302f330', 13800138000);
