#include <QApplication>
#include <QFile>
#include <QTimer>
#include <algorithm>

#include "render/GLCanvas.h"
#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    MainWindow window;
    window.resize(1280, 800);
    window.show();

    const QStringList args = app.arguments();

    // "--"で始まる動作確認用フックが1つもない場合のみクラッシュリカバリ確認を行う。
    // ヘッドレステスト実行時に復元ダイアログが出て止まってしまうのを防ぐ
    const bool hasTestFlag = std::any_of(args.begin(), args.end(), [](const QString& arg) { return arg.startsWith("--"); });
    if (!hasTestFlag) {
        window.checkAutosaveRecovery();
    }

    // 動作確認用: --stroke-test <出力PNG> でストロークを自動描画し、
    // フレームバッファを保存して終了する
    const int testIndex = args.indexOf("--stroke-test");
    if (testIndex >= 0 && testIndex + 1 < args.size()) {
        const QString outputPath = args.at(testIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.canvas()->debugSimulateStroke();
            QTimer::singleShot(200, &window, [&window, outputPath] {
                window.canvas()->grabFramebuffer().save(outputPath);
                QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
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
                QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
            });
        });
    }

    // 動作確認用: --view-test <出力PNG> でズーム2倍+回転15度+ストロークの表示を保存する
    const int viewIndex = args.indexOf("--view-test");
    if (viewIndex >= 0 && viewIndex + 1 < args.size()) {
        const QString outputPath = args.at(viewIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.canvas()->debugSimulateStroke();  // 変換前に画像座標で描く
            window.canvas()->debugSetView(2.0f, 15.0, QPointF(40, -20));
            QTimer::singleShot(200, &window, [&window, outputPath] {
                window.canvas()->grabFramebuffer().save(outputPath);
                QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
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
                        QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
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
                QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
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

    // 動作確認用: --autosave-test <出力PNG> でストローク描画→自動保存→新規(白紙化)→
    // 自動保存データの読み込みを行い、自動保存/クラッシュリカバリを一括検証する
    const int autosaveIndex = args.indexOf("--autosave-test");
    if (autosaveIndex >= 0 && autosaveIndex + 1 < args.size()) {
        const QString outputPath = args.at(autosaveIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.canvas()->debugSimulateStroke();
            const QString autosavedPath = window.debugTriggerAutosave();
            window.debugNewDocument();  // 一度白紙に戻す
            const bool loaded = !autosavedPath.isEmpty() && window.debugLoadFrom(autosavedPath);
            QTimer::singleShot(200, &window, [&window, outputPath, autosavedPath, loaded] {
                window.canvas()->grabFramebuffer().save(outputPath);
                // テストで作った自動保存ファイルを残すと次回通常起動時に偽のリカバリ提案が出るため削除する
                if (!autosavedPath.isEmpty()) QFile::remove(autosavedPath);
                QApplication::exit((!autosavedPath.isEmpty() && loaded) ? 0 : 1);
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
                    QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
                });
            });
        });
    }

    // 動作確認用: --underlay-test <参照画像パス> <出力PNG> でストロークを描いた上に
    // 下敷き(参照画像)を重ねた状態のフレームバッファを保存して終了する
    const int underlayIndex = args.indexOf("--underlay-test");
    if (underlayIndex >= 0 && underlayIndex + 2 < args.size()) {
        const QString underlayPath = args.at(underlayIndex + 1);
        const QString outputPath = args.at(underlayIndex + 2);
        QTimer::singleShot(500, &window, [&window, underlayPath, outputPath] {
            window.canvas()->debugSimulateStroke();
            window.debugSetUnderlayFile(underlayPath);
            QTimer::singleShot(200, &window, [&window, outputPath] {
                window.canvas()->grabFramebuffer().save(outputPath);
                QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
            });
        });
    }

    // 動作確認用: --fill-test <出力PNG> で閉じた矩形枠を描き、その内側を塗りつぶして保存する
    const int fillIndex = args.indexOf("--fill-test");
    if (fillIndex >= 0 && fillIndex + 1 < args.size()) {
        const QString outputPath = args.at(fillIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupFillDemo();  // 閉じた矩形枠(黒)を描く
            window.canvas()->setPenColor(QColor(255, 140, 0));
            // フィット表示では画像中心=ウィジェット中心なので、中心をクリックすれば枠内が塗られる
            const QPointF center(window.canvas()->width() * 0.5, window.canvas()->height() * 0.5);
            window.canvas()->debugFillAt(center);
            QTimer::singleShot(200, &window, [&window, outputPath] {
                window.canvas()->grabFramebuffer().save(outputPath);
                QApplication::exit(0);
            });
        });
    }

    // 動作確認用: --layers-test <PNG1> <PNG2> でレイヤー2枚(赤縦線+青横線)を表示保存後、
    // 下レイヤーを非表示にして保存する(PNG2は青横線のみになるはず)
    const int layersIndex = args.indexOf("--layers-test");
    if (layersIndex >= 0 && layersIndex + 2 < args.size()) {
        const QString out1 = args.at(layersIndex + 1);
        const QString out2 = args.at(layersIndex + 2);
        QTimer::singleShot(500, &window, [&window, out1, out2] {
            window.debugSetupLayerDemo();
            QTimer::singleShot(200, &window, [&window, out1, out2] {
                window.canvas()->grabFramebuffer().save(out1);  // 両レイヤー表示
                window.debugSetLayerVisible(0, false);          // 下(赤)を非表示
                QTimer::singleShot(200, &window, [&window, out2] {
                    window.canvas()->grabFramebuffer().save(out2);  // 青のみ
                    QApplication::exit(0);
                });
            });
        });
    }

    // 動作確認用: --palette-test <ppamパス> でパレット追加→保存→新規→読込の往復を検証し、
    // 結果に応じた終了コードで終了する(0=成功, 1=不一致)
    const int paletteIndex = args.indexOf("--palette-test");
    if (paletteIndex >= 0 && paletteIndex + 1 < args.size()) {
        const QString ppamPath = args.at(paletteIndex + 1);
        QTimer::singleShot(500, &window, [&window, ppamPath] {
            const int result = window.debugPaletteRoundTrip(ppamPath);
            QApplication::exit(result);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
        });
    }

    // 動作確認用: --role-test <ppamパス> でレイヤー種別(色トレス線/作監修正)の設定→保存→新規→読込の
    // 往復を検証し、結果に応じた終了コードで終了する(0=成功, 1=不一致)
    const int roleIndex = args.indexOf("--role-test");
    if (roleIndex >= 0 && roleIndex + 1 < args.size()) {
        const QString ppamPath = args.at(roleIndex + 1);
        QTimer::singleShot(500, &window, [&window, ppamPath] {
            const int result = window.debugRoleRoundTrip(ppamPath);
            QApplication::exit(result);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
        });
    }

    // 動作確認用: --xsheet-test <PNG1> <PNG2> でタイムシート(尺6コマ・2コマ打ち)を組み、
    // コマ1(動画1)とコマ4(動画2)の表示差分(縦線の位置)を保存する
    const int xsheetIndex = args.indexOf("--xsheet-test");
    if (xsheetIndex >= 0 && xsheetIndex + 2 < args.size()) {
        const QString out1 = args.at(xsheetIndex + 1);
        const QString out2 = args.at(xsheetIndex + 2);
        QTimer::singleShot(500, &window, [&window, out1, out2] {
            window.debugSetupXsheetDemo();
            QTimer::singleShot(200, &window, [&window, out1, out2] {
                window.canvas()->grabFramebuffer().save(out1);  // コマ1=動画1
                window.debugSetCurrentFrame(3);
                QTimer::singleShot(200, &window, [&window, out2] {
                    window.canvas()->grabFramebuffer().save(out2);  // コマ4=動画2
                    QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
                });
            });
        });
    }

    // 動作確認用: --perf-test <出力TXT> でストローク描画+再描画を繰り返し、描画時間(ms)を出力する
    const int perfIndex = args.indexOf("--perf-test");
    if (perfIndex >= 0 && perfIndex + 1 < args.size()) {
        const QString outputPath = args.at(perfIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            for (int i = 0; i < 30; ++i) {
                window.canvas()->debugSimulateStroke();
                window.canvas()->grabFramebuffer();  // paintGLを強制実行(FBO+読み戻し込みの上限値)
            }
            QFile file(outputPath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                file.write(QStringLiteral("paint_ms_ema=%1\n").arg(window.canvas()->paintMillis(), 0, 'f', 3).toUtf8());
            }
            QApplication::exit(0);
        });
    }

    return app.exec();
}
