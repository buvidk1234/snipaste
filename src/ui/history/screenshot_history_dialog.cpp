#include "ui/history/screenshot_history_dialog.h"

#include "core/history/screenshot_history.h"

#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QListView>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace snipaste::ui {

ScreenshotHistoryDialog::ScreenshotHistoryDialog(core::ScreenshotHistory &history, QWidget *parent)
    : QDialog(parent)
    , m_history(history)
{
    setWindowTitle(QStringLiteral("截屏历史"));
    resize(760, 520);

    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(128, 96));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSpacing(8);

    m_preview = new QLabel(this);
    m_preview->setMinimumSize(360, 280);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet(QStringLiteral("background: #202020; color: #bbbbbb;"));
    m_preview->setText(QStringLiteral("选择一张截图"));

    auto *split = new QHBoxLayout;
    split->addWidget(m_list, 1);
    split->addWidget(m_preview, 2);

    m_copyButton = new QPushButton(QStringLiteral("复制"), this);
    m_pasteButton = new QPushButton(QStringLiteral("贴图"), this);
    m_saveButton = new QPushButton(QStringLiteral("保存…"), this);
    m_clearButton = new QPushButton(QStringLiteral("清空历史"), this);
    for (auto *button : {m_copyButton, m_pasteButton, m_saveButton, m_clearButton})
        button->setEnabled(false);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_copyButton);
    buttons->addWidget(m_pasteButton);
    buttons->addWidget(m_saveButton);
    buttons->addStretch();
    buttons->addWidget(m_clearButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(split);
    layout->addLayout(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, [this] {
        updatePreview();
    });
    connect(m_copyButton, &QPushButton::clicked, this, [this] {
        const QImage image = selectedImage();
        if (!image.isNull())
            emit copyRequested(image);
    });
    connect(m_pasteButton, &QPushButton::clicked, this, [this] {
        const QImage image = selectedImage();
        if (!image.isNull())
            emit pasteRequested(image);
    });
    connect(m_saveButton, &QPushButton::clicked, this, [this] {
        const QImage image = selectedImage();
        if (image.isNull())
            return;
        const QString path = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("保存历史截图"),
            QStringLiteral("snipaste-history.png"),
            QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg)"));
        if (!path.isEmpty() && !image.save(path))
            QMessageBox::warning(this, QStringLiteral("保存失败"), QStringLiteral("无法写入图像文件。"));
    });
    connect(m_clearButton, &QPushButton::clicked, this, [this] {
        m_history.clear();
        emit clearRequested();
        refresh();
    });

    refresh();
}

void ScreenshotHistoryDialog::refresh()
{
    const quint64 generation = ++m_refreshGeneration;
    const quint64 revision = m_history.revision();
    const int previousRow = m_list->currentRow();
    m_list->clear();
    for (int index = 0; index < m_history.size(); ++index) {
        const QSize imageSize = m_history.imageSizeAt(index);
        auto *item = new QListWidgetItem(QStringLiteral("%1 × %2")
                                             .arg(imageSize.width()).arg(imageSize.height()), m_list);
        item->setData(Qt::UserRole, index);
    }
    if (m_history.size() > 0) {
        auto *watcher = new QFutureWatcher<QList<QImage>>(this);
        connect(watcher, &QFutureWatcher<QList<QImage>>::finished, this,
            [this, watcher, generation, revision] {
                const QList<QImage> thumbnails = watcher->result();
                watcher->deleteLater();
                if (generation != m_refreshGeneration || revision != m_history.revision())
                    return;
                for (int index = 0; index < thumbnails.size() && index < m_list->count(); ++index)
                    m_list->item(index)->setIcon(QIcon(QPixmap::fromImage(thumbnails.at(index))));
            });
        watcher->setFuture(QtConcurrent::run([snapshot = m_history] {
            QList<QImage> thumbnails;
            for (int index = 0; index < snapshot.size(); ++index)
                thumbnails.append(snapshot.thumbnailAt(index));
            return thumbnails;
        }));
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(qBound(0, previousRow, m_list->count() - 1));
    else
        updatePreview();
}

QImage ScreenshotHistoryDialog::selectedImage() const
{
    const auto *item = m_list->currentItem();
    if (!item)
        return {};
    const int index = item->data(Qt::UserRole).toInt();
    if (index < 0 || index >= m_history.size())
        return {};
    return m_history.imageAt(index);
}

void ScreenshotHistoryDialog::updatePreview()
{
    ++m_previewGeneration;
    m_previewPending = false;
    const int index = m_list->currentRow();
    const bool hasImage = index >= 0 && index < m_history.size();
    for (auto *button : {m_copyButton, m_pasteButton, m_saveButton, m_clearButton})
        button->setEnabled(hasImage || button == m_clearButton);
    if (!hasImage) {
        m_preview->setPixmap({});
        m_preview->setText(QStringLiteral("暂无截屏历史"));
        return;
    }
    m_preview->setPixmap({});
    m_preview->setText(QStringLiteral("正在加载…"));
    // Keep one decode in flight. Rapid selection changes replace the next
    // request instead of queueing full-size image decodes for obsolete rows.
    m_previewPending = true;
    if (!m_previewWatcher)
        startPreviewLoad();
}

void ScreenshotHistoryDialog::startPreviewLoad()
{
    m_previewPending = false;
    const quint64 generation = m_previewGeneration;
    const quint64 revision = m_history.revision();
    const int index = m_list->currentRow();
    auto *watcher = new QFutureWatcher<QImage>(this);
    m_previewWatcher = watcher;
    connect(watcher, &QFutureWatcher<QImage>::finished, this,
        [this, watcher, generation, revision] {
            const QImage preview = watcher->result();
            m_previewWatcher = nullptr;
            watcher->deleteLater();
            if (generation == m_previewGeneration && revision == m_history.revision()) {
                m_preview->setText(preview.isNull() ? QStringLiteral("无法读取截图") : QString());
                m_preview->setPixmap(QPixmap::fromImage(preview));
                for (auto *button : {m_copyButton, m_pasteButton, m_saveButton})
                    button->setEnabled(!preview.isNull());
            }
            if (m_previewPending)
                startPreviewLoad();
        });
    watcher->setFuture(QtConcurrent::run([snapshot = m_history, index, size = m_preview->size()] {
        return snapshot.imageAt(index).scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }));
}

} // namespace snipaste::ui
