#ifndef TAGCLOUD_H
#define TAGCLOUD_H

#include <QWidget>
#include <QMap>
#include <QStringList>

class QLabel;
class QGridLayout;
class QScrollArea;

/**
 * @brief 标签云组件 —— AI 自动标签的可视化与点击过滤。
 *
 * 功能（对应 spec.md 7.5 的 Client TagCloud 要求）：
 *   1. 聚合本会话内收到 AITagRS 的文件标签，按使用次数降序展示；
 *   2. 点击标签 → 发出 tagClicked(tag) 信号，由主窗口过滤文件列表；
 *   3. 再次点击同一标签 → 取消过滤（tagClicked("")）。
 *
 * 数据来源：Widget::slot_aitag 收到服务端 STRU_AITAGRS 后调用
 * setFileTags(fileId, tags) 喂入（会话内累积；历史文件的标签
 * 需重新触发分析后才会出现）。
 */
class TagCloud : public QWidget
{
    Q_OBJECT
public:
    explicit TagCloud(QWidget* parent = nullptr);

    /// 设置某个文件的标签（替换该文件旧值），并刷新标签云
    void setFileTags(qint64 fileId, const QStringList& tags);

    /// 查询某个文件的标签（供文件列表过滤使用）
    QStringList tagsOf(qint64 fileId) const;

    /// 当前过滤的标签（空串 = 未过滤）
    QString selectedTag() const { return m_selectedTag; }

    /// 清空全部标签与过滤状态
    void clear();

signals:
    /// 点击标签；tag 为空串表示"取消过滤"
    void tagClicked(const QString& tag);

private slots:
    void onTagButtonClicked();

private:
    /// 重建标签按钮（统计计数 → 排序 → 布局）
    void rebuild();

    QMap<qint64, QStringList> m_fileTags;    // fileId → 标签列表
    QMap<QString, int>        m_tagCounts;   // 标签 → 出现次数
    QString                   m_selectedTag; // 当前过滤标签（空 = 不过滤）
    QLabel*                   m_titleLabel;
    QGridLayout*              m_grid;
    QScrollArea*              m_scrollArea;
    QWidget*                  m_container;
};

#endif // TAGCLOUD_H
