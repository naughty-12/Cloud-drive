#include "widget.h"
#include "ui_widget.h"
#include "ProtocolFactory.h"
#include "QFile"
#include <QDesktopServices>
#include <QProcess>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>
#include <QClipboard>
#include <QApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <string.h>
#include <cstring>
#include "cache/UploadCache.h"
#include "CryptoUtil.h"          // shared/crypto 共享模块

// ============================================================================
// 播放器路径解析（跨平台，且不含任何 #ifdef）
// ============================================================================
// 优先级：环境变量 VLC_PATH > 用户设置(QSettings player/vlcPath)
//         > PATH 搜索 > 各平台常见安装位置的候选列表 > 交给系统默认程序
//
// 候选列表在所有平台上统一尝试，靠 QFile::exists 过滤 —— 所以不需要
// 平台条件编译：Windows 上 /usr/bin/vlc 自然不存在，Linux 上 C:\... 也不存在。
// 找不到任何播放器时返回空串，由调用方回退到 QDesktopServices::openUrl。
static QString resolvePreferredPlayer()
{
    // 1) 环境变量（临时覆盖 / 部署时指定）
    const QByteArray env = qgetenv("VLC_PATH");
    if (!env.isEmpty()) {
        const QString p = QString::fromLocal8Bit(env);
        if (QFile::exists(p)) return p;
    }

    // 2) 用户设置（可在设置界面里改；Windows 走注册表，Linux 走 ~/.config）
    QSettings settings("0323CloudDisk", "client");
    const QString configured = settings.value("player/vlcPath").toString();
    if (!configured.isEmpty() && QFile::exists(configured)) return configured;

    // 3) PATH 搜索（QStandardPaths 在所有平台上都是跨平台的 PATH 查找）
    const QString inPath = QStandardPaths::findExecutable("vlc");
    if (!inPath.isEmpty()) return inPath;

    // 4) 各平台常见安装位置
    //    注：最后一项目前是本项目开发机上的位置，作为兼容候选保留；
    //        推荐用前两级（环境变量 / 用户设置）指定，避免依赖个人机器布局。
    const QStringList candidates = {
        "C:/Program Files/VideoLAN/VLC/vlc.exe",
        "C:/Program Files (x86)/VideoLAN/VLC/vlc.exe",
        QString(qgetenv("ProgramFiles")) + "/VideoLAN/VLC/vlc.exe",
        "C:/学习/VLC/vlc.exe",                  // 开发机默认（可被前两级覆盖）
        "/usr/bin/vlc",
        "/usr/local/bin/vlc",
        "/snap/bin/vlc",
        "/Applications/VLC.app/Contents/MacOS/VLC"   // macOS
    };
    for (const QString& c : candidates) {
        if (!c.isEmpty() && QFile::exists(c)) return c;
    }

    return QString();   // 5) 没找到 → 调用方回退系统默认播放器
}

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    m_fileNum =0;
    m_fileListRowOffset = 0;
    m_downloadFile = nullptr;
    m_downloadFileId = 0;
    m_downloadFileSize = 0;
    m_uploadTotalBlocks = 0;
    m_uploadReceivedAcks = 0;
    ui->setupUi(this);
    m_pKernel=new tcpkernel;
    m_login=new login1;
    m_login->getkernel(m_pKernel);

    m_login->show();

    if(m_pKernel->connectServer())
    {
        printf("connect server success\n");
    }
    else
    {
        printf("connect err\n");
    }

    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_register,m_login,&login1::signal_register,Qt::BlockingQueuedConnection);
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_loginerrs,this,&Widget::slot_loginrs,Qt::BlockingQueuedConnection);
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_getfilelistrs,this,&Widget::slot_getfilelistrs,Qt::BlockingQueuedConnection);
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_uploadfileinfors,this,&Widget::slot_uploadfileinfors,Qt::BlockingQueuedConnection);
    // Phase 2：下载、删除、分享、提取
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_downloadinfors,this,&Widget::slot_downloadinfors,Qt::BlockingQueuedConnection);
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_downloadblockrs,this,&Widget::slot_downloadblockrs,Qt::BlockingQueuedConnection);
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_deleters,this,&Widget::slot_deleters,Qt::BlockingQueuedConnection);
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_sharers,this,&Widget::slot_sharers,Qt::BlockingQueuedConnection);
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_getfilers,this,&Widget::slot_getfilers,Qt::BlockingQueuedConnection);
    // Cluster：重定向信号
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_redirect,this,&Widget::slot_redirect,Qt::BlockingQueuedConnection);
    // Phase 2：HTTP 流媒体令牌信号
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_streamtoken,this,&Widget::slot_streamtoken,Qt::BlockingQueuedConnection);
    // Phase 3：AI 信号
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_aipreview,this,&Widget::slot_aipreview,Qt::BlockingQueuedConnection);
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_aisearch,this,&Widget::slot_aisearch,Qt::BlockingQueuedConnection);
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_aitag,this,&Widget::slot_aitag,Qt::BlockingQueuedConnection);
    // X3 修复：块 ACK 追踪
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_uploadfileblockrs,this,&Widget::slot_uploadfileblockrs,Qt::BlockingQueuedConnection);
    // F10-4：分享撤销
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_deletesharers,this,&Widget::slot_deletesharers,Qt::BlockingQueuedConnection);
    // L2 稀疏指纹预检
    connect((tcpkernel*)m_pKernel,&tcpkernel::signal_sparsecheckrs,this,&Widget::slot_sparsecheckrs,Qt::BlockingQueuedConnection);

    // Phase 3：AI 标签云（右侧面板，点击过滤文件列表）
    m_tagCloud = new TagCloud(this);
    m_tagCloud->setGeometry(520, 520, 264, 160);
    connect(m_tagCloud, &TagCloud::tagClicked, this, &Widget::onTagClicked);

    // 双击表格行 → 智能路由（媒体 → 流播放，文本 → AI 预览）
    connect(ui->tableWidget, &QTableWidget::cellDoubleClicked, this, [this](int row, int /*col*/) {
        QTableWidgetItem* item = ui->tableWidget->item(row, 0);
        if (!item) return;

        QString fileName = item->text();
        QString ext = QFileInfo(fileName).suffix().toLower();
        int64_t fileId = item->data(Qt::UserRole).toLongLong();

        // 媒体文件：视频、音频、图片、PDF → HTTP 流播放
        bool isMedia = false;
        const char* mediaExts[] = {
            "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v", "3gp",
            "mp3", "wav", "flac", "aac", "ogg", "wma", "m4a", "opus",
            "jpg", "jpeg", "png", "gif", "bmp", "webp", "svg", "ico", "tiff", "tif",
            "pdf", nullptr
        };
        for (int i = 0; mediaExts[i] != nullptr; i++) {
            if (ext == mediaExts[i]) { isMedia = true; break; }
        }

        if (isMedia) {
            STRU_STREAMTOKENRQ rq;
            rq.m_userId = Userid;
            rq.m_fileID = fileId;
            auto packet = ProtocolFactory::serializeStreamTokenRQ(rq);
            m_pKernel->sendData((char*)packet.data(), packet.size());
        } else {
            STRU_AIPREVIEWRQ rq;
            rq.m_userId = Userid;
            rq.m_fileID = fileId;
            auto packet = ProtocolFactory::serializeAIPreviewRQ(rq);
            m_pKernel->sendData((char*)packet.data(), packet.size());
        }
    });
}

Widget::~Widget()
{
    if (m_downloadFile) { fclose(m_downloadFile); m_downloadFile = nullptr; }
    for (auto* p : m_lstfileinfo) { delete p; }
    m_lstfileinfo.clear();
    delete m_pKernel;
    delete m_login;
    delete ui;
}

void Widget::slot_loginrs(const STRU_LOGINRS& psl)
{
    if(psl.m_szResult==_login_success)
    {
        m_login->hide();
        this->show();
        Userid=psl.m_userId;

        STRU_GETFILELISTRQ sg;
        sg.m_userId=Userid;
        m_fileListRowOffset = 0;
        ui->tableWidget->setRowCount(0);
        auto packet = ProtocolFactory::serializeGetFileListRQ(sg);
        m_pKernel->sendData((char*)packet.data(), packet.size());
        return;
    }
    // F2-2 修复：统一错误提示，不区分"用户不存在"与"密码错误"
    QMessageBox::information(this,"login","用户名或密码无效");
}

void Widget::slot_getfilelistrs(const STRU_GETFILELISTRS& psg)
{
    QTableWidgetItem*pItem;
    QString str;
    m_fileNum=psg.m_nFileNum;

    int baseRow = m_fileListRowOffset;
    ui->tableWidget->setRowCount(baseRow + psg.m_nFileNum);

    for(int i=0;i<psg.m_nFileNum;i++)
    {
        int row = baseRow + i;
        pItem=new QTableWidgetItem(QIcon(":/0307.jpg"),psg.m_aryFileInfo[i].m_szFileName);
        pItem->setData(Qt::UserRole, QVariant((qlonglong)psg.m_aryFileInfo[i].m_fileID));
        ui->tableWidget->setItem(row,0,pItem);

        str=QString::number(psg.m_aryFileInfo[i].m_filesize);
        pItem=new QTableWidgetItem(str);
        ui->tableWidget->setItem(row,1,pItem);

        pItem=new QTableWidgetItem(psg.m_aryFileInfo[i].m_szFileUploadTime);
        ui->tableWidget->setItem(row,2,pItem);

        m_fileIdMap[psg.m_aryFileInfo[i].m_szFileName] = psg.m_aryFileInfo[i].m_fileID;
    }

    m_fileListRowOffset += psg.m_nFileNum;
}

std::string FileDigest(QString filename) {
    return CryptoUtil::fileFingerprint(filename.toStdString());
}

void Widget::slot_uploadfileinfors(const STRU_UPLOADFILEINFORS& psu)
{
    STRU_FILEINFO*p=NULL;
    auto itefile=m_lstfileinfo.begin();
    while(itefile!=m_lstfileinfo.end())
    {
        if(0==strcmp(psu.m_szFileSHA256,(*itefile)->m_szFileSHA256)&&
            0==strcmp(psu.m_szFileName,(*itefile)->m_szFileName))
        {
            p=*itefile;
            break;
        }
        ++itefile;
    }

    switch (psu.m_szResult) {
    case _uploadfile_isuploaded:
        QMessageBox::information(this,"upload file","文件已存在（秒传）");
        // F3-6 修复：上传后刷新文件列表
        {
            STRU_GETFILELISTRQ sg; sg.m_userId = Userid;
            m_fileListRowOffset = 0; ui->tableWidget->setRowCount(0);
            auto pkt = ProtocolFactory::serializeGetFileListRQ(sg);
            m_pKernel->sendData((char*)pkt.data(), pkt.size());
        }
        break;
    case _uploadfile_flash:
        // F3-6 修复：秒传成功后刷新文件列表
        {
            STRU_GETFILELISTRQ sg; sg.m_userId = Userid;
            m_fileListRowOffset = 0; ui->tableWidget->setRowCount(0);
            auto pkt = ProtocolFactory::serializeGetFileListRQ(sg);
            m_pKernel->sendData((char*)pkt.data(), pkt.size());
        }
        QMessageBox::information(this,"upload file","秒传成功");
        break;
    case _uploadfile_normal:
        {
            if (!p) break;
            STRU_UPLOADFILEBLOCKRQ su;
            su.m_userId=Userid;
            su.m_fileID=psu.m_fileID;
            FILE*pfile=fopen(p->m_szFilePath,"rb");
            if(!pfile) { QMessageBox::warning(this,"上传","无法打开文件"); break; }

            // X3 修复：统计总块数用于 ACK 计数
            long long fileSize = p->m_filesize;
            m_uploadTotalBlocks = (int)((fileSize + MAXFILECONTENT - 1) / MAXFILECONTENT);
            m_uploadReceivedAcks = 0;
            m_uploadFileName = QString::fromUtf8(p->m_szFileName);

            // F4-4 修复：>2GB 文件使用 _fseeki64
            if(psu.m_pos>0) {
                _fseeki64(pfile, psu.m_pos, SEEK_SET);
                // 调整块序号，从正确位置续传
                su.m_blockSeq = (int32_t)(psu.m_pos / MAXFILECONTENT);
            }
            int seq = su.m_blockSeq;
            while(1)
            {
                int nreadnum=fread(su.m_szFileContent,sizeof(char),sizeof(su.m_szFileContent),pfile);
                if(nreadnum>0)
                {
                    su.m_blockSeq = seq++;
                    su.m_fileblocksize=nreadnum;
                    auto packet = ProtocolFactory::serializeUploadFileBlockRQ(su);
                    m_pKernel->sendData((const char*)packet.data(), packet.size());
                }
                else
                {
                    break;
                }
            }
            fclose(pfile);
        }
        break;
    case _uploadfile_continue:
        {
            if (!p) break;
            printf("[续传] 从位置 %lld 继续上传文件 %s\n", psu.m_pos, p->m_szFileName);

            FILE* pfile = fopen(p->m_szFilePath, "rb");
            if (!pfile) {
                QMessageBox::warning(this, "上传", "无法恢复上传，文件可能已被删除");
                break;
            }
            // F4-4 修复：>2GB 文件使用 _fseeki64
            _fseeki64(pfile, psu.m_pos, SEEK_SET);
            p->m_fileid = psu.m_fileID;

            // X3 修复：断点续传时同样统计块数
            long long remaining = p->m_filesize - psu.m_pos;
            m_uploadTotalBlocks = (int)((remaining + MAXFILECONTENT - 1) / MAXFILECONTENT);
            m_uploadReceivedAcks = 0;
            m_uploadFileName = QString::fromUtf8(p->m_szFileName);

            STRU_UPLOADFILEBLOCKRQ su;
            su.m_userId = Userid;
            su.m_fileID = psu.m_fileID;
            su.m_blockSeq = (int32_t)(psu.m_pos / MAXFILECONTENT);
            int seq = su.m_blockSeq;
            while (1) {
                int nreadnum = fread(su.m_szFileContent, 1, sizeof(su.m_szFileContent), pfile);
                if (nreadnum > 0) {
                    su.m_blockSeq = seq++;
                    su.m_fileblocksize = nreadnum;
                    auto packet = ProtocolFactory::serializeUploadFileBlockRQ(su);
                    m_pKernel->sendData((const char*)packet.data(), packet.size());
                } else {
                    break;
                }
            }
            fclose(pfile);
        }
        break;
    default:
        break;
    }

    // F3-6 修复：清理本地追踪信息并刷新列表
    auto ite=m_lstfileinfo.begin();
    while(ite!=m_lstfileinfo.end())
    {
        if(0==strcmp(psu.m_szFileSHA256,(*ite)->m_szFileSHA256)&&
            0==strcmp(psu.m_szFileName,(*ite)->m_szFileName))
        {
            delete *ite;
            m_lstfileinfo.erase(ite);
            break;
        }
        ++ite;
    }
}

void Widget::slot_uploadfileblockrs(const STRU_UPLOADFILEBLOCKRS& rs)
{
    // X3 修复：统计 ACK 数量，全部块确认后才提示"上传成功"
    if (rs.m_szResult != 0) {
        m_uploadReceivedAcks++;
    }
    if (m_uploadTotalBlocks > 0 && m_uploadReceivedAcks >= m_uploadTotalBlocks) {
        // F3-6 修复：上传完成后刷新文件列表
        STRU_GETFILELISTRQ sg; sg.m_userId = Userid;
        m_fileListRowOffset = 0; ui->tableWidget->setRowCount(0);
        auto pkt = ProtocolFactory::serializeGetFileListRQ(sg);
        m_pKernel->sendData((char*)pkt.data(), pkt.size());

        QMessageBox::information(this, "上传", "上传成功");
        m_uploadTotalBlocks = 0;
        m_uploadReceivedAcks = 0;
    }
}

void Widget::slot_deletesharers(const STRU_DELETESHARERS& rs)
{
    // F10-4：分享撤销响应
    if (rs.m_szResult == 1) {
        QMessageBox::information(this, "撤销分享", "分享链接已撤销");
    } else {
        QMessageBox::warning(this, "撤销分享", "未找到该文件的分享链接");
    }
}

void Widget::on_pushButton_clicked()
{
    //上传文件
    QString filepath=QFileDialog::getOpenFileName(
        this, tr("Open File"), ".",
        "ALL files (*.*);;Images (*.png *.xpm *.jpg);;Text files (*.txt)");
    if (filepath.isEmpty()) return;

    QString filename=filepath.section('/',-1);

    long long filesize;
    {
        QFile file(filepath);
        file.open(QIODevice::ReadOnly);
        filesize=file.size();
        file.close();
    }
    QDateTime datetime=QDateTime::currentDateTime();
    QString strtime=datetime.toString("yyyy-MM-dd hh:mm:ss");

    QFileInfo fileInfo(filepath);
    int64_t mtime = fileInfo.lastModified().toSecsSinceEpoch();

    // ===================================================================
    // 三层秒传漏斗
    // ===================================================================
    std::string strSHA256;

    // L1：客户端 SQLite 缓存（约 70% 命中，<1ms）
    UploadCache cache;
    cache.open("./upload_cache.db");
    std::string cachedSHA = cache.lookup(filepath.toStdString(), mtime, filesize);
    if (!cachedSHA.empty()) {
        strSHA256 = cachedSHA;
        qDebug() << "[秒传 L1 HIT] SQLite cache:" << cachedSHA.c_str();
        cache.close();

        // L1 命中：跳过 L2+L3，立即发送 UploadFileInfoRQ
        qDebug()<<filesize<<strtime<<"SHA256:"<<strSHA256.c_str();
        STRU_UPLOADFILEINFORQ su;
        strncpy(su.m_fileInfo.m_szFileName,filename.toStdString().c_str(),MAXSIZE-1);
        strncpy(su.m_fileInfo.m_szFileUploadTime,strtime.toStdString().c_str(),MAXSIZE-1);
        su.m_fileInfo.m_filesize=filesize;
        strncpy(su.m_szFileSHA256, strSHA256.c_str(), sizeof(su.m_szFileSHA256) - 1);
        su.m_userId=Userid;
        auto packet = ProtocolFactory::serializeUploadFileInfoRQ(su);
        m_pKernel->sendData((const char*)packet.data(), packet.size());

        STRU_FILEINFO*p=new STRU_FILEINFO;
        p->m_fileid=0; p->m_filepos=0; p->m_filesize=filesize;
        strncpy(p->m_szFileSHA256,strSHA256.c_str(),MAXSIZE-1);
        strncpy(p->m_szFilePath,filepath.toStdString().c_str(),259);
        strncpy(p->m_szFileName,filename.toStdString().c_str(),MAXSIZE-1);
        strncpy(p->m_szFileUploadTime,strtime.toStdString().c_str(),MAXSIZE-1);
        m_lstfileinfo.push_back(p);
    } else {
        // L1 未命中：计算稀疏指纹用于 L2 预检
        std::string sparseFp = CryptoUtil::sparseFingerprint(filepath.toStdString());
        qDebug() << "[秒传 L2] Sparse fingerprint, querying server:" << sparseFp.c_str();

        // 保存上下文，供异步 sparsecheck 响应处理使用
        m_pendingUploadPath = filepath;
        m_pendingUploadName = filename;
        m_pendingUploadSize = filesize;
        m_pendingUploadMtime = mtime;
        m_pendingUploadTime = strtime;

        // 向服务器发送 sparsecheck 请求（L2）
        STRU_SPARSECHECKRQ sq;
        sq.m_userId = Userid;
        sq.m_fileSize = filesize;
        strncpy(sq.m_szSparseFingerprint, sparseFp.c_str(), sizeof(sq.m_szSparseFingerprint) - 1);
        sq.m_szSparseFingerprint[sizeof(sq.m_szSparseFingerprint) - 1] = '\0';
        auto pkt = ProtocolFactory::serializeSparseCheckRQ(sq);
        m_pKernel->sendData((const char*)pkt.data(), pkt.size());

        cache.close();  // 关闭缓存，必要时在 slot_sparsecheckrs 中重新打开
    }
}

// ─── L2 稀疏指纹响应处理 ──────────────────────────────────────────────
void Widget::slot_sparsecheckrs(const STRU_SPARSECHECKRS& rs)
{
    if (m_pendingUploadPath.isEmpty()) return;  // 安全保护：无待上传任务

    std::string strSHA256;
    QString filepath     = m_pendingUploadPath;
    QString filename     = m_pendingUploadName;
    long long filesize   = m_pendingUploadSize;
    int64_t mtime        = m_pendingUploadMtime;
    QString strtime      = m_pendingUploadTime;

    // 清空待处理上下文
    m_pendingUploadPath.clear();

    if (rs.m_szResult == 0) {
        // 服务器判定"肯定为新文件" → 跳过完整 SHA-256 计算
        qDebug() << "[秒传 L2] Server: definitely new, skipping full SHA-256";
        strSHA256 = "";  // 服务器将在块上传期间计算真实 SHA-256
    } else {
        // 服务器判定"可能存在" → 计算完整 SHA-256 供 L3 检查
        qDebug() << "[秒传 L2→L3] Server: might exist, computing full SHA-256...";
        strSHA256 = CryptoUtil::fileFingerprint(filepath.toStdString());
        qDebug() << "[秒传 L2→L3] Full SHA-256:" << strSHA256.c_str();

        // 存入 L1 缓存，供后续秒传使用
        UploadCache cache;
        cache.open("./upload_cache.db");
        cache.store(filepath.toStdString(), mtime, filesize, strSHA256);
        cache.close();
    }

    qDebug()<<filesize<<strtime<<"SHA256:"<<strSHA256.c_str();

    // 发送 UploadFileInfoRQ
    STRU_UPLOADFILEINFORQ su;
    strncpy(su.m_fileInfo.m_szFileName,filename.toStdString().c_str(),MAXSIZE-1);
    strncpy(su.m_fileInfo.m_szFileUploadTime,strtime.toStdString().c_str(),MAXSIZE-1);
    su.m_fileInfo.m_filesize=filesize;
    strncpy(su.m_szFileSHA256, strSHA256.c_str(), sizeof(su.m_szFileSHA256) - 1);
    su.m_userId=Userid;

    auto packet = ProtocolFactory::serializeUploadFileInfoRQ(su);
    m_pKernel->sendData((const char*)packet.data(), packet.size());

    // 本地记录上传会话
    STRU_FILEINFO*p=new STRU_FILEINFO;
    p->m_fileid=0;
    p->m_filepos=0;
    p->m_filesize=filesize;
    strncpy(p->m_szFileSHA256,strSHA256.c_str(),MAXSIZE-1);
    strncpy(p->m_szFilePath,filepath.toStdString().c_str(),259);
    strncpy(p->m_szFileName,filename.toStdString().c_str(),MAXSIZE-1);
    strncpy(p->m_szFileUploadTime,strtime.toStdString().c_str(),MAXSIZE-1);

    m_lstfileinfo.push_back(p);
}

void Widget::on_pushButton_2_clicked()//搜索文件
{
    bool ok;
    QString query = QInputDialog::getText(this, "AI Search", "Enter search keywords:",
                                          QLineEdit::Normal, "", &ok);
    if (!ok || query.isEmpty()) return;

    STRU_AISEARCHRQ sq;
    sq.m_userId = Userid;
    strncpy(sq.m_szQuery, query.toStdString().c_str(), sizeof(sq.m_szQuery) - 1);
    sq.m_szQuery[sizeof(sq.m_szQuery) - 1] = '\0';

    auto packet = ProtocolFactory::serializeAISearchRQ(sq);
    m_pKernel->sendData((char*)packet.data(), packet.size());
}

void Widget::on_pushButton_3_clicked()//删除文件
{
    int row = ui->tableWidget->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "删除", "请先选择要删除的文件");
        return;
    }

    if (QMessageBox::question(this, "确认", "确定要删除此文件吗？") != QMessageBox::Yes)
        return;

    QTableWidgetItem* item = ui->tableWidget->item(row, 0);
    if (!item) return;

    // F7-3 修复：用 Qt::UserRole 存 fileID（而非 m_fileID = 0）
    long long fileID = item->data(Qt::UserRole).toLongLong();
    if (fileID == 0) {
        // 兜底：尝试按文件名映射
        QString fileName = item->text();
        auto it = m_fileIdMap.find(fileName.toStdString());
        if (it != m_fileIdMap.end()) fileID = it->second;
    }

    STRU_DELETEFILERQ drq;
    drq.m_userId = Userid;
    drq.m_fileID = fileID;
    auto packet = ProtocolFactory::serializeDeleteFileRQ(drq);
    m_pKernel->sendData((char*)packet.data(), packet.size());
}

void Widget::on_pushButton_4_clicked()//下载文件
{
    int row = ui->tableWidget->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "下载", "请先选择要下载的文件");
        return;
    }

    QTableWidgetItem* item = ui->tableWidget->item(row, 0);
    if (!item) return;

    QString fileName = item->text();
    m_downloadFileName = fileName;

    // F7-3 修复：用 Qt::UserRole 取 fileID
    long long fileID = item->data(Qt::UserRole).toLongLong();

    STRU_DOWNLOADFILEINFORQ dq;
    dq.m_userId = Userid;
    dq.m_fileID = fileID;
    strncpy(dq.m_szFileName, fileName.toStdString().c_str(), MAXSIZE - 1);

    auto packet = ProtocolFactory::serializeDownloadFileInfoRQ(dq);
    m_pKernel->sendData((char*)packet.data(), packet.size());
}

void Widget::on_pushButton_5_clicked()//分享文件
{
    int row = ui->tableWidget->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "分享", "请先选择要分享的文件");
        return;
    }

    QTableWidgetItem* item = ui->tableWidget->item(row, 0);
    if (!item) return;

    // F7-3 修复：用 Qt::UserRole 取 fileID
    long long fileID = item->data(Qt::UserRole).toLongLong();

    STRU_SHAREFILERQ sq;
    sq.m_userId = Userid;
    sq.m_fileID = fileID;
    memset(sq.m_szShareToUser, 0, MAXSIZE);

    auto packet = ProtocolFactory::serializeShareFileRQ(sq);
    m_pKernel->sendData((char*)packet.data(), packet.size());
}

void Widget::on_pushButton_6_clicked()//提取文件
{
    bool ok;
    QString code = QInputDialog::getText(this, "提取文件", "请输入分享码:",
                                          QLineEdit::Normal, "", &ok);
    if (!ok || code.isEmpty()) return;

    STRU_GETFILERQ gq;
    gq.m_userId = Userid;
    gq.m_shareFileID = strtoll(code.toStdString().c_str(), nullptr, 16);

    auto packet = ProtocolFactory::serializeGetFileRQ(gq);
    m_pKernel->sendData((char*)packet.data(), packet.size());
}

void Widget::on_pushButton_7_clicked()//F10-4: 撤销分享
{
    int row = ui->tableWidget->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, "撤销分享", "请先选择要撤销分享的文件");
        return;
    }

    QTableWidgetItem* item = ui->tableWidget->item(row, 0);
    if (!item) return;

    if (QMessageBox::question(this, "确认", "确定要撤销此文件的分享链接吗？") != QMessageBox::Yes)
        return;

    long long fileID = item->data(Qt::UserRole).toLongLong();

    STRU_DELETESHARERQ dq;
    dq.m_userId = Userid;
    dq.m_fileID = fileID;

    auto packet = ProtocolFactory::serializeDeleteShareRQ(dq);
    m_pKernel->sendData((char*)packet.data(), packet.size());
}

// ============================================================================
// 下载槽函数
// ============================================================================

void Widget::requestDownloadBlock(int64_t pos)
{
    STRU_DOWNLOADFILEBLOCKRQ dq;
    dq.m_userId = Userid;
    dq.m_fileID = m_downloadFileId;
    dq.m_pos = pos;

    auto packet = ProtocolFactory::serializeDownloadFileBlockRQ(dq);
    m_pKernel->sendData((char*)packet.data(), packet.size());
}

void Widget::slot_downloadinfors(const STRU_DOWNLOADFILEINFORS& rs)
{
    if (rs.m_szResult == 0) {
        QMessageBox::warning(this, "下载", "文件不存在或无权限");
        return;
    }

    m_downloadFileId = rs.m_fileID;
    m_downloadFileSize = rs.m_fileSize;
    m_downloadTotalBlocks = rs.m_nBlockNum;
    m_downloadBlockSize = (int)rs.m_nBlockSize;
    m_downloadReceivedBlocks = 0;
    // X5：保存期望的 SHA-256，用于最终校验
    m_downloadExpectedSHA256 = QString::fromUtf8(rs.m_szFileSHA256, strnlen(rs.m_szFileSHA256, 65));

    QString savePath = QFileDialog::getSaveFileName(this, "保存文件", m_downloadFileName);
    if (savePath.isEmpty()) return;

    m_downloadFile = fopen(savePath.toStdString().c_str(), "wb");
    if (!m_downloadFile) {
        QMessageBox::critical(this, "下载", "无法创建文件");
        return;
    }
    m_downloadSavePath = savePath;  // X5：记录保存路径，用于 SHA-256 校验

    requestDownloadBlock(0);
}

void Widget::slot_downloadblockrs(const STRU_DOWNLOADFILEBLOCKRS& rs)
{
    if (rs.m_szResult == 0 || rs.m_fileblocksize <= 0) {
        QMessageBox::warning(this, "下载", "接收文件块失败");
        if (m_downloadFile) { fclose(m_downloadFile); m_downloadFile = nullptr; }
        return;
    }

    // F7-4 修复：>2GB 文件使用 _fseeki64
    _fseeki64(m_downloadFile, rs.m_pos, SEEK_SET);
    fwrite(rs.m_szFileContent, 1, (size_t)rs.m_fileblocksize, m_downloadFile);

    m_downloadReceivedBlocks++;

    int64_t nextPos = rs.m_pos + rs.m_fileblocksize;
    if (nextPos < m_downloadFileSize) {
        requestDownloadBlock(nextPos);
    } else {
        // 下载完成
        fclose(m_downloadFile);
        m_downloadFile = nullptr;

        // X5 修复：SHA-256 完整性校验
        bool hashOk = true;
        if (!m_downloadExpectedSHA256.isEmpty() && !m_downloadSavePath.isEmpty()) {
            std::string actualHash = CryptoUtil::fileFingerprint(m_downloadSavePath.toStdString());
            std::string expectedHash = m_downloadExpectedSHA256.toStdString();
            hashOk = (actualHash == expectedHash);
            if (!hashOk) {
                QMessageBox::warning(this, "下载",
                    QString("SHA-256 校验失败！\n期望: %1\n实际: %2\n文件可能已损坏，请重新下载。")
                    .arg(expectedHash.c_str()).arg(actualHash.c_str()));
            }
        }
        if (hashOk) {
            QMessageBox::information(this, "下载", QString("下载完成！\n文件: %1\n大小: %2 字节")
                .arg(m_downloadFileName).arg(m_downloadFileSize));
        }
    }
}

void Widget::slot_deleters(const STRU_DELETEFILERS& rs)
{
    if (rs.m_szResult == 1) {
        QMessageBox::information(this, "删除", "文件已删除");
        STRU_GETFILELISTRQ sg;
        sg.m_userId = Userid;
        m_fileListRowOffset = 0;
        ui->tableWidget->setRowCount(0);
        auto packet = ProtocolFactory::serializeGetFileListRQ(sg);
        m_pKernel->sendData((char*)packet.data(), packet.size());
    } else {
        QMessageBox::warning(this, "删除", "删除失败");
    }
}

void Widget::slot_sharers(const STRU_SHAREFILERS& rs)
{
    if (rs.m_szResult == 1) {
        // F10-6 修复：分享码可复制
        QString codeText = QString("分享码: %1\n将此码发给好友即可提取文件").arg(rs.m_szShareCode);
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("分享");
        msgBox.setText(codeText);
        msgBox.setStandardButtons(QMessageBox::Ok);
        // 添加"复制"按钮
        QPushButton* copyBtn = msgBox.addButton("复制分享码", QMessageBox::ActionRole);
        msgBox.exec();
        if (msgBox.clickedButton() == copyBtn) {
            QApplication::clipboard()->setText(rs.m_szShareCode);
        }
    } else {
        QMessageBox::warning(this, "分享", "分享失败");
    }
}

void Widget::slot_getfilers(const STRU_GETFILERS& rs)
{
    if (rs.m_szResult == 1) {
        QMessageBox::information(this, "提取",
            QString("文件提取成功！\n文件名: %1").arg(rs.m_fileInfo.m_szFileName));
        STRU_GETFILELISTRQ sg;
        sg.m_userId = Userid;
        m_fileListRowOffset = 0;
        ui->tableWidget->setRowCount(0);
        auto packet = ProtocolFactory::serializeGetFileListRQ(sg);
        m_pKernel->sendData((char*)packet.data(), packet.size());
    } else {
        QMessageBox::warning(this, "提取", "提取失败，分享码无效或已过期");
    }
}

// ============================================================================
// HTTP 流媒体令牌槽函数
// ============================================================================
void Widget::slot_streamtoken(const STRU_STREAMTOKENRS& rs) {
    if (rs.m_szResult != 0) {
        QMessageBox::warning(this, "Stream Error",
            "Cannot stream this file. The file may have been deleted or is unavailable.");
        return;
    }

    QString url = QString("http://127.0.0.1:%1/stream/%2?token=%3&userId=%4&ts=%5&name=%6")
        .arg(rs.m_nHttpPort)
        .arg(rs.m_fileID)
        .arg(rs.m_szToken)
        .arg(Userid)
        .arg(rs.m_nTimestamp)
        .arg(rs.m_szFileName);

    printf("[StreamToken] Opening URL: %s\n", url.toUtf8().constData());

    // 播放器路径来自「环境变量 > 用户设置 > PATH > 各平台常见位置」，
    // 不再写死某一台机器上的绝对路径（见文件顶部的 resolvePreferredPlayer）
    const QString playerPath = resolvePreferredPlayer();
    if (playerPath.isEmpty()) {
        printf("[StreamToken] No VLC found, using system default player\n");
        if (!QDesktopServices::openUrl(QUrl(url))) {
            QMessageBox::warning(this, "Stream Error",
                QString("Failed to open media player.\n\nYou can manually open this URL:\n%1").arg(url));
        }
        return;
    }

    printf("[StreamToken] Launching player: %s\n", playerPath.toUtf8().constData());
    qint64 pid = 0;
    if (!QProcess::startDetached(playerPath, QStringList{url}, QString(), &pid)) {
        printf("[StreamToken] Player launch failed, falling back to system default\n");
        if (!QDesktopServices::openUrl(QUrl(url))) {
            QMessageBox::warning(this, "Stream Error",
                QString("Failed to open media player.\n\nYou can manually open this URL:\n%1").arg(url));
        }
        return;
    }

    printf("[StreamToken] Player launched (PID: %lld)\n", pid);
}

// ============================================================================
// AI 预览槽函数
// ============================================================================
void Widget::slot_aipreview(const STRU_AIPREVIEWRS& rs) {
    if (rs.m_szResult == 1) {
        QMessageBox::warning(this, "Preview Error", "Cannot read file content. The file may have been deleted.");
        return;
    }

    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(QString("Online Preview — %1").arg(rs.m_szFileName));
    dlg->resize(600, 700);
    dlg->setMinimumSize(500, 400);

    QVBoxLayout* mainLayout = new QVBoxLayout(dlg);

    if (strlen(rs.m_szAIError) > 0) {
        QLabel* errLabel = new QLabel(dlg);
        errLabel->setText(QString("Warning: AI unavailable — %1\nShowing fallback summary below.").arg(rs.m_szAIError));
        errLabel->setStyleSheet("QLabel { background-color: #fff3cd; color: #856404; padding: 8px; border-radius: 4px; font-weight: bold; }");
        errLabel->setWordWrap(true);
        mainLayout->addWidget(errLabel);
    }

    QLabel* typeLabel = new QLabel(dlg);
    typeLabel->setText(QString("File Type: %1").arg(rs.m_szFileType));
    typeLabel->setStyleSheet("QLabel { color: #6c757d; padding: 4px 0; }");
    mainLayout->addWidget(typeLabel);

    QGroupBox* aiGroup = new QGroupBox("AI Intelligent Analysis", dlg);
    QVBoxLayout* aiLayout = new QVBoxLayout(aiGroup);

    QLabel* summaryLabel = new QLabel(dlg);
    summaryLabel->setText(QString("Summary\n%1").arg(rs.m_szSummary));
    summaryLabel->setWordWrap(true);
    summaryLabel->setStyleSheet("QLabel { padding: 4px; }");
    aiLayout->addWidget(summaryLabel);

    if (strlen(rs.m_szKeywords) > 0) {
        QLabel* kwLabel = new QLabel(dlg);
        kwLabel->setText(QString("Keywords: %1").arg(rs.m_szKeywords));
        kwLabel->setWordWrap(true);
        kwLabel->setStyleSheet("QLabel { color: #0d6efd; padding: 4px; }");
        aiLayout->addWidget(kwLabel);
    }

    if (strlen(rs.m_szKeySentences) > 0) {
        QLabel* ksLabel = new QLabel(dlg);
        ksLabel->setText(QString("Key Sentences\n%1").arg(rs.m_szKeySentences));
        ksLabel->setWordWrap(true);
        ksLabel->setStyleSheet("QLabel { color: #198754; padding: 4px; font-style: italic; }");
        aiLayout->addWidget(ksLabel);
    }

    mainLayout->addWidget(aiGroup);

    if (rs.m_nRawContentLen > 0) {
        QGroupBox* rawGroup = new QGroupBox("Original Content (first 8KB)", dlg);
        QVBoxLayout* rawLayout = new QVBoxLayout(rawGroup);

        QTextEdit* rawText = new QTextEdit(dlg);
        rawText->setReadOnly(true);
        rawText->setFont(QFont("Consolas", 10));
        rawText->setPlainText(QString::fromUtf8(rs.m_szRawContent, rs.m_nRawContentLen));
        rawText->setMinimumHeight(200);

        if (rs.m_nRawContentLen >= MAXFILECONTENT * 2) {
            rawText->append("\n\n--- (Displaying first 8KB only) ---");
        }

        rawLayout->addWidget(rawText);
        mainLayout->addWidget(rawGroup);
    }

    QPushButton* closeBtn = new QPushButton("Close", dlg);
    QObject::connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    mainLayout->addWidget(closeBtn);

    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->exec();
}

// ============================================================================
// AI 搜索槽函数
// ============================================================================
void Widget::slot_aisearch(const STRU_AISEARCHRS& rs) {
    if (rs.m_nResultNum == 0) {
        QMessageBox::information(this, "AI Search", "No matching files found");
        return;
    }
    QString msg = QString("Found %1 result(s)").arg(rs.m_nResultNum);
    for (int i = 0; i < rs.m_nResultNum && i < 5; i++) {
        msg += QString("\n  %1 -- %2")
            .arg(rs.m_aryResults[i].m_fileInfo.m_szFileName)
            .arg(rs.m_aryResults[i].m_szMatchReason);
    }
    QMessageBox::information(this, "AI Semantic Search", msg);
}

// ============================================================================
// AI 标签槽函数 — 标签写入标签云（替代早期 QMessageBox 弹窗）
// ============================================================================
void Widget::slot_aitag(const STRU_AITAGRS& rs) {
    if (rs.m_nTagNum > 0) {
        QStringList tags;
        for (int i = 0; i < rs.m_nTagNum; i++) {
            tags << QString::fromUtf8(rs.m_szTags[i]);
        }
        if (m_tagCloud) {
            m_tagCloud->setFileTags(rs.m_fileID, tags);
            // 若当前正处于某标签过滤状态，新标签到达后刷新过滤视图
            if (!m_tagCloud->selectedTag().isEmpty())
                onTagClicked(m_tagCloud->selectedTag());
        }
    }
}

// ============================================================================
// 标签云点击 → 过滤文件列表（再次点击同一标签取消过滤）
// ============================================================================
void Widget::onTagClicked(const QString& tag)
{
    int rows = ui->tableWidget->rowCount();
    for (int r = 0; r < rows; r++) {
        QTableWidgetItem* item = ui->tableWidget->item(r, 0);
        if (!item) {
            ui->tableWidget->setRowHidden(r, false);
            continue;
        }
        qint64 fileId = item->data(Qt::UserRole).toLongLong();
        bool visible = tag.isEmpty() || m_tagCloud->tagsOf(fileId).contains(tag);
        ui->tableWidget->setRowHidden(r, !visible);
    }
}

// ============================================================================
// Cluster：重定向槽函数 — 自动重连到正确节点
// ============================================================================
void Widget::slot_redirect(const STRU_REDIRECTRS& rs)
{
    if (rs.m_szResult == _redirect_permanent) {
        printf("[Redirect] Auto-reconnecting to %s:%d ...\n",
               rs.m_szRedirectIP, rs.m_nRedirectPort);

        QString username = m_login->getUsername();
        QString password = m_login->getPassword();

        if (username.isEmpty()) {
            QMessageBox::warning(this, "重定向",
                QString("文件位于节点 %1:%2，请手动重新连接")
                .arg(rs.m_szRedirectIP).arg(rs.m_nRedirectPort));
            return;
        }

        m_pKernel->disconnectServer("Cluster redirect");

        if (!m_pKernel->connectServer(rs.m_szRedirectIP, (short)rs.m_nRedirectPort)) {
            QMessageBox::warning(this, "重定向失败",
                QString("无法连接到节点 %1:%2").arg(rs.m_szRedirectIP).arg(rs.m_nRedirectPort));
            return;
        }

        // X2 修复：只发送 SHA-256 哈希，不发送明文密码
        STRU_LOGINRQ sl;
        strncpy(sl.m_szName, username.toStdString().c_str(), MAXSIZE - 1);
        sl.m_szName[MAXSIZE - 1] = '\0';
        std::string hashed = CryptoUtil::hashPassword(password.toStdString());
        strncpy(sl.m_szPasswordSHA256, hashed.c_str(), 64);
        sl.m_szPasswordSHA256[64] = '\0';

        auto packet = ProtocolFactory::serializeLoginRQ(sl);
        m_pKernel->sendData((char*)packet.data(), packet.size());

        printf("[Redirect] Re-login sent as user=%s to %s:%d\n",
               sl.m_szName, rs.m_szRedirectIP, rs.m_nRedirectPort);
    }
}
