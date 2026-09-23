#include "login1.h"
#include "ui_login1.h"
#include "ProtocolFactory.h"
#include "CryptoUtil.h"

login1::login1(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::login1)
{
    ui->setupUi(this);
    setWindowTitle("disk");
    // 输入校验：限制最大长度，防止缓冲区溢出（F1-2 修复）
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
    // F2-6 修复：按钮防抖，防止重复点击
    ui->pushButton->setEnabled(false);

    //获取注册信息
    QString strtel=ui->lineEditrtel->text();
    QString struser=ui->lineEdit_2ruser->text();
    QString strpassword=ui->lineEdit_3rpassword->text();

    // F1-3 修复：拒绝空用户名/密码
    if (struser.isEmpty() || strpassword.isEmpty()) {
        QMessageBox::warning(this, "注册", "用户名和密码不能为空");
        ui->pushButton->setEnabled(true);
        return;
    }

    //发送
    STRU_REGISTERRQ sr;
    sr.m_tel=strtel.toLongLong();
    // F1-2 修复：使用带边界检查的 strncpy 代替 strcpy
    strncpy(sr.m_szName, struser.toStdString().c_str(), MAXSIZE - 1);
    sr.m_szName[MAXSIZE - 1] = '\0';

    // X2 修复：只发送 SHA-256 哈希，不发送明文密码
    // F1-1 修复：哈希在服务端计算；客户端只发送哈希（非明文）
    std::string hashed = CryptoUtil::hashPassword(strpassword.toStdString());
    strncpy(sr.m_szPasswordSHA256, hashed.c_str(), 64);
    sr.m_szPasswordSHA256[64] = '\0';

    auto packet = ProtocolFactory::serializeRegisterRQ(sr);
    m_pkernel->sendData((char*)packet.data(), packet.size());
}

void login1::signal_register(const STRU_REGISTERRS& psr)
{
    ui->pushButton->setEnabled(true);  // F2-6：重新启用按钮
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
    // F2-6 修复：按钮防抖
    ui->pushButton_2->setEnabled(false);

    QString strUser=ui->lineEdit_7luser->text();
    QString strPassword=ui->lineEdit_6password->text();

    // F1-3 修复：拒绝空输入
    if (strUser.isEmpty() || strPassword.isEmpty()) {
        QMessageBox::warning(this, "登录", "用户名和密码不能为空");
        ui->pushButton_2->setEnabled(true);
        return;
    }

    STRU_LOGINRQ sl;
    // F1-2 修复：使用带边界检查的 strncpy
    strncpy(sl.m_szName, strUser.toStdString().c_str(), MAXSIZE - 1);
    sl.m_szName[MAXSIZE - 1] = '\0';

    // X2 修复：只发送 SHA-256 哈希，不发送明文密码
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
