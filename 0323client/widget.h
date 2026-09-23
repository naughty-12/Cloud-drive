#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include "kernel/tcpkernel.h"
#include "login1.h"
#include "TagCloud.h"
#include "Packdef.h"
#include "ProtocolFactory.h"
#include <QTime>
#include <QDateTime>
#include <qdebug.h>
#include <QDebug>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QClipboard>
#include <map>
#include <cstdio>
QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE
struct STRU_FILEINFO{
    char m_szFileName[MAXSIZE];
    char m_szFilePath[260];
    char m_szFileSHA256[MAXSIZE];
    char m_szFileUploadTime[MAXSIZE];
    long long m_filepos;
    long long m_filesize;
    long long m_fileid;
};

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();
public slots:
    void slot_loginrs(const STRU_LOGINRS&);
    void slot_getfilelistrs(const STRU_GETFILELISTRS& psg);
    void slot_uploadfileinfors(const STRU_UPLOADFILEINFORS&);
    // Phase 2：下载、删除、分享、提取
    void slot_downloadinfors(const STRU_DOWNLOADFILEINFORS&);
    void slot_downloadblockrs(const STRU_DOWNLOADFILEBLOCKRS&);
    void slot_deleters(const STRU_DELETEFILERS&);
    void slot_sharers(const STRU_SHAREFILERS&);
    void slot_getfilers(const STRU_GETFILERS&);
    // Cluster：重定向槽
    void slot_redirect(const STRU_REDIRECTRS&);

    // Phase 2：HTTP 流媒体令牌槽
    void slot_streamtoken(const STRU_STREAMTOKENRS&);

    // Phase 3：AI 槽
    void slot_aipreview(const STRU_AIPREVIEWRS&);
    void slot_aisearch(const STRU_AISEARCHRS&);
    void slot_aitag(const STRU_AITAGRS&);

    // 上传块 ACK（X3 修复）
    void slot_uploadfileblockrs(const STRU_UPLOADFILEBLOCKRS&);
    // 撤销分享（F10-4）
    void slot_deletesharers(const STRU_DELETESHARERS&);
    // L2 稀疏指纹预检（上传漏斗）
    void slot_sparsecheckrs(const STRU_SPARSECHECKRS&);
private slots:
    void on_pushButton_clicked();
    void on_pushButton_2_clicked();
    void on_pushButton_3_clicked();
    void on_pushButton_4_clicked();
    void on_pushButton_5_clicked();
    void on_pushButton_6_clicked();
    void on_pushButton_7_clicked();  // F10-4：撤销分享按钮
    // 标签云点击 → 过滤文件列表（tag 为空串 = 取消过滤）
    void onTagClicked(const QString& tag);

private:
    Ui::Widget *ui;
    Ikernel *m_pKernel;
    login1*m_login;
    long long Userid;
    std::list<STRU_FILEINFO*> m_lstfileinfo;
    long m_fileNum;
    // Phase 3：AI 标签云（会话内聚合 AITagRS）
    TagCloud* m_tagCloud;

    // Phase 2：下载状态
    int64_t m_downloadFileId;
    QString m_downloadFileName;
    int64_t m_downloadFileSize;
    int     m_downloadTotalBlocks;
    int     m_downloadBlockSize;
    int     m_downloadReceivedBlocks;
    FILE*   m_downloadFile;
    void    requestDownloadBlock(int64_t pos);

    // Phase 2：文件 ID 映射（文件名 → fileID）
    std::map<std::string, int64_t> m_fileIdMap;

    // 多批次文件列表：跨批次累计行偏移量
    int      m_fileListRowOffset;

    // X3 修复：上传"发后即忘"模式的块 ACK 追踪
    int      m_uploadTotalBlocks;
    int      m_uploadReceivedAcks;
    QString  m_uploadFileName;       // 待上传文件名（用于显示）

    // X5 修复：下载后 SHA-256 校验
    QString  m_downloadSavePath;
    QString  m_downloadExpectedSHA256;

    // L2 稀疏指纹预检的挂起上传上下文（等待服务器响应期间暂存）
    QString  m_pendingUploadPath;
    QString  m_pendingUploadName;
    long long m_pendingUploadSize;
    int64_t  m_pendingUploadMtime;
    QString  m_pendingUploadTime;
};
#endif // WIDGET_H
