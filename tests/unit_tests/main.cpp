/**
 * @file main.cpp
 * @brief Qt Test 入口 — 依次执行四个测试套件。
 *
 * 用自定义 main 而不是 QTEST_MAIN, 是因为一个可执行文件里
 * 注册了多个测试类 (BinaryStream / CryptoUtil / ProtocolFactory / StreamAccess)。
 */

#include <QCoreApplication>
#include <QtTest/QtTest>

#include "tst_binarystream.h"
#include "tst_crypto.h"
#include "tst_protocolfactory.h"
#include "tst_streamaccess.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    int rc = 0;

    TestBinaryStream t1;
    rc |= QTest::qExec(&t1, argc, argv);

    TestCrypto t2;
    rc |= QTest::qExec(&t2, argc, argv);

    TestProtocolFactory t3;
    rc |= QTest::qExec(&t3, argc, argv);

    TestStreamAccess t4;
    rc |= QTest::qExec(&t4, argc, argv);

    return rc;
}
