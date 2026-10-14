#include "core/history/screenshot_history_writer.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QThread>

namespace snipaste::core {

ScreenshotHistoryWriter::ScreenshotHistoryWriter(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(3000);
}

ScreenshotHistoryWriter::~ScreenshotHistoryWriter()
{
    // Finish persistence before the history owner is destroyed at app shutdown.
    waitForDone();
}

void ScreenshotHistoryWriter::waitForDone()
{
    m_pool.waitForDone();
}

void ScreenshotHistoryWriter::save(const ScreenshotHistory &history, const QString &directory)
{
    Q_ASSERT(QThread::currentThread() == thread());
    QMutexLocker locker(&m_mutex);
    m_pending = Request{history, directory, ++m_generation};
    if (m_running)
        return;
    m_running = true;
    m_pool.start([this] { drain(); });
}

void ScreenshotHistoryWriter::drain()
{
    for (;;) {
        std::optional<Request> request;
        {
            QMutexLocker locker(&m_mutex);
            if (!m_pending) {
                m_running = false;
                return;
            }
            request = std::move(m_pending);
            m_pending.reset();
        }
        const bool ok = request->history.saveToDirectory(request->directory);
        // Queue only the result, never the images. Reject results overtaken by
        // a newer capture so the GUI cannot rebase paths to an older index.
        QMetaObject::invokeMethod(this,
            [this, ok, generation = request->generation,
             revision = request->history.revision(), directory = request->directory] {
                {
                    QMutexLocker locker(&m_mutex);
                    if (generation != m_generation)
                        return;
                }
                if (ok)
                    emit saved(revision, directory);
                else
                    emit failed(directory);
            }, Qt::QueuedConnection);
    }
}

} // namespace snipaste::core
