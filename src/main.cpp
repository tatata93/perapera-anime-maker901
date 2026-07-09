#include <QApplication>
#include <QTimer>

#include "render/GLCanvas.h"
#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    MainWindow window;
    window.resize(1280, 800);
    window.show();

    // 動作確認用: --stroke-test <出力PNG> でストロークを自動描画し、
    // フレームバッファを保存して終了する
    const QStringList args = app.arguments();
    const int testIndex = args.indexOf("--stroke-test");
    if (testIndex >= 0 && testIndex + 1 < args.size()) {
        const QString outputPath = args.at(testIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.canvas()->debugSimulateStroke();
            QTimer::singleShot(200, &window, [&window, outputPath] {
                window.canvas()->grabFramebuffer().save(outputPath);
                QApplication::quit();
            });
        });
    }

    return app.exec();
}
