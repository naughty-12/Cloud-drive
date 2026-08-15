#include "login1.h"
#include "ui_login1.h"
#include "ProtocolFactory.h"
#include "security/CryptoUtil.h"

login1::login1(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::login1)
{
    ui->setupUi(this);
    setWindowTitle("disk");
    // Input validation: limit max length to prevent buffer overflow (F1-2 fix)
    ui->lineEdit_2ruser->setMaxLength(MAXSIZE - 1);
    ui->lineEdit_3rpassword->setMaxLength(MAXSIZE - 1);
    ui->lineEdit_7luser->setMaxLength(MAXSIZE - 1);
    ui->lineEdit_6password->setMaxLength(MAXSIZE - 1);
    //setWindowIcon(QIcon(":0307.jpg"));
}

login1::~login1()
{
    delete ui;
}

void login1::on_pushButton_clicked()//注册
{
    // F2-6 fix: button debounce — prevent double-click
    ui->pushButton->setEnabled(false);

    //获取注册信息
    QString strtel=ui->lineEditrtel->text();
    QString struser=ui->lineEdit_2ruser->text();
    QString strpassword=ui->lineEdit_3rpassword->text();

    // F1-3 fix: reject empty username/password
    if (struser.isEmpty() || strpassword.isEmpty()) {
        QMessageBox::warning(this, "注册", "用户名和密码不能为空");
        ui->pushButton->setEnabled(true);
        return;
    }

    //发送
    STRU_REGISTERRQ sr;
    sr.m_tel=strtel.toLongLong();
    // F1-2 fix: use strncpy with bounds check instead of strcpy
    strncpy(sr.m_szName, struser.toStdString().c_str(), MAXSIZE - 1);
    sr.m_szName[MAXSIZE - 1] = '\0';

    // X2 fix: only send SHA-256 hash, not plaintext password
    // F1-1 fix: hash is computed server-side; client sends hash only (not plaintext)
    std::string hashed = CryptoUtil::hashPassword(strpassword.toStdString());
    strncpy(sr.m_szPasswordSHA256, hashed.c_str(), 64);
    sr.m_szPasswordSHA256[64] = '\0';

    auto packet = ProtocolFactory::serializeRegisterRQ(sr);
    m_pkernel->sendData((char*)packet.data(), packet.size());
}

void login1::signal_register(const STRU_REGISTERRS& psr)
{
    ui->pushButton->setEnabled(true);  // F2-6: re-enable button
    if(psr.m_szResult==_register_success)
    {
        QMessageBox::information(this,"register","注册成功");
    }
    else
    {
        QMessageBox::information(this,"register","注册失败");
    }
}


void login1::on_pushButton_2_clicked()//登录
{
    // F2-6 fix: button debounce
    ui->pushButton_2->setEnabled(false);

    QString strUser=ui->lineEdit_7luser->text();
    QString strPassword=ui->lineEdit_6password->text();

    // F1-3 fix: reject empty input
    if (strUser.isEmpty() || strPassword.isEmpty()) {
        QMessageBox::warning(this, "登录", "用户名和密码不能为空");
        ui->pushButton_2->setEnabled(true);
        return;
    }

    STRU_LOGINRQ sl;
    // F1-2 fix: use strncpy with bounds check
    strncpy(sl.m_szName, strUser.toStdString().c_str(), MAXSIZE - 1);
    sl.m_szName[MAXSIZE - 1] = '\0';

    // X2 fix: only send SHA-256 hash, not plaintext password
    std::string hashed = CryptoUtil::hashPassword(strPassword.toStdString());
    strncpy(sl.m_szPasswordSHA256, hashed.c_str(), 64);
    sl.m_szPasswordSHA256[64] = '\0';

    auto packet = ProtocolFactory::serializeLoginRQ(sl);
    m_pkernel->sendData((char*)packet.data(), packet.size());
}

QString login1::getUsername() const {
    return ui->lineEdit_7luser->text();
}

QString login1::getPassword() const {
    return ui->lineEdit_6password->text();
}
