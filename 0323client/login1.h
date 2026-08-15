#ifndef LOGIN1_H
#define LOGIN1_H

#include <QWidget>
#include "kernel/Ikernel.h"
#include "Packdef.h"
#include <QMessageBox>//用于显示消息框
namespace Ui {
class login1;
}

class login1 : public QWidget
{
    Q_OBJECT

public:
    explicit login1(QWidget *parent = nullptr);
    ~login1();
public:
    void getkernel(Ikernel*pkernel)
    {
        m_pkernel=pkernel;
    }
    // Expose credentials for cluster redirect auto-reconnect
    QString getUsername() const;
    QString getPassword() const;
private slots:
               // void on_pushButton_clicked();

    //void on_pushButton_2_clicked();

    //void on_pushButtonZhuCe_clicked();

   // void on_pushButtonZHUCE_clicked();

    void on_pushButton_2_clicked();

    void on_pushButton_clicked();

public slots:
    void signal_register(const STRU_REGISTERRS&);
private:
    Ui::login1 *ui;
    Ikernel*m_pkernel;
};

#endif // LOGIN1_H
