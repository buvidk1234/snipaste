#pragma once

#include "core/ai/ai_client.h"
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace snipaste::ui
{
class AiDialog final : public QWidget
{
    Q_OBJECT
  public:
    AiDialog(const QImage &image, core::AiSettings settings, QWidget *parent = nullptr);
    void placeNear(const QRect &selection, const QRect &bounds);

  signals:
    void closed();

  protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void send();
    void setBusy(bool busy);
    void scrollToEnd();
    void showAnswer(const QString &text, const QString &color);
    void showImages(const QList<QImage> &images);
    core::AiClient m_client;
    QScrollArea *m_history = nullptr;
    QVBoxLayout *m_messages = nullptr;
    QWidget *m_header = nullptr;
    QWidget *m_pendingTurn = nullptr;
    QWidget *m_failedTurn = nullptr;
    QLabel *m_answer = nullptr;
    QLabel *m_empty = nullptr;
    QPlainTextEdit *m_question = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_send = nullptr;
    QString m_pendingQuestion;
    QPoint m_dragOffset;
    bool m_dragging = false;
    bool m_composing = false;
};
} // namespace snipaste::ui
