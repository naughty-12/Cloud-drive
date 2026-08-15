-- =============================================================================
-- 0323 Cloud Disk — Demo Seed Data
-- =============================================================================
-- Purpose: Pre-populate database with demo files + AI preview caches so an
--          interviewer can login → see files → double-click → AI preview works
--          instantly without needing OPENAI_API_KEY.
--
-- Prerequisites:
--   1. MySQL running on localhost:3306 (root / 20041130 / new_schema15)
--   2. init-db.sql has already been run to create the base tables
--   3. Sample files exist at C:\disk1\1\ (see seed_demo_readme.md)
--
-- Demo Account:
--   Username: demo
--   Password: demo123
--   SHA-256 = SHA256("demo123" + "0323CloudDisk_SALT_2026")
--   To verify: echo -n "demo1230323CloudDisk_SALT_2026" | openssl dgst -sha256
--   Expected:  8b1059ae94846da40c6dd11ca6316649b3176e5a4abb0b24005d3e39d22b9d92
-- =============================================================================

USE new_schema15;

-- =============================================================================
-- 1. AI Previews Table (create if not exists)
-- =============================================================================
-- Note: The server currently does NOT read from this cache table (it calls
-- the AI API directly each time). This table is populated so that it is ready
-- when the cache lookup is added. In the meantime, AI preview works via the
-- built-in fallback (returns first 200 chars of file content as summary)
-- when OPENAI_API_KEY is not set.
-- =============================================================================

CREATE TABLE IF NOT EXISTS ai_previews (
    f_id            BIGINT PRIMARY KEY,
    summary         TEXT,
    keywords        VARCHAR(500),
    key_sentences   TEXT,
    file_type       VARCHAR(50),
    created_at      DATETIME DEFAULT NOW(),
    FOREIGN KEY (f_id) REFERENCES files(f_id) ON DELETE CASCADE
);

-- =============================================================================
-- 2. Demo User
-- =============================================================================
-- Uses ON DUPLICATE KEY UPDATE to work with existing 'demo' user from init-db.sql
-- without changing the user's u_id (preserving referential integrity).
-- =============================================================================

INSERT INTO user (u_name, u_password, u_tel, u_created) VALUES
('demo', '8b1059ae94846da40c6dd11ca6316649b3176e5a4abb0b24005d3e39d22b9d92', 13800138000, NOW())
ON DUPLICATE KEY UPDATE
    u_password = VALUES(u_password),
    u_tel      = VALUES(u_tel);

-- =============================================================================
-- 3. Sample Files
-- =============================================================================
-- f_sha256 column stores SHA-256 hex (64 chars). Renamed from f_MD5 (2026-08-08).
--
-- SHA-256 hashes were computed from the actual sample files in
-- C:\disk1\demo_samples\. The f_path points to the server's legacy fallback
-- path where the server looks for file content when FileStorage has no entry.
-- =============================================================================

-- File 1: C++ Programming Guide
INSERT INTO files (f_name, f_size, f_sha256, f_path, fcount)
SELECT 'demo_cpp_guide.txt', 7067,
       '8f7d54de0ac08ddca74bb95eeb560f6a075fbdc404f3caa4696e86fa868967b0',
       'C:\\disk1\\1\\demo_cpp_guide.txt', 1
WHERE NOT EXISTS (
    SELECT 1 FROM files WHERE f_sha256 = '8f7d54de0ac08ddca74bb95eeb560f6a075fbdc404f3caa4696e86fa868967b0'
);

-- File 2: Qt Signals and Slots Guide
INSERT INTO files (f_name, f_size, f_sha256, f_path, fcount)
SELECT 'demo_qt_signals.md', 4363,
       'ce1a54a9385009444505253dba1eb448245f17130578b5589cc6afbdde7ea718',
       'C:\\disk1\\1\\demo_qt_signals.md', 1
WHERE NOT EXISTS (
    SELECT 1 FROM files WHERE f_sha256 = 'ce1a54a9385009444505253dba1eb448245f17130578b5589cc6afbdde7ea718'
);

-- File 3: Architecture Notes
INSERT INTO files (f_name, f_size, f_sha256, f_path, fcount)
SELECT 'demo_notes.txt', 4030,
       '5979fbeb186d6e1d0cc163b42a00ac40e26eda2f519ba0b7b298d6c87901b703',
       'C:\\disk1\\1\\demo_notes.txt', 1
WHERE NOT EXISTS (
    SELECT 1 FROM files WHERE f_sha256 = '5979fbeb186d6e1d0cc163b42a00ac40e26eda2f519ba0b7b298d6c87901b703'
);

-- File 4: Project Overview
INSERT INTO files (f_name, f_size, f_sha256, f_path, fcount)
SELECT 'demo_readme.txt', 2943,
       'de845935503acd09aeae149caa981aab19545d339dd4c3968cc4f07ca5e5e2f2',
       'C:\\disk1\\1\\demo_readme.txt', 1
WHERE NOT EXISTS (
    SELECT 1 FROM files WHERE f_sha256 = 'de845935503acd09aeae149caa981aab19545d339dd4c3968cc4f07ca5e5e2f2'
);

-- File 5: REST API Design Guide
INSERT INTO files (f_name, f_size, f_sha256, f_path, fcount)
SELECT 'demo_api_doc.txt', 7073,
       'ac80d93949cf0ca56b0df4c127ae80e566946bed7354b12d1ec85b2ed8ba9ac4',
       'C:\\disk1\\1\\demo_api_doc.txt', 1
WHERE NOT EXISTS (
    SELECT 1 FROM files WHERE f_sha256 = 'ac80d93949cf0ca56b0df4c127ae80e566946bed7354b12d1ec85b2ed8ba9ac4'
);

-- =============================================================================
-- 4. User-File Mappings
-- =============================================================================
-- Links the demo user (u_id=1 in a fresh database) to all 5 files.
-- Uses a subquery to find the actual u_id of the 'demo' user, and matches
-- files by SHA-256 to find their f_id values.
-- =============================================================================

-- Mapping 1: demo → demo_cpp_guide.txt
INSERT IGNORE INTO user_file (u_id, f_id)
SELECT u.u_id, f.f_id FROM user u, files f
WHERE u.u_name = 'demo'
  AND f.f_sha256 = '8f7d54de0ac08ddca74bb95eeb560f6a075fbdc404f3caa4696e86fa868967b0';

-- Mapping 2: demo → demo_qt_signals.md
INSERT IGNORE INTO user_file (u_id, f_id)
SELECT u.u_id, f.f_id FROM user u, files f
WHERE u.u_name = 'demo'
  AND f.f_sha256 = 'ce1a54a9385009444505253dba1eb448245f17130578b5589cc6afbdde7ea718';

-- Mapping 3: demo → demo_notes.txt
INSERT IGNORE INTO user_file (u_id, f_id)
SELECT u.u_id, f.f_id FROM user u, files f
WHERE u.u_name = 'demo'
  AND f.f_sha256 = '5979fbeb186d6e1d0cc163b42a00ac40e26eda2f519ba0b7b298d6c87901b703';

-- Mapping 4: demo → demo_readme.txt
INSERT IGNORE INTO user_file (u_id, f_id)
SELECT u.u_id, f.f_id FROM user u, files f
WHERE u.u_name = 'demo'
  AND f.f_sha256 = 'de845935503acd09aeae149caa981aab19545d339dd4c3968cc4f07ca5e5e2f2';

-- Mapping 5: demo → demo_api_doc.txt
INSERT IGNORE INTO user_file (u_id, f_id)
SELECT u.u_id, f.f_id FROM user u, files f
WHERE u.u_name = 'demo'
  AND f.f_sha256 = 'ac80d93949cf0ca56b0df4c127ae80e566946bed7354b12d1ec85b2ed8ba9ac4';

-- =============================================================================
-- 5. AI Preview Cache Entries
-- =============================================================================
-- Pre-computed AI responses so preview works offline. Each entry provides a
-- realistic 2-3 sentence summary, 5-8 keywords, and 2-3 key sentences that
-- would match what gpt-4o-mini would produce from the file content.
--
-- Uses subqueries to look up f_id by SHA-256 (avoiding hardcoded IDs that
-- may differ from the actual database state).
-- =============================================================================

-- Preview Cache 1: demo_cpp_guide.txt
INSERT INTO ai_previews (f_id, summary, keywords, key_sentences, file_type, created_at)
SELECT f.f_id,
    '这是一份C++编程入门指南，详细介绍了类与对象、继承与多态、虚函数、访问控制、构造函数与析构函数、智能指针以及模板等核心概念。文档从基础语法出发逐步深入到RAII和现代C++11特性，适合初学者系统学习面向对象编程。',
    'C++, 面向对象, 类, 继承, 多态, 虚函数, 智能指针, 模板',
    'Inheritance allows a class to derive properties and behaviors from a base class.|Virtual functions enable runtime polymorphism — the correct function is called based on the actual object type, not the pointer type.|Modern C++ prefers smart pointers over raw pointers for automatic memory management.',
    'C++ Tutorial',
    NOW()
FROM files f
WHERE f.f_sha256 = '8f7d54de0ac08ddca74bb95eeb560f6a075fbdc404f3caa4696e86fa868967b0'
ON DUPLICATE KEY UPDATE
    summary       = VALUES(summary),
    keywords      = VALUES(keywords),
    key_sentences = VALUES(key_sentences),
    file_type     = VALUES(file_type);

-- Preview Cache 2: demo_qt_signals.md
INSERT INTO ai_previews (f_id, summary, keywords, key_sentences, file_type, created_at)
SELECT f.f_id,
    '这是一份Qt信号与槽机制的完整教程，涵盖了信号槽的基本概念、声明语法、连接方式以及跨线程通信的四种连接类型。文档还介绍了Meta-Object Compiler的工作原理、最佳实践和常见陷阱，是理解Qt事件驱动架构的重要参考资料。',
    'Qt, 信号槽, Signals, Slots, MOC, QObject, 跨线程, BlockingQueuedConnection',
    'Signals and slots are Qt''s primary mechanism for inter-object communication.|Qt::BlockingQueuedConnection works like QueuedConnection but blocks the sender thread until the slot returns.|Qt''s signals and slots rely on the Meta-Object Compiler (MOC), which processes header files and generates additional C++ code.',
    'Markdown Documentation',
    NOW()
FROM files f
WHERE f.f_sha256 = 'ce1a54a9385009444505253dba1eb448245f17130578b5589cc6afbdde7ea718'
ON DUPLICATE KEY UPDATE
    summary       = VALUES(summary),
    keywords      = VALUES(keywords),
    key_sentences = VALUES(key_sentences),
    file_type     = VALUES(file_type);

-- Preview Cache 3: demo_notes.txt
INSERT INTO ai_previews (f_id, summary, keywords, key_sentences, file_type, created_at)
SELECT f.f_id,
    '这是一份云盘系统的架构设计笔记，记录了项目的四层架构（客户端、服务端、共享协议库、AI增强层）以及关键技术决策。文档详细说明了选择IOCP异步I/O、自定义二进制协议、SHA-256指纹和libprotocol静态库的设计理由，还包含数据库策略和三层秒传漏斗的说明。',
    '云盘架构, IOCP, BinaryStream, SHA-256, libprotocol, 秒传, 三层漏斗, 数据库',
    'IOCP is Windows-native async I/O with kernel-level thread pool management, replacing the per-client thread model.|The instant upload funnel: L1 Client SQLite cache (~70% hit rate, <1ms), L2 sparse fingerprint (~10ms), L3 Bloom Filter (~1 microsecond).',
    'Architecture Notes',
    NOW()
FROM files f
WHERE f.f_sha256 = '5979fbeb186d6e1d0cc163b42a00ac40e26eda2f519ba0b7b298d6c87901b703'
ON DUPLICATE KEY UPDATE
    summary       = VALUES(summary),
    keywords      = VALUES(keywords),
    key_sentences = VALUES(key_sentences),
    file_type     = VALUES(file_type);

-- Preview Cache 4: demo_readme.txt
INSERT INTO ai_previews (f_id, summary, keywords, key_sentences, file_type, created_at)
SELECT f.f_id,
    '这是0323云盘项目的总体介绍文档，概括了项目的核心功能（文件上传秒传、断点续传、AI预览、语义搜索、自动标签等）、技术栈（C++11/Qt 5.15/MySQL 8.0/IOCP）、快速启动步骤以及项目结构。文档还说明了AI功能的可选性和降级策略。',
    '云盘, 快速开始, C++11, Qt, MySQL, IOCP, AI预览, 项目概述',
    '0323 Cloud Disk is a full-stack cloud storage application featuring file upload with instant deduplication, breakpoint resume, and AI-powered file preview.|Without API key, all AI features gracefully degrade to basic functionality.',
    'Project README',
    NOW()
FROM files f
WHERE f.f_sha256 = 'de845935503acd09aeae149caa981aab19545d339dd4c3968cc4f07ca5e5e2f2'
ON DUPLICATE KEY UPDATE
    summary       = VALUES(summary),
    keywords      = VALUES(keywords),
    key_sentences = VALUES(key_sentences),
    file_type     = VALUES(file_type);

-- Preview Cache 5: demo_api_doc.txt
INSERT INTO ai_previews (f_id, summary, keywords, key_sentences, file_type, created_at)
SELECT f.f_id,
    '这是一份REST API设计最佳实践指南，涵盖了资源命名规范、HTTP方法与状态码映射、请求响应设计模式、分页过滤排序、版本管理、JWT认证、限流策略和文件上传端点设计等完整主题。文档以云盘API为案例，提供了可落地的代码示例和OpenAPI规范。',
    'REST API, HTTP, JWT, 分页, 版本管理, 认证, OpenAPI, 限流',
    'Use nouns, not verbs, for resource endpoints — GET /api/files, not GET /api/getFiles.|For public APIs, URL versioning is recommended — it is simple, explicit, and easy to test with any HTTP client.|Use JWT (JSON Web Token) with the Authorization: Bearer header for stateless authentication.',
    'API Design Guide',
    NOW()
FROM files f
WHERE f.f_sha256 = 'ac80d93949cf0ca56b0df4c127ae80e566946bed7354b12d1ec85b2ed8ba9ac4'
ON DUPLICATE KEY UPDATE
    summary       = VALUES(summary),
    keywords      = VALUES(keywords),
    key_sentences = VALUES(key_sentences),
    file_type     = VALUES(file_type);

-- =============================================================================
-- Verification Query
-- =============================================================================
-- Run this after seeding to verify everything is in place:
--
--   SELECT u.u_name, f.f_name, f.f_size, f.f_sha256
--   FROM user u
--   JOIN user_file uf ON u.u_id = uf.u_id
--   JOIN files f ON uf.f_id = f.f_id
--   WHERE u.u_name = 'demo';
--
-- Expected output: 5 rows, one for each demo file
-- =============================================================================

-- Verify ai_previews cache:
--   SELECT f.f_name, ap.summary, ap.keywords, ap.file_type
--   FROM ai_previews ap
--   JOIN files f ON ap.f_id = f.f_id;
--
-- Expected output: 5 rows with pre-computed AI summaries
-- =============================================================================
