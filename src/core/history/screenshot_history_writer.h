#pragma once

#include "core/history/screenshot_history.h"

#include <QMutex>
#include <QObject>
#include <QThreadPool>
#include <optional>

namespace snipaste::core {

// A single disk writer plus one replaceable pending snapshot. Rapid captures
// must not queue a thread and a desktop-sized image snapshot for every request.
class ScreenshotHistoryWriter final : public QObject
{
    Q_OBJECT

public:
    explicit ScreenshotHistoryWriter(QObject *parent = nullptr);
    ~ScreenshotHistoryWriter() override;
    void save(const ScreenshotHistory &history, const QString &directory);
    void waitForDone();

signals:
    void saved(quint64 revision, const QString &directory);
    void failed(const QString &directory);

private:
    struct Request
    {
        ScreenshotHistory history;
        QString directory;
        quint64 generation;
    };
    void drain();

    QThreadPool m_pool;
    QMutex m_mutex;
    std::optional<Request> m_pending;
    quint64 m_generation = 0;
    bool m_running = false;
};

} // namespace snipaste::core
