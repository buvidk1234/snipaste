#include "ui/snip/capture_target_worker.h"

#include <QMutexLocker>

namespace snipaste::ui
{

CaptureTargetWorker::CaptureTargetWorker()
{
    // The overlay never waits for a remote accessibility provider on close.
    // This parentless thread owns its remaining query until it can finish.
    connect(this, &QThread::finished, this, &QObject::deleteLater);
}

void CaptureTargetWorker::submit(quint64 requestId, std::function<QList<QRect>()> query)
{
    const QMutexLocker lock(&m_mutex);
    if (m_stopping)
        return;
    m_pending = Request{requestId, std::move(query)};
    m_ready.wakeOne();
}

void CaptureTargetWorker::cancelPending()
{
    const QMutexLocker lock(&m_mutex);
    m_pending.reset();
}

void CaptureTargetWorker::stop()
{
    const QMutexLocker lock(&m_mutex);
    m_stopping = true;
    m_pending.reset();
    m_ready.wakeOne();
}

void CaptureTargetWorker::run()
{
    for (;;)
    {
        Request request;
        {
            QMutexLocker lock(&m_mutex);
            while (!m_stopping && !m_pending)
                m_ready.wait(&m_mutex);
            if (m_stopping)
                return;
            request = std::move(*m_pending);
            m_pending.reset();
        }
        const auto targets = request.query();
        {
            const QMutexLocker lock(&m_mutex);
            if (m_stopping)
                return;
        }
        emit targetsReady(request.id, targets);
    }
}

} // namespace snipaste::ui
