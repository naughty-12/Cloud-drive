#ifndef PACKDEF_H
#define PACKDEF_H

#include <cstdint>
#include <cstring>

// ============================================================================
// Packdef.h — Unified Protocol Definition for 0323 Cloud Disk
// ============================================================================
// This is the SINGLE authoritative protocol definition file, replacing the
// separate copies previously in 0323client/ and 0323server/.
//
// All structs inherit from STRUBASE (m_ntype = protocol type identifier).
// Integer fields use int64_t for portability and clarity.
// SHA-256 fields use explicit [65] arrays (64 hex chars + null terminator).
//
// ⚠️ MAXSIZE=45 is too small for SHA-256 hex (64 chars).
//    For SHA-256 fields, use explicit `[65]` arrays.
//    Future: migrate to BinaryStream dynamic strings.
// ============================================================================

// ---------------------------------------------------------------------------
// Protocol Type Constants
// ---------------------------------------------------------------------------
#define _default_protocol_base  1

// Registration
#define _default_protocol_register_rq     (_default_protocol_base + 1)   // 2
#define _default_protocol_register_rs     (_default_protocol_base + 2)   // 3

// Login
#define _default_protocol_login_rq        (_default_protocol_base + 3)   // 4
#define _default_protocol_login_rs        (_default_protocol_base + 4)   // 5

// File List
#define _default_protocol_getfilelist_rq  (_default_protocol_base + 5)   // 6
#define _default_protocol_getfilelist_rs  (_default_protocol_base + 6)   // 7

// Upload
#define _default_protocol_uploadfileinfo_rq   (_default_protocol_base + 7)   // 8
#define _default_protocol_uploadfileinfo_rs   (_default_protocol_base + 8)   // 9
#define _default_protocol_uploadfileblock_rq  (_default_protocol_base + 9)   // 10
#define _default_protocol_uploadfileblock_rs  (_default_protocol_base + 10)  // 11

// Download
#define _default_protocol_downloadfileinfo_rq   (_default_protocol_base + 11)  // 12
#define _default_protocol_downloadfileinfo_rs   (_default_protocol_base + 12)  // 13
#define _default_protocol_downloadfileblock_rq  (_default_protocol_base + 13)  // 14
#define _default_protocol_downloadfileblock_rs  (_default_protocol_base + 14)  // 15

// Search
#define _default_protocol_searchfile_rq  (_default_protocol_base + 15)  // 16
#define _default_protocol_searchfile_rs  (_default_protocol_base + 16)  // 17

// Delete
#define _default_protocol_deletefile_rq  (_default_protocol_base + 17)  // 18
#define _default_protocol_deletefile_rs  (_default_protocol_base + 18)  // 19

// Share
#define _default_protocol_sharefile_rq   (_default_protocol_base + 19)  // 20
#define _default_protocol_sharefile_rs   (_default_protocol_base + 20)  // 21

// Delete Share (F10-4: share revocation)
#define _default_protocol_deleteshare_rq (_default_protocol_base + 34)  // 35
#define _default_protocol_deleteshare_rs (_default_protocol_base + 35)  // 36

// Sparse Fingerprint Pre-check (L2 in three-tier instant-upload funnel)
#define _default_protocol_sparsecheck_rq (_default_protocol_base + 36)  // 37
#define _default_protocol_sparsecheck_rs (_default_protocol_base + 37)  // 38

// Extract / GetFile
#define _default_protocol_getfile_rq     (_default_protocol_base + 21)  // 22
#define _default_protocol_getfile_rs     (_default_protocol_base + 22)  // 23

// AI — Smart Preview (Phase 3)
#define _default_protocol_aipreview_rq   (_default_protocol_base + 23)  // 24
#define _default_protocol_aipreview_rs   (_default_protocol_base + 24)  // 25

// AI — Semantic Search (Phase 3)
#define _default_protocol_aisearch_rq    (_default_protocol_base + 25)  // 26
#define _default_protocol_aisearch_rs    (_default_protocol_base + 26)  // 27

// AI — Auto Tagging (Phase 3)
#define _default_protocol_aitag_rq       (_default_protocol_base + 27)  // 28
#define _default_protocol_aitag_rs       (_default_protocol_base + 28)  // 29

// ---------------------------------------------------------------------------
// Size Constants
// ---------------------------------------------------------------------------
/// SQL query / response text buffer size
#define SQLLEN  300

/// MAXSIZE=65 — large enough for SHA-256 hex (64 chars + null).
/// ⚠️ Historical: was 45 (too small for SHA-256). Changed 2026-08-08.
#define MAXSIZE  65

/// Max file content per upload/download block (4 KB)
#define MAXFILECONTENT  4096

// ---------------------------------------------------------------------------
// Result / Status Codes
// ---------------------------------------------------------------------------

// Register results
#define _register_err      0
#define _register_success  1

// Login results
#define _login_usernoexists  0
#define _login_password_err  1
#define _login_success       2
#define _login_invalid       3   ///< Unified "invalid credentials" (F2-2 fix)

// Upload results
#define _uploadfile_normal      0   ///< Normal upload
#define _uploadfile_continue    1   ///< Resume upload (breakpoint)
#define _uploadfile_flash       2   ///< Instant upload (dedup match)
#define _uploadfile_isuploaded  3   ///< Already uploaded by this user

// ---------------------------------------------------------------------------
// Base Protocol Struct
// ---------------------------------------------------------------------------
/// All protocol packets inherit from STRUBASE. The m_ntype field identifies
/// the protocol type and is the first byte of every packet payload.
struct STRUBASE {
    char m_ntype;
};

// ---------------------------------------------------------------------------
// 1. Register (RQ:2, RS:3)
// ---------------------------------------------------------------------------
struct STRU_REGISTERRQ : public STRUBASE {
    STRU_REGISTERRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_register_rq);
    }
    int64_t m_tel;                         ///< Phone number
    char    m_szName[MAXSIZE];             ///< Username
    char    m_szPasswordSHA256[65];        ///< SHA-256(password + salt) hex string (X2: plaintext removed)
};

struct STRU_REGISTERRS : public STRUBASE {
    STRU_REGISTERRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_register_rs);
    }
    char m_szResult;                       ///< _register_success or _register_err
};

// ---------------------------------------------------------------------------
// 2. Login (RQ:4, RS:5)
// ---------------------------------------------------------------------------
struct STRU_LOGINRQ : public STRUBASE {
    STRU_LOGINRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_login_rq);
    }
    char m_szName[MAXSIZE];                ///< Username
    char m_szPasswordSHA256[65];           ///< SHA-256(password + salt) hex string (X2: plaintext removed)
};

struct STRU_LOGINRS : public STRUBASE {
    STRU_LOGINRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_login_rs);
    }
    int64_t m_userId;                      ///< User ID on success
    char    m_szResult;                    ///< _login_success / _login_password_err / _login_usernoexists
};

// ---------------------------------------------------------------------------
// 3. Get File List (RQ:6, RS:7)
// ---------------------------------------------------------------------------
struct STRU_GETFILELISTRQ : public STRUBASE {
    STRU_GETFILELISTRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_getfilelist_rq);
    }
    int64_t m_userId;                      ///< Requesting user ID
};

/// File metadata entry (used in list and upload responses)
struct FILEINFO {
    char    m_szFileName[MAXSIZE];         ///< File name
    int64_t m_filesize;                    ///< File size in bytes
    char    m_szFileUploadTime[MAXSIZE];   ///< Upload timestamp string
    int64_t m_fileID;                      ///< File ID in database (Phase 2: added for delete/share/download)
};

struct STRU_GETFILELISTRS : public STRUBASE {
    STRU_GETFILELISTRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_getfilelist_rs);
    }
    FILEINFO m_aryFileInfo[MAXSIZE];       ///< Array of file entries (max 45)
    int      m_nFileNum;                   ///< Actual number of entries
};

// ---------------------------------------------------------------------------
// 4. Upload File Info (RQ:8, RS:9)
// ---------------------------------------------------------------------------
struct STRU_UPLOADFILEINFORQ : public STRUBASE {
    STRU_UPLOADFILEINFORQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_uploadfileinfo_rq);
    }
    int64_t  m_userId;                     ///< Uploading user ID
    FILEINFO m_fileInfo;                   ///< File metadata
    char     m_szFileSHA256[65];           ///< SHA-256 hex fingerprint
};

struct STRU_UPLOADFILEINFORS : public STRUBASE {
    STRU_UPLOADFILEINFORS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_uploadfileinfo_rs);
    }
    char    m_szFileName[MAXSIZE];         ///< File name echoed back
    char    m_szFileSHA256[65];            ///< SHA-256 hex fingerprint
    int64_t m_fileID;                      ///< Assigned file ID on server
    int64_t m_pos;                         ///< Resume position (for breakpoint)
    char    m_szResult;                    ///< _uploadfile_normal / _continue / _flash / _isuploaded
};

// ---------------------------------------------------------------------------
// 5. Upload File Block (RQ:10, RS:11)
// ---------------------------------------------------------------------------
struct STRU_UPLOADFILEBLOCKRQ : public STRUBASE {
    STRU_UPLOADFILEBLOCKRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_uploadfileblock_rq);
    }
    int64_t m_userId;                      ///< Uploading user ID
    int64_t m_fileID;                      ///< Target file ID
    int32_t m_blockSeq;                    ///< Block sequence number (F4-6 fix)
    char    m_szFileContent[MAXFILECONTENT]; ///< Block data (up to 4 KB)
    int64_t m_fileblocksize;                ///< Actual bytes in this block
};

struct STRU_UPLOADFILEBLOCKRS : public STRUBASE {
    STRU_UPLOADFILEBLOCKRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_uploadfileblock_rs);
    }
    int64_t m_fileID;                      ///< File ID acknowledged
    int64_t m_pos;                         ///< Next expected byte position
    char    m_szResult;                    ///< Status (0 = ok, nonzero = error)
};

// ---------------------------------------------------------------------------
// 6. Download File Info (RQ:12, RS:13)
// ---------------------------------------------------------------------------
struct STRU_DOWNLOADFILEINFORQ : public STRUBASE {
    STRU_DOWNLOADFILEINFORQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_downloadfileinfo_rq);
    }
    int64_t m_userId;                      ///< Requesting user ID
    int64_t m_fileID;                      ///< Target file ID
    char    m_szFileName[MAXSIZE];         ///< Optional: file name hint
};

struct STRU_DOWNLOADFILEINFORS : public STRUBASE {
    STRU_DOWNLOADFILEINFORS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_downloadfileinfo_rs);
    }
    int64_t m_fileID;                      ///< File ID
    int64_t m_fileSize;                    ///< Total file size in bytes
    int     m_nBlockNum;                   ///< Number of blocks to download
    int64_t m_nBlockSize;                  ///< Size of each block (0 = not chunked)
    char    m_szFileSHA256[65];            ///< SHA-256 for integrity verification
    char    m_szResult;                    ///< 0 = ok, nonzero = error (file not found, etc.)
};

// Backward compatibility alias — old code used STRU_DOWNLOADFILERQ
typedef STRU_DOWNLOADFILEINFORQ STRU_DOWNLOADFILERQ;

// ---------------------------------------------------------------------------
// 7. Download File Block (RQ:14, RS:15)
// ---------------------------------------------------------------------------
struct STRU_DOWNLOADFILEBLOCKRQ : public STRUBASE {
    STRU_DOWNLOADFILEBLOCKRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_downloadfileblock_rq);
    }
    int64_t m_userId;                      ///< Requesting user ID
    int64_t m_fileID;                      ///< Target file ID
    int64_t m_pos;                         ///< Byte offset of requested block
};

struct STRU_DOWNLOADFILEBLOCKRS : public STRUBASE {
    STRU_DOWNLOADFILEBLOCKRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_downloadfileblock_rs);
    }
    int64_t m_fileID;                      ///< File ID
    char    m_szFileContent[MAXFILECONTENT]; ///< Block data (up to 4 KB)
    int64_t m_fileblocksize;                ///< Actual bytes in this block
    int64_t m_pos;                         ///< Byte offset of this block
    char    m_szResult;                    ///< 0 = ok, nonzero = error
};

// ---------------------------------------------------------------------------
// 8. Search File (RQ:16, RS:17)
// ---------------------------------------------------------------------------
struct STRU_SEARCHFILERQ : public STRUBASE {
    STRU_SEARCHFILERQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_searchfile_rq);
    }
    int64_t m_userId;                      ///< Searching user ID
    char    m_szSearchKey[MAXSIZE];        ///< Search keyword / filename pattern
};

struct STRU_SEARCHFILERS : public STRUBASE {
    STRU_SEARCHFILERS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_searchfile_rs);
    }
    FILEINFO m_aryFileInfo[MAXSIZE];       ///< Search result file entries
    int      m_nFileNum;                   ///< Number of results
};

// ---------------------------------------------------------------------------
// 9. Delete File (RQ:18, RS:19)
// ---------------------------------------------------------------------------
struct STRU_DELETEFILERQ : public STRUBASE {
    STRU_DELETEFILERQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_deletefile_rq);
    }
    int64_t m_userId;                      ///< Requesting user ID
    int64_t m_fileID;                      ///< Target file ID
};

struct STRU_DELETEFILERS : public STRUBASE {
    STRU_DELETEFILERS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_deletefile_rs);
    }
    int64_t m_fileID;                      ///< Deleted file ID
    char    m_szResult;                    ///< 0 = success, nonzero = error
};

// ---------------------------------------------------------------------------
// 10. Share File (RQ:20, RS:21)
// ---------------------------------------------------------------------------
struct STRU_SHAREFILERQ : public STRUBASE {
    STRU_SHAREFILERQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_sharefile_rq);
    }
    int64_t m_userId;                      ///< Sharing user ID
    int64_t m_fileID;                      ///< File to share
    char    m_szShareToUser[MAXSIZE];      ///< Optional: target username
};

struct STRU_SHAREFILERS : public STRUBASE {
    STRU_SHAREFILERS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_sharefile_rs);
    }
    int64_t m_fileID;                      ///< Shared file ID
    char    m_szShareCode[9];              ///< Generated share code (8 chars + null)
    char    m_szResult;                    ///< 0 = success, nonzero = error
};

// ---------------------------------------------------------------------------
// 10.5 Delete Share (RQ:35, RS:36) — F10-4: share revocation
// ---------------------------------------------------------------------------
struct STRU_DELETESHARERQ : public STRUBASE {
    STRU_DELETESHARERQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_deleteshare_rq);
    }
    int64_t m_userId;                      ///< Requesting user ID
    int64_t m_fileID;                      ///< File whose share to revoke
};

struct STRU_DELETESHARERS : public STRUBASE {
    STRU_DELETESHARERS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_deleteshare_rs);
    }
    int64_t m_fileID;                      ///< File ID
    char    m_szResult;                    ///< 1 = revoked, 0 = no share found / error
};

// ---------------------------------------------------------------------------
// 10.6 Sparse Fingerprint Pre-check (RQ:37, RS:38) — L2 in upload funnel
// ---------------------------------------------------------------------------
/// Client sends sparse fingerprint (head 4KB + tail 4KB + file size) to ask
/// server whether this file is already known. If server says "definitely new",
/// client can skip computing the full SHA-256 of the entire file.
struct STRU_SPARSECHECKRQ : public STRUBASE {
    STRU_SPARSECHECKRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_sparsecheck_rq);
    }
    int64_t m_userId;                      ///< User ID
    int64_t m_fileSize;                    ///< File size in bytes
    char    m_szSparseFingerprint[65];     ///< SHA-256(head4KB + tail4KB + size)
};

struct STRU_SPARSECHECKRS : public STRUBASE {
    STRU_SPARSECHECKRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_sparsecheck_rs);
    }
    char m_szResult;                       ///< 0 = definitely new, 1 = might exist
};

// ---------------------------------------------------------------------------
// 11. Get File / Extract (RQ:22, RS:23)
// ---------------------------------------------------------------------------
struct STRU_GETFILERQ : public STRUBASE {
    STRU_GETFILERQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_getfile_rq);
    }
    int64_t m_userId;                      ///< Extracting user ID
    int64_t m_shareFileID;                 ///< Share code or shared file reference ID
};

// Backward compatibility alias — old code used STRU_EXTRACTFILERQ
typedef STRU_GETFILERQ STRU_EXTRACTFILERQ;

struct STRU_GETFILERS : public STRUBASE {
    STRU_GETFILERS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_getfile_rs);
    }
    FILEINFO m_fileInfo;                   ///< Extracted file metadata
    char     m_szFileSHA256[65];           ///< SHA-256 fingerprint
    int64_t  m_fileID;                     ///< Assigned file ID for this user
    int64_t  m_pos;                        ///< Resume position (0 = new)
    char     m_szResult;                   ///< 0 = success, nonzero = error
};

// ---------------------------------------------------------------------------
// 12. AI Preview (RQ:24, RS:25) — Phase 3
// ---------------------------------------------------------------------------
/// Request AI-powered file content preview (summary, keywords, key sentences)
struct STRU_AIPREVIEWRQ : public STRUBASE {
    STRU_AIPREVIEWRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_aipreview_rq);
    }
    int64_t m_userId;                      ///< Requesting user ID
    int64_t m_fileID;                      ///< Target file ID
};

/// AI preview response with structured analysis
struct STRU_AIPREVIEWRS : public STRUBASE {
    STRU_AIPREVIEWRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_aipreview_rs);
    }
    int64_t m_fileID;                      ///< File ID
    char    m_szSummary[SQLLEN * 2];       ///< AI-generated summary (~200 chars Chinese)
    char    m_szKeywords[MAXSIZE * 4];     ///< Comma-separated keywords
    char    m_szKeySentences[SQLLEN * 2];  ///< Key sentences from the file
    char    m_szFileType[MAXSIZE];         ///< Detected file type
    // New fields for raw content + AI error display
    char    m_szFileName[MAXSIZE];          ///< File name for dialog title
    int32_t m_nRawContentLen;               ///< Valid bytes in raw content (0-8192)
    char    m_szRawContent[MAXFILECONTENT*2]; ///< File content first 8KB
    char    m_szAIError[256];               ///< AI error reason, empty = AI OK
    char    m_szResult;                    ///< 0 = success, nonzero = AI unavailable / error
};

// ---------------------------------------------------------------------------
// 13. AI Semantic Search (RQ:26, RS:27) — Phase 3
// ---------------------------------------------------------------------------
/// Request semantic (natural language) search over user's files
struct STRU_AISEARCHRQ : public STRUBASE {
    STRU_AISEARCHRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_aisearch_rq);
    }
    int64_t m_userId;                      ///< Searching user ID
    char    m_szQuery[SQLLEN];             ///< Natural language query string
};

/// Single search result entry with match explanation
struct AI_SEARCH_RESULT {
    FILEINFO m_fileInfo;                   ///< File metadata
    char     m_szMatchReason[SQLLEN];      ///< Why this file matched (e.g., "content similarity 0.87")
    char     m_szFileSHA256[65];           ///< SHA-256 fingerprint for verification
};

/// AI semantic search response
struct STRU_AISEARCHRS : public STRUBASE {
    STRU_AISEARCHRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_aisearch_rs);
    }
    int             m_nResultNum;                          ///< Number of results
    AI_SEARCH_RESULT m_aryResults[MAXSIZE];                 ///< Search result entries
    char            m_szResult;                            ///< 0 = success, nonzero = AI unavailable / fallback
};

// ---------------------------------------------------------------------------
// 14. AI Auto Tag (RQ:28, RS:29) — Phase 3
// ---------------------------------------------------------------------------
/// Request AI-generated tags for a file (usually triggered after upload)
struct STRU_AITAGRQ : public STRUBASE {
    STRU_AITAGRQ() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_aitag_rq);
    }
    int64_t m_userId;                      ///< Requesting user ID
    int64_t m_fileID;                      ///< Target file ID
};

/// AI tag response with suggested labels
struct STRU_AITAGRS : public STRUBASE {
    STRU_AITAGRS() {
        memset(this, 0, sizeof(*this));
        m_ntype = static_cast<char>(_default_protocol_aitag_rs);
    }
    int64_t m_fileID;                      ///< File ID
    int     m_nTagNum;                     ///< Number of tags returned
    char    m_szTags[15][MAXSIZE];         ///< Tag strings (max 15 tags of MAXSIZE each)
    int     m_nNewTagSuggestions;          ///< Number of new tag suggestions
    char    m_szNewTags[5][MAXSIZE];       ///< Newly suggested tags
    char    m_szResult;                    ///< 0 = success, nonzero = AI unavailable / error
};

// ============================================================
// Streaming Token Protocol (Phase 2: HTTP video/audio streaming)
// ============================================================
#define _default_protocol_streamtoken_rq       (_default_protocol_base + 32)  // 33
#define _default_protocol_streamtoken_rs       (_default_protocol_base + 33)  // 34

// ============================================================
// Cluster Protocol Constants (Phase: Distributed L2)
// ============================================================
#define _default_protocol_replicate_block_rq   (_default_protocol_base + 29)  // 30
#define _default_protocol_replicate_block_rs   (_default_protocol_base + 30)  // 31
#define _default_protocol_redirect_rs          (_default_protocol_base + 31)  // 32

// Redirect result codes
#define _redirect_permanent   0
#define _redirect_temporary   1

// --- Replicate Block (peer-to-peer) ---
struct STRU_REPLICATEBLOCKRQ : public STRUBASE {
    STRU_REPLICATEBLOCKRQ() { memset(this, 0, sizeof(*this)); m_ntype = static_cast<char>(_default_protocol_replicate_block_rq); }
    int64_t m_fileId;
    int     m_blockSeq;
    int64_t m_offset;       // offset in target blocks.dat
    int64_t m_dataLen;
    char    m_szData[MAXFILECONTENT];
};

struct STRU_REPLICATEBLOCKRS : public STRUBASE {
    STRU_REPLICATEBLOCKRS() { memset(this, 0, sizeof(*this)); m_ntype = static_cast<char>(_default_protocol_replicate_block_rs); }
    int64_t m_fileId;
    int     m_blockSeq;
    char    m_szResult;     // 0=fail, 1=success
};

// --- Stream Token Request (client → server, ask for HTTP streaming URL) ---
struct STRU_STREAMTOKENRQ : public STRUBASE {
    STRU_STREAMTOKENRQ() { memset(this, 0, sizeof(*this)); m_ntype = static_cast<char>(_default_protocol_streamtoken_rq); }
    int64_t m_userId;      // Requesting user ID
    int64_t m_fileID;      // Target file ID to stream
};

// --- Stream Token Response (server → client, returns HTTP URL params) ---
struct STRU_STREAMTOKENRS : public STRUBASE {
    STRU_STREAMTOKENRS() { memset(this, 0, sizeof(*this)); m_ntype = static_cast<char>(_default_protocol_streamtoken_rs); }
    int64_t m_fileID;          // File ID
    char    m_szToken[65];     // SHA-256 streaming token (64 hex chars + null)
    int64_t m_nTimestamp;      // Unix timestamp when token was generated (for URL)
    int32_t m_nHttpPort;       // HTTP server port (default 8900)
    char    m_szFileName[260]; // Original file name (for MIME type detection)
    int64_t m_fileSize;        // Total file size in bytes
    char    m_szResult;        // 0 = success, nonzero = error (file not found, etc.)
};

// --- Redirect Response (server tells client to reconnect) ---
struct STRU_REDIRECTRS : public STRUBASE {
    STRU_REDIRECTRS() { memset(this, 0, sizeof(*this)); m_ntype = static_cast<char>(_default_protocol_redirect_rs); }
    char m_szRedirectIP[16];     // target server IP
    int  m_nRedirectPort;        // target server port
    char m_szResult;             // _redirect_permanent or _redirect_temporary
};

#endif // PACKDEF_H
