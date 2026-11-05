#include "mainwindow.h"

#include <QApplication>
#include <QFont>
#include <QScreen>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("plate_ui"));
    QApplication::setApplicationDisplayName(QStringLiteral("RK3568 智能车牌识别"));
#if defined(Q_OS_WIN)
    QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei UI")));
#endif

    MainWindow window;
#if defined(Q_OS_LINUX)
    // Use the screen size reported by the board's Wayland compositor.
    window.showFullScreen();
#else
    window.resize(400, 640);
    if (QScreen *screen = QApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        window.move(available.center() - window.rect().center());
    }
    window.show();
#endif
    return app.exec();
}
