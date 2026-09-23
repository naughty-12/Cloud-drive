/**
 * @file main.cpp
 * @brief Qt Test 入口 — 依次执行六个测试套件。
 *
 * 用自定义 main 而不是 QTEST_MAIN, 是因为一个可执行文件里
 * 注册了多个测试类 (BinaryStream / CryptoUtil / ProtocolFactory /
 * StreamAccess / LocalTokenize / TxtExtractor)。
 *
 * 注意: argv 会透传给每个 qExec 调用, 因此命令行参数被解释为"测试函数名",
 * 不能用来只跑某个测试类; 要看单个套件的输出请搜索 "Start testing of TestXXX"。
 */

#include <QCoreApplication>
#include <QtTest/QtTest>

#include "tst_binarystream.h"
#include "tst_crypto.h"
#include "tst_localtokenize.h"
#include "tst_protocolfactory.h"
#include "tst_streamaccess.h"
#include "tst_txtextractor.h"

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

    TestLocalTokenize t5;
    rc |= QTest::qExec(&t5, argc, argv);

    TestTxtExtractor t6;
    rc |= QTest::qExec(&t6, argc, argv);

    return rc;
}
