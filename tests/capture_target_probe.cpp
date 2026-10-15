#include "platform/windows/capture_target.h"

#include <QGuiApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QWindow>
#include <iostream>
#include <thread>

// Read-only native diagnostic: creates a hidden HWND to establish the monitor
// transform, then queries windows beneath several points without moving the
// pointer, sending input, showing windows, or changing the active application.
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    auto *screen = app.primaryScreen();
    if (!screen)
        return 1;
    QWindow anchor;
    anchor.setScreen(screen);
    anchor.setGeometry(screen->geometry());
    anchor.create();
    const QSize size = screen->geometry().size();
    const auto array = [](QRect rect) {
        return QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
    };
    QJsonArray results;
    QList<snipaste::platform::CaptureTargetQuery> queries;
    for (int pass = 0; pass < 3; ++pass)
        for (const QPoint point : {QPoint(25, 30), QPoint(25, 130), QPoint(110, 30),
                                   QPoint(size.width() / 2, size.height() / 2)})
            queries.append(snipaste::platform::prepareCaptureTargetQuery(point, size, anchor.winId(), true));
    // Match the capture worker's COM apartment and include cold/warm timings.
    std::thread worker([&] {
        for (const auto &query : queries) {
            auto windowQuery = query;
            windowQuery.elementMode = false;
            QElapsedTimer timer;
            timer.start();
            const auto windows = snipaste::platform::captureTargetHierarchy(windowQuery);
            const double windowMs = timer.nsecsElapsed() / 1e6;
            timer.restart();
            const auto elements = snipaste::platform::captureTargetHierarchy(query);
            const double elementMs = timer.nsecsElapsed() / 1e6;
            results.append(QJsonObject{{"point", QJsonArray{query.localPosition.x(), query.localPosition.y()}},
                {"window", array(windows.value(0))}, {"element", array(elements.value(0))},
                {"windowMs", windowMs}, {"elementMs", elementMs}});
        }
    });
    worker.join();
    const QJsonObject report{{"platform", app.platformName()},
        {"screenSize", QJsonArray{size.width(), size.height()}},
        {"devicePixelRatio", screen->devicePixelRatio()}, {"samples", results}};
    std::cout << QJsonDocument(report).toJson().constData();
}
