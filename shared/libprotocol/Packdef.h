#ifndef PACKDEF_H
#define PACKDEF_H

#include <cstdint>
#include <cstring>

// ============================================================================
// Packdef.h — 0323 云盘统一协议定义
// ============================================================================
// 这是唯一权威的协议定义文件，取代了原先 0323client/ 与 0323server/ 各自的副本。
//
// 所有结构体均继承自 STRUBASE（m_ntype = 协议类型标识）。
// 整数字段使用 int64_t 以保证可移植性与清晰性。
// SHA-256 字段使用显式 [65] 数组（64 个十六进制字符 + 结尾空字符）。
//
// ⚠️ MAXSIZE=45 不足以容纳 SHA-256 hex（64 字符）。
//    对于 SHA-256 字段，请使用显式 `[65]` 数组。
//    未来：迁移到 BinaryStream 动态字符串。
// ============================================================================

// ---------------------------------------------------------------------------
// 协议类型常量
// ---------------------------------------------------------------------------
#define _default_protocol_base  1

// 注册
#define _default_protocol_register_rq     (_default_protocol_base + 1)   // 2
#define _default_protocol_register_rs     (_default_protocol_base + 2)   // 3

// 登录
#define _default_protocol_login_rq        (_default_protocol_base + 3)   // 4
#define _default_protocol_login_rs        (_default_protocol_base + 4)   // 5

// 文件列表
#define _default_protocol_getfilelist_rq  (_default_protocol_base + 5)   // 6
#define _default_protocol_getfilelist_rs  (_default_protocol_base + 6)   // 7

// 上传
#define _default_protocol_uploadfileinfo_rq   (_default_protocol_base + 7)   // 8
#define _default_protocol_uploadfileinfo_rs   (_default_protocol_base + 8)   // 9
#define _default_protocol_uploadfileblock_rq  (_default_protocol_base + 9)   // 10
#define _default_protocol_uploadfileblock_rs  (_default_protocol_base + 10)  // 11

// 下载
#define _default_protocol_downloadfileinfo_rq   (_default_protocol_base + 11)  // 12
#define _default_protocol_downloadfileinfo_rs   (_default_protocol_base + 12)  // 13
#define _default_protocol_downloadfileblock_rq  (_default_protocol_base + 13)  // 14
#define _default_protocol_downloadfileblock_rs  (_default_protocol_base + 14)  // 15

// 搜索
#define _default_protocol_searchfile_rq  (_default_protocol_base + 15)  // 16
#define _default_protocol_searchfile_rs  (_default_protocol_base + 16)  // 17

// 删除
#define _default_protocol_deletefile_rq  (_default_protocol_base + 17)  // 18
#define _default_protocol_deletefile_rs  (_default_protocol_base + 18)  // 19

// 分享
#define _default_protocol_sharefile_rq   (_default_protocol_base + 19)  // 20
#define _default_protocol_sharefile_rs   (_default_protocol_base + 20)  // 21

// 撤销分享 (F10-4: 分享撤销)
#define _default_protocol_deleteshare_rq (_default_protocol_base + 34)  // 35
#define _default_protocol_deleteshare_rs (_default_protocol_base + 35)  // 36

// 稀疏指纹预检（秒传三层漏斗中的 L2 层）
#define _default_protocol_sparsecheck_rq (_default_protocol_base + 36)  // 37
#define _default_protocol_sparsecheck_rs (_default_protocol_base + 37)  // 38

// 提取文件 / GetFile
#define _default_protocol_getfile_rq     (_default_protocol_base + 21)  // 22
#define _default_protocol_getfile_rs     (_default_protocol_base + 22)  // 23

// AI — 智能预览 (Phase 3)
#define _default_protocol_aipreview_rq   (_default_protocol_base + 23)  // 24
#define _default_protocol_aipreview_rs   (_default_protocol_base + 24)  // 25

// AI — 语义搜索 (Phase 3)
#define _default_protocol_aisearch_rq    (_default_protocol_base + 25)  // 26
#define _default_protocol_aisearch_rs    (_default_protocol_base + 26)  // 27

// AI — 自动标签 (Phase 3)
#define _default_protocol_aitag_rq       (_default_protocol_base + 27)  // 28
#define _default_protocol_aitag_rs       (_default_protocol_base + 28)  // 29

// ---------------------------------------------------------------------------
// 大小常量
// ---------------------------------------------------------------------------
/// SQL 查询 / 响应文本缓冲区大小
#define SQLLEN  300

/// MAXSIZE=65 — 足够容纳 SHA-256 hex（64 字符 + 结尾空字符）。
/// ⚠️ 历史：曾为 45（无法容纳 SHA-256）。2026-08-08 修改。
#define MAXSIZE  65

/// 每个上传/下载块的最大文件内容（4 KB）
#define MAXFILECONTENT  4096

// ---------------------------------------------------------------------------
// 结果 / 状态码
// ---------------------------------------------------------------------------

// 注册结果
#define _register_err      0
#define _register_success  1

// 登录结果
#define _login_usernoexists  0
#define _login_password_err  1
#define _login_success       2
#define _login_invalid       3   ///< 统一的"凭据无效"结果 (F2-2 修复)

// 上传结果
#define _uploadfile_normal      0   ///< 普通上传
#define _uploadfile_continue    1   ///< 断点续传
#define _uploadfile_flash       2   ///< 秒传（去重命中）
#define _uploadfile_isuploaded  3   ///< 该用户已上传过此文件

// ---------------------------------------------------------------------------
// 基础协议结构体
// ---------------------------------------------------------------------------
/// 所有协议报文均继承自 STRUBASE。m_ntype 字段标识协议类型，
/// 且是每个报文载荷的第一个字节。
struct STRUBASE {
    char m_ntype;
};

// ---------------------------------------------------------------------------
// 1. 注册 (RQ:2, RS:3)
// ---------------------------------------------------------------------------
struct STRU_REGISTERRQ : public STRUBASE {
    STRU_REGISTERRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_register_rq);
    }
    int64_t m_tel;                         ///< 手机号码
    char    m_szName[MAXSIZE];             ///< 用户名
    char    m_szPasswordSHA256[65];        ///< SHA-256(password + salt) hex 字符串 (X2: 已去除明文)
};

struct STRU_REGISTERRS : public STRUBASE {
    STRU_REGISTERRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_register_rs);
    }
    char m_szResult;                       ///< _register_success 或 _register_err
};

// ---------------------------------------------------------------------------
// 2. 登录 (RQ:4, RS:5)
// ---------------------------------------------------------------------------
struct STRU_LOGINRQ : public STRUBASE {
    STRU_LOGINRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_login_rq);
    }
    char m_szName[MAXSIZE];                ///< 用户名
    char m_szPasswordSHA256[65];           ///< SHA-256(password + salt) hex 字符串 (X2: 已去除明文)
};

struct STRU_LOGINRS : public STRUBASE {
    STRU_LOGINRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_login_rs);
    }
    int64_t m_userId;                      ///< 登录成功时的用户 ID
    char    m_szResult;                    ///< _login_success / _login_password_err / _login_usernoexists
};

// ---------------------------------------------------------------------------
// 3. 获取文件列表 (RQ:6, RS:7)
// ---------------------------------------------------------------------------
struct STRU_GETFILELISTRQ : public STRUBASE {
    STRU_GETFILELISTRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_getfilelist_rq);
    }
    int64_t m_userId;                      ///< 请求方用户 ID
};

/// 文件元数据条目（用于列表与上传响应）
struct FILEINFO {
    char    m_szFileName[MAXSIZE];         ///< 文件名
    int64_t m_filesize;                    ///< 文件大小（字节）
    char    m_szFileUploadTime[MAXSIZE];   ///< 上传时间戳字符串
    int64_t m_fileID;                      ///< 数据库中的文件 ID（Phase 2：为删除/分享/下载新增）
};

struct STRU_GETFILELISTRS : public STRUBASE {
    STRU_GETFILELISTRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_getfilelist_rs);
    }
    FILEINFO m_aryFileInfo[MAXSIZE];       ///< 文件信息数组（最多 MAXSIZE=65 个条目）
    int      m_nFileNum;                   ///< 实际条目数量
};

// ---------------------------------------------------------------------------
// 4. 上传文件信息 (RQ:8, RS:9)
// ---------------------------------------------------------------------------
struct STRU_UPLOADFILEINFORQ : public STRUBASE {
    STRU_UPLOADFILEINFORQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_uploadfileinfo_rq);
    }
    int64_t  m_userId;                     ///< 上传用户 ID
    FILEINFO m_fileInfo;                   ///< 文件元数据
    char     m_szFileSHA256[65];           ///< SHA-256 hex 指纹
};

struct STRU_UPLOADFILEINFORS : public STRUBASE {
    STRU_UPLOADFILEINFORS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_uploadfileinfo_rs);
    }
    char    m_szFileName[MAXSIZE];         ///< 回显的文件名
    char    m_szFileSHA256[65];            ///< SHA-256 hex 指纹
    int64_t m_fileID;                      ///< 服务器分配的文件 ID
    int64_t m_pos;                         ///< 续传位置（用于断点续传）
    char    m_szResult;                    ///< _uploadfile_normal / _continue / _flash / _isuploaded
};

// ---------------------------------------------------------------------------
// 5. 上传文件块 (RQ:10, RS:11)
// ---------------------------------------------------------------------------
struct STRU_UPLOADFILEBLOCKRQ : public STRUBASE {
    STRU_UPLOADFILEBLOCKRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_uploadfileblock_rq);
    }
    int64_t m_userId;                      ///< 上传用户 ID
    int64_t m_fileID;                      ///< 目标文件 ID
    int32_t m_blockSeq;                    ///< 块序号 (F4-6 修复)
    char    m_szFileContent[MAXFILECONTENT]; ///< 块数据（最多 4 KB）
    int64_t m_fileblocksize;                ///< 本块实际字节数
};

struct STRU_UPLOADFILEBLOCKRS : public STRUBASE {
    STRU_UPLOADFILEBLOCKRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_uploadfileblock_rs);
    }
    int64_t m_fileID;                      ///< 确认的文件 ID
    int64_t m_pos;                         ///< 下一个期望的字节位置
    char    m_szResult;                    ///< 状态（0 = 成功，非 0 = 错误）
};

// ---------------------------------------------------------------------------
// 6. 下载文件信息 (RQ:12, RS:13)
// ---------------------------------------------------------------------------
struct STRU_DOWNLOADFILEINFORQ : public STRUBASE {
    STRU_DOWNLOADFILEINFORQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_downloadfileinfo_rq);
    }
    int64_t m_userId;                      ///< 请求方用户 ID
    int64_t m_fileID;                      ///< 目标文件 ID
    char    m_szFileName[MAXSIZE];         ///< 可选：文件名提示
};

struct STRU_DOWNLOADFILEINFORS : public STRUBASE {
    STRU_DOWNLOADFILEINFORS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_downloadfileinfo_rs);
    }
    int64_t m_fileID;                      ///< 文件 ID
    int64_t m_fileSize;                    ///< 文件总大小（字节）
    int     m_nBlockNum;                   ///< 需要下载的块数
    int64_t m_nBlockSize;                  ///< 每个块的大小（0 = 不分块）
    char    m_szFileSHA256[65];            ///< 用于完整性校验的 SHA-256
    char    m_szResult;                    ///< 0 = 成功，非 0 = 错误（文件不存在等）
};

// 向后兼容别名 — 旧代码使用 STRU_DOWNLOADFILERQ
typedef STRU_DOWNLOADFILEINFORQ STRU_DOWNLOADFILERQ;

// ---------------------------------------------------------------------------
// 7. 下载文件块 (RQ:14, RS:15)
// ---------------------------------------------------------------------------
struct STRU_DOWNLOADFILEBLOCKRQ : public STRUBASE {
    STRU_DOWNLOADFILEBLOCKRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_downloadfileblock_rq);
    }
    int64_t m_userId;                      ///< 请求方用户 ID
    int64_t m_fileID;                      ///< 目标文件 ID
    int64_t m_pos;                         ///< 请求块的字节偏移
};

struct STRU_DOWNLOADFILEBLOCKRS : public STRUBASE {
    STRU_DOWNLOADFILEBLOCKRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_downloadfileblock_rs);
    }
    int64_t m_fileID;                      ///< 文件 ID
    char    m_szFileContent[MAXFILECONTENT]; ///< 块数据（最多 4 KB）
    int64_t m_fileblocksize;                ///< 本块实际字节数
    int64_t m_pos;                         ///< 本块的字节偏移
    char    m_szResult;                    ///< 0 = 成功，非 0 = 错误
};

// ---------------------------------------------------------------------------
// 8. 搜索文件 (RQ:16, RS:17)
// ---------------------------------------------------------------------------
struct STRU_SEARCHFILERQ : public STRUBASE {
    STRU_SEARCHFILERQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_searchfile_rq);
    }
    int64_t m_userId;                      ///< 搜索用户 ID
    char    m_szSearchKey[MAXSIZE];        ///< 搜索关键字 / 文件名模式
};

struct STRU_SEARCHFILERS : public STRUBASE {
    STRU_SEARCHFILERS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_searchfile_rs);
    }
    FILEINFO m_aryFileInfo[MAXSIZE];       ///< 搜索结果文件条目
    int      m_nFileNum;                   ///< 结果数量
};

// ---------------------------------------------------------------------------
// 9. 删除文件 (RQ:18, RS:19)
// ---------------------------------------------------------------------------
struct STRU_DELETEFILERQ : public STRUBASE {
    STRU_DELETEFILERQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_deletefile_rq);
    }
    int64_t m_userId;                      ///< 请求方用户 ID
    int64_t m_fileID;                      ///< 目标文件 ID
};

struct STRU_DELETEFILERS : public STRUBASE {
    STRU_DELETEFILERS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_deletefile_rs);
    }
    int64_t m_fileID;                      ///< 被删除的文件 ID
    char    m_szResult;                    ///< 0 = 成功，非 0 = 错误
};

// ---------------------------------------------------------------------------
// 10. 分享文件 (RQ:20, RS:21)
// ---------------------------------------------------------------------------
struct STRU_SHAREFILERQ : public STRUBASE {
    STRU_SHAREFILERQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_sharefile_rq);
    }
    int64_t m_userId;                      ///< 分享用户 ID
    int64_t m_fileID;                      ///< 要分享的文件
    char    m_szShareToUser[MAXSIZE];      ///< 可选：目标用户名
};

struct STRU_SHAREFILERS : public STRUBASE {
    STRU_SHAREFILERS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_sharefile_rs);
    }
    int64_t m_fileID;                      ///< 已分享的文件 ID
    char    m_szShareCode[9];              ///< 生成的分享码（8 字符 + 结尾空字符）
    char    m_szResult;                    ///< 0 = 成功，非 0 = 错误
};

// ---------------------------------------------------------------------------
// 10.5 撤销分享 (RQ:35, RS:36) — F10-4: 分享撤销
// ---------------------------------------------------------------------------
struct STRU_DELETESHARERQ : public STRUBASE {
    STRU_DELETESHARERQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_deleteshare_rq);
    }
    int64_t m_userId;                      ///< 请求方用户 ID
    int64_t m_fileID;                      ///< 需要撤销分享的文件
};

struct STRU_DELETESHARERS : public STRUBASE {
    STRU_DELETESHARERS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_deleteshare_rs);
    }
    int64_t m_fileID;                      ///< 文件 ID
    char    m_szResult;                    ///< 1 = 已撤销，0 = 未找到分享 / 错误
};

// ---------------------------------------------------------------------------
// 10.6 稀疏指纹预检 (RQ:37, RS:38) — 上传漏斗中的 L2 层
// ---------------------------------------------------------------------------
/// 客户端发送稀疏指纹（头 4KB + 尾 4KB + 文件大小），询问服务器
/// 该文件是否已存在。若服务器判定"确定为新文件"，
/// 客户端可跳过计算整个文件的完整 SHA-256。
struct STRU_SPARSECHECKRQ : public STRUBASE {
    STRU_SPARSECHECKRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_sparsecheck_rq);
    }
    int64_t m_userId;                      ///< 用户 ID
    int64_t m_fileSize;                    ///< 文件大小（字节）
    char    m_szSparseFingerprint[65];     ///< SHA-256(头4KB + 尾4KB + 大小)
};

struct STRU_SPARSECHECKRS : public STRUBASE {
    STRU_SPARSECHECKRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_sparsecheck_rs);
    }
    char m_szResult;                       ///< 0 = 确定为新文件，1 = 可能存在
};

// ---------------------------------------------------------------------------
// 11. 获取文件 / 提取 (RQ:22, RS:23)
// ---------------------------------------------------------------------------
struct STRU_GETFILERQ : public STRUBASE {
    STRU_GETFILERQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_getfile_rq);
    }
    int64_t m_userId;                      ///< 提取用户 ID
    int64_t m_shareFileID;                 ///< 分享码或分享文件引用 ID
};

// 向后兼容别名 — 旧代码使用 STRU_EXTRACTFILERQ
typedef STRU_GETFILERQ STRU_EXTRACTFILERQ;

struct STRU_GETFILERS : public STRUBASE {
    STRU_GETFILERS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_getfile_rs);
    }
    FILEINFO m_fileInfo;                   ///< 提取的文件元数据
    char     m_szFileSHA256[65];           ///< SHA-256 指纹
    int64_t  m_fileID;                     ///< 分配给该用户的文件 ID
    int64_t  m_pos;                        ///< 续传位置（0 = 新文件）
    char     m_szResult;                   ///< 0 = 成功，非 0 = 错误
};

// ---------------------------------------------------------------------------
// 12. AI 智能预览 (RQ:24, RS:25) — Phase 3
// ---------------------------------------------------------------------------
/// 请求 AI 生成文件内容预览（摘要、关键词、关键句）
struct STRU_AIPREVIEWRQ : public STRUBASE {
    STRU_AIPREVIEWRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_aipreview_rq);
    }
    int64_t m_userId;                      ///< 请求方用户 ID
    int64_t m_fileID;                      ///< 目标文件 ID
};

/// 携带结构化分析的 AI 预览响应
struct STRU_AIPREVIEWRS : public STRUBASE {
    STRU_AIPREVIEWRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_aipreview_rs);
    }
    int64_t m_fileID;                      ///< 文件 ID
    char    m_szSummary[SQLLEN * 2];       ///< AI 生成的摘要（约 200 个中文字符）
    char    m_szKeywords[MAXSIZE * 4];     ///< 逗号分隔的关键词
    char    m_szKeySentences[SQLLEN * 2];  ///< 文件中的关键句
    char    m_szFileType[MAXSIZE];         ///< 检测到的文件类型
    // 新增字段：原始内容 + AI 错误展示
    char    m_szFileName[MAXSIZE];          ///< 用于对话框标题的文件名
    int32_t m_nRawContentLen;               ///< 原始内容中的有效字节数（0-8192）
    char    m_szRawContent[MAXFILECONTENT*2]; ///< 文件内容前 8KB
    char    m_szAIError[256];               ///< AI 错误原因，为空 = AI 正常
    char    m_szResult;                    ///< 0 = 成功，非 0 = AI 不可用 / 错误
};

// ---------------------------------------------------------------------------
// 13. AI 语义搜索 (RQ:26, RS:27) — Phase 3
// ---------------------------------------------------------------------------
/// 请求对用户文件进行语义（自然语言）搜索
struct STRU_AISEARCHRQ : public STRUBASE {
    STRU_AISEARCHRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_aisearch_rq);
    }
    int64_t m_userId;                      ///< 搜索用户 ID
    char    m_szQuery[SQLLEN];             ///< 自然语言查询字符串
};

/// 携带匹配说明的单条搜索结果条目
struct AI_SEARCH_RESULT {
    FILEINFO m_fileInfo;                   ///< 文件元数据
    char     m_szMatchReason[SQLLEN];      ///< 该文件匹配的原因（如"内容相似度 0.87"）
    char     m_szFileSHA256[65];           ///< 用于校验的 SHA-256 指纹
};

/// AI 语义搜索响应
struct STRU_AISEARCHRS : public STRUBASE {
    STRU_AISEARCHRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_aisearch_rs);
    }
    int             m_nResultNum;                          ///< 结果数量
    AI_SEARCH_RESULT m_aryResults[MAXSIZE];                 ///< 搜索结果条目
    char            m_szResult;                            ///< 0 = 成功，非 0 = AI 不可用 / 已降级
};

// ---------------------------------------------------------------------------
// 14. AI 自动标签 (RQ:28, RS:29) — Phase 3
// ---------------------------------------------------------------------------
/// 请求为文件生成 AI 标签（通常在上传后触发）
struct STRU_AITAGRQ : public STRUBASE {
    STRU_AITAGRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_aitag_rq);
    }
    int64_t m_userId;                      ///< 请求方用户 ID
    int64_t m_fileID;                      ///< 目标文件 ID
};

/// 携带建议标签的 AI 标签响应
struct STRU_AITAGRS : public STRUBASE {
    STRU_AITAGRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_aitag_rs);
    }
    int64_t m_fileID;                      ///< 文件 ID
    int     m_nTagNum;                     ///< 返回的标签数量
    char    m_szTags[15][MAXSIZE];         ///< 标签字符串（最多 15 个，每个长度 MAXSIZE）
    int     m_nNewTagSuggestions;          ///< 新标签建议数量
    char    m_szNewTags[5][MAXSIZE];       ///< 新建议的标签
    char    m_szResult;                    ///< 0 = 成功，非 0 = AI 不可用 / 错误
};

// ============================================================
// 流媒体令牌协议 (Phase 2: HTTP 视频/音频流)
// ============================================================
#define _default_protocol_streamtoken_rq       (_default_protocol_base + 32)  // 33
#define _default_protocol_streamtoken_rs       (_default_protocol_base + 33)  // 34

// ============================================================
// 集群协议常量 (Phase: 分布式 L2)
// ============================================================
#define _default_protocol_replicateblock_rq   (_default_protocol_base + 29)  // 30
#define _default_protocol_replicateblock_rs   (_default_protocol_base + 30)  // 31
#define _default_protocol_redirect_rs          (_default_protocol_base + 31)  // 32

// 重定向结果码
#define _redirect_permanent   0
#define _redirect_temporary   1

// --- 复制块 (对等节点间) ---
struct STRU_REPLICATEBLOCKRQ : public STRUBASE {
    STRU_REPLICATEBLOCKRQ() { memset(this, 0, sizeof(*this)); m_ntype = static_cast<char>(_default_protocol_replicateblock_rq); }
    int64_t m_fileId;
    int     m_blockSeq;
    int64_t m_offset;       // 目标 blocks.dat 中的偏移
    int64_t m_dataLen;
    char    m_szData[MAXFILECONTENT];
};

struct STRU_REPLICATEBLOCKRS : public STRUBASE {
    STRU_REPLICATEBLOCKRS() { memset(this, 0, sizeof(*this)); m_ntype = static_cast<char>(_default_protocol_replicateblock_rs); }
    int64_t m_fileId;
    int     m_blockSeq;
    char    m_szResult;     // 0=失败, 1=成功
};

// --- 流媒体令牌请求（客户端 → 服务器，请求 HTTP 流媒体 URL）---
struct STRU_STREAMTOKENRQ : public STRUBASE {
    STRU_STREAMTOKENRQ() { memset(this, 0, sizeof(*this)); m_ntype = static_cast<char>(_default_protocol_streamtoken_rq); }
    int64_t m_userId;      // 请求方用户 ID
    int64_t m_fileID;      // 要流式播放的目标文件 ID
};

// --- 流媒体令牌响应（服务器 → 客户端，返回 HTTP URL 参数）---
struct STRU_STREAMTOKENRS : public STRUBASE {
    STRU_STREAMTOKENRS() { memset(this, 0, sizeof(*this)); m_ntype = static_cast<char>(_default_protocol_streamtoken_rs); }
    int64_t m_fileID;          // 文件 ID
    char    m_szToken[65];     // SHA-256 流媒体令牌（64 个十六进制字符 + 结尾空字符）
    int64_t m_nTimestamp;      // 令牌生成时的 Unix 时间戳（用于 URL）
    int32_t m_nHttpPort;       // HTTP 服务器端口（默认 8900）
    char    m_szFileName[260]; // 原始文件名（用于 MIME 类型检测）
    int64_t m_fileSize;        // 文件总大小（字节）
    char    m_szResult;        // 0 = 成功，非 0 = 错误（文件不存在等）
};

// --- 重定向响应（服务器通知客户端重新连接）---
struct STRU_REDIRECTRS : public STRUBASE {
    STRU_REDIRECTRS() { memset(this, 0, sizeof(*this)); m_ntype = static_cast<char>(_default_protocol_redirect_rs); }
    char m_szRedirectIP[16];     // 目标服务器 IP
    int  m_nRedirectPort;        // 目标服务器端口
    char m_szResult;             // _redirect_permanent 或 _redirect_temporary
};

#endif // PACKDEF_H
