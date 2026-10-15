#pragma once

#include <QList>
#include <QMutex>
#include <QRect>
#include <QThread>
#include <QWaitCondition>
#include <functional>
#include <optional>

namespace snipaste::ui
{

// Native accessibility providers may stall. Keep them off the GUI thread and
// retain only the latest pending query while a provider is busy.
class CaptureTargetWorker final : public QThread
{
    Q_OBJECT

  public:
    CaptureTargetWorker();
    void submit(quint64 requestId, std::function<QList<QRect>()> query);
    void cancelPending();
    void stop();

  signals:
    void targetsReady(quint64 requestId, const QList<QRect> &targets);

  protected:
    void run() override;

  private:
    struct Request
    {
        quint64 id;
        std::function<QList<QRect>()> query;
    };
    QMutex m_mutex;
    QWaitCondition m_ready;
    std::optional<Request> m_pending;
    bool m_stopping = false;
};

} // namespace snipaste::ui
