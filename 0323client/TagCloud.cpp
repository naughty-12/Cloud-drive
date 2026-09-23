#include "TagCloud.h"

#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QFrame>
#include <QVariant>
#include <algorithm>

TagCloud::TagCloud(QWidget* parent)
    : QWidget(parent)
    , m_titleLabel(nullptr)
    , m_grid(nullptr)
    , m_scrollArea(nullptr)
    , m_container(nullptr)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    m_titleLabel = new QLabel(QString::fromUtf8("🏷️ 标签云（点击过滤）"), this);
    m_titleLabel->setStyleSheet("font-weight: bold;");
    outer->addWidget(m_titleLabel);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_container = new QWidget(m_scrollArea);
    m_grid = new QGridLayout(m_container);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(4);

    m_scrollArea->setWidget(m_container);
    outer->addWidget(m_scrollArea);

    rebuild();
}

void TagCloud::setFileTags(qint64 fileId, const QStringList& tags)
{
    m_fileTags.insert(fileId, tags);
    rebuild();
}

QStringList TagCloud::tagsOf(qint64 fileId) const
{
    return m_fileTags.value(fileId);
}

void TagCloud::clear()
{
    m_fileTags.clear();
    m_selectedTag.clear();
    rebuild();
}

void TagCloud::onTagButtonClicked()
{
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn)
        return;
    QString tag = btn->property("tag").toString();

    // 再次点击同一标签 → 取消过滤
    if (m_selectedTag == tag)
        m_selectedTag.clear();
    else
        m_selectedTag = tag;

    rebuild();
    emit tagClicked(m_selectedTag);
}

void TagCloud::rebuild()
{
    // 1) 统计每个标签的出现次数（跨所有已标注文件）
    m_tagCounts.clear();
    for (auto it = m_fileTags.constBegin(); it != m_fileTags.constEnd(); ++it) {
        foreach (const QString& tag, it.value()) {
            m_tagCounts[tag] = m_tagCounts.value(tag) + 1;
        }
    }

    // 2) 清空旧按钮（takeAt 逐个移除并销毁）
    while (QLayoutItem* item = m_grid->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    // 3) 空状态提示
    if (m_tagCounts.isEmpty()) {
        auto* empty = new QLabel(
            QString::fromUtf8("（暂无标签——上传文件后 AI 自动生成）"), m_container);
        empty->setStyleSheet("color: gray;");
        m_grid->addWidget(empty, 0, 0);
        return;
    }

    // 4) 按出现次数降序排列标签
    QStringList tags = m_tagCounts.keys();
    std::sort(tags.begin(), tags.end(), [this](const QString& a, const QString& b) {
        return m_tagCounts.value(a) > m_tagCounts.value(b);
    });

    // 5) 生成标签按钮（每行 2 列；选中标签高亮）
    const int cols = 2;
    int row = 0, col = 0;
    foreach (const QString& tag, tags) {
        int count = m_tagCounts.value(tag);
        auto* btn = new QPushButton(QString("%1 ×%2").arg(tag).arg(count), m_container);
        btn->setProperty("tag", tag);
        btn->setToolTip(QString::fromUtf8("点击过滤文件列表，再次点击取消"));
        if (tag == m_selectedTag) {
            btn->setStyleSheet("background-color: #4a90d9; color: white; font-weight: bold;");
        }
        connect(btn, &QPushButton::clicked, this, &TagCloud::onTagButtonClicked);
        m_grid->addWidget(btn, row, col);
        if (++col >= cols) {
            col = 0;
            ++row;
        }
    }
    m_grid->setRowStretch(row, 1);
}
