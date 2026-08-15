#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include "kernel/tcpkernel.h"
#include "login1.h"
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
    // Phase 2: Download, Delete, Share, Extract
    void slot_downloadinfors(const STRU_DOWNLOADFILEINFORS&);
    void slot_downloadblockrs(const STRU_DOWNLOADFILEBLOCKRS&);
    void slot_deleters(const STRU_DELETEFILERS&);
    void slot_sharers(const STRU_SHAREFILERS&);
    void slot_getfilers(const STRU_GETFILERS&);
    // Cluster: redirect slot
    void slot_redirect(const STRU_REDIRECTRS&);

    // Phase 2: HTTP streaming token slot
    void slot_streamtoken(const STRU_STREAMTOKENRS&);

    // Phase 3: AI slots
    void slot_aipreview(const STRU_AIPREVIEWRS&);
    void slot_aisearch(const STRU_AISEARCHRS&);
    void slot_aitag(const STRU_AITAGRS&);

    // Upload block ACK (X3 fix)
    void slot_uploadfileblockrs(const STRU_UPLOADFILEBLOCKRS&);
    // Delete Share (F10-4)
    void slot_deletesharers(const STRU_DELETESHARERS&);
    // L2 Sparse fingerprint pre-check (upload funnel)
    void slot_sparsecheckrs(const STRU_SPARSECHECKRS&);
private slots:
    void on_pushButton_clicked();
    void on_pushButton_2_clicked();
    void on_pushButton_3_clicked();
    void on_pushButton_4_clicked();
    void on_pushButton_5_clicked();
    void on_pushButton_6_clicked();
    void on_pushButton_7_clicked();  // F10-4: Revoke share button

private:
    Ui::Widget *ui;
    Ikernel *m_pKernel;
    login1*m_login;
    long long Userid;
    std::list<STRU_FILEINFO*> m_lstfileinfo;
    long m_fileNum;

    // Phase 2: Download state
    int64_t m_downloadFileId;
    QString m_downloadFileName;
    int64_t m_downloadFileSize;
    int     m_downloadTotalBlocks;
    int     m_downloadBlockSize;
    int     m_downloadReceivedBlocks;
    FILE*   m_downloadFile;
    void    requestDownloadBlock(int64_t pos);

    // Phase 2: File ID mapping (filename → fileID)
    std::map<std::string, int64_t> m_fileIdMap;

    // Multi-batch file list: track cumulative row offset across batches
    int      m_fileListRowOffset;

    // X3 fix: Block ACK tracking for upload fire-and-forget
    int      m_uploadTotalBlocks;
    int      m_uploadReceivedAcks;
    QString  m_uploadFileName;       // Pending upload file name for display

    // X5 fix: SHA-256 verification after download
    QString  m_downloadSavePath;
    QString  m_downloadExpectedSHA256;

    // L2 sparsecheck pending upload context (saved while waiting for server response)
    QString  m_pendingUploadPath;
    QString  m_pendingUploadName;
    long long m_pendingUploadSize;
    int64_t  m_pendingUploadMtime;
    QString  m_pendingUploadTime;
};
#endif // WIDGET_H
