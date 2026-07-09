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

    // 動作確認用: --onion-test <出力PNG> で3フレーム分の線を描き、
    // オニオンスキン表示状態のフレームバッファを保存して終了する
    const int onionIndex = args.indexOf("--onion-test");
    if (onionIndex >= 0 && onionIndex + 1 < args.size()) {
        const QString outputPath = args.at(onionIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupOnionDemo();
            QTimer::singleShot(200, &window, [&window, outputPath] {
                window.canvas()->grabFramebuffer().save(outputPath);
                QApplication::quit();
            });
        });
    }

    // 動作確認用: --undo-test <PNG1> <PNG2> <PNG3> でストローク→Undo→Redoの3状態を保存する
    const int undoIndex = args.indexOf("--undo-test");
    if (undoIndex >= 0 && undoIndex + 3 < args.size()) {
        const QString out1 = args.at(undoIndex + 1);
        const QString out2 = args.at(undoIndex + 2);
        const QString out3 = args.at(undoIndex + 3);
        QTimer::singleShot(500, &window, [&window, out1, out2, out3] {
            window.canvas()->debugSimulateStroke();
            QTimer::singleShot(100, &window, [&window, out1, out2, out3] {
                window.canvas()->grabFramebuffer().save(out1);  // 線あり
                window.debugUndo();
                QTimer::singleShot(100, &window, [&window, out2, out3] {
                    window.canvas()->grabFramebuffer().save(out2);  // 白紙に戻る
                    window.debugRedo();
                    QTimer::singleShot(100, &window, [&window, out3] {
                        window.canvas()->grabFramebuffer().save(out3);  // 線が復元
                        QApplication::quit();
                    });
                });
            });
        });
    }

    // 動作確認用: --ui-test <出力PNG> でウィンドウ全体(メニュー/ツールバー/パネル込み)を保存する
    const int uiIndex = args.indexOf("--ui-test");
    if (uiIndex >= 0 && uiIndex + 1 < args.size()) {
        const QString outputPath = args.at(uiIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupOnionDemo();
            QTimer::singleShot(200, &window, [&window, outputPath] {
                window.grab().save(outputPath);
                QApplication::quit();
            });
        });
    }

    // 動作確認用: --io-test <ppamパス> <出力PNG> でストローク描画→保存→新規→
    // 読み込み→画面保存を行い、UI経由の保存/読み込みを一括検証する
    const int ioIndex = args.indexOf("--io-test");
    if (ioIndex >= 0 && ioIndex + 2 < args.size()) {
        const QString ppamPath = args.at(ioIndex + 1);
        const QString outputPath = args.at(ioIndex + 2);
        QTimer::singleShot(500, &window, [&window, ppamPath, outputPath] {
            window.canvas()->debugSimulateStroke();
            const bool saved = window.debugSaveTo(ppamPath);
            window.debugNewDocument();  // 一度白紙に戻す
            const bool loaded = window.debugLoadFrom(ppamPath);
            QTimer::singleShot(200, &window, [&window, outputPath, saved, loaded] {
                window.canvas()->grabFramebuffer().save(outputPath);
                QApplication::exit(saved && loaded ? 0 : 1);
            });
        });
    }

    // 動作確認用: --play-test <PNG1> <PNG2> で再生中の画面を2回保存して終了する。
    // 2枚が異なればフレームが切り替わっている
    const int playIndex = args.indexOf("--play-test");
    if (playIndex >= 0 && playIndex + 2 < args.size()) {
        const QString out1 = args.at(playIndex + 1);
        const QString out2 = args.at(playIndex + 2);
        QTimer::singleShot(500, &window, [&window, out1, out2] {
            window.debugSetupOnionDemo();
            window.debugTogglePlayback();  // 12fps → 約83ms間隔でフレームが進む
            QTimer::singleShot(150, &window, [&window, out1, out2] {
                window.canvas()->grabFramebuffer().save(out1);
                // 3フレーム×83ms=249msの再生周期と重ならない遅延にする
                QTimer::singleShot(320, &window, [&window, out2] {
                    window.canvas()->grabFramebuffer().save(out2);
                    QApplication::quit();
                });
            });
        });
    }

    return app.exec();
}
