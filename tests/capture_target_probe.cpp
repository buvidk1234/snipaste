#include "platform/windows/capture_target.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QWindow>
#include <iostream>

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
    for (const QPoint point : {QPoint(25, 30), QPoint(25, 130), QPoint(110, 30),
                               QPoint(size.width() / 2, size.height() / 2)}) {
        const QRect window = snipate::platform::captureTargetAt(point, size, anchor.winId(), false);
        const QRect element = snipate::platform::captureTargetAt(point, size, anchor.winId(), true);
        results.append(QJsonObject{{"point", QJsonArray{point.x(), point.y()}},
                                   {"window", array(window)}, {"element", array(element)}});
    }
    const QJsonObject report{{"platform", app.platformName()},
        {"screenSize", QJsonArray{size.width(), size.height()}},
        {"devicePixelRatio", screen->devicePixelRatio()}, {"samples", results}};
    std::cout << QJsonDocument(report).toJson().constData();
}
