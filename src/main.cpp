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

    // 動作確認用: --cels-test <PNG1> <PNG2> でセル2枚(A=赤縦線/B=青横線)を重ねた状態を保存後、
    // セルAを非表示にして保存する(PNG2は青横線のみになるはず)
    const int celsIndex = args.indexOf("--cels-test");
    if (celsIndex >= 0 && celsIndex + 2 < args.size()) {
        const QString out1 = args.at(celsIndex + 1);
        const QString out2 = args.at(celsIndex + 2);
        QTimer::singleShot(500, &window, [&window, out1, out2] {
            window.debugSetupCelDemo();
            QTimer::singleShot(200, &window, [&window, out1, out2] {
                window.canvas()->grabFramebuffer().save(out1);  // セルA(赤縦線)+セルB(青横線)を重ねて表示
                window.debugSetCelVisible(0, false);             // セルAを非表示
                QTimer::singleShot(200, &window, [&window, out2] {
                    window.canvas()->grabFramebuffer().save(out2);  // セルB(青横線)のみ
                    QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
                });
            });
        });
    }

    // 動作確認用: --drawing-delete-test <PNG1> <PNG2> でオニオンデモ(動画3枚、1コマ打ち、
    // コマ2=動画2を表示)を組んだ状態を保存後、動画2を削除して(コマ2が空欄になり、
    // 旧動画3が動画2に詰まる)再度保存する
    const int drawingDeleteIndex = args.indexOf("--drawing-delete-test");
    if (drawingDeleteIndex >= 0 && drawingDeleteIndex + 2 < args.size()) {
        const QString out1 = args.at(drawingDeleteIndex + 1);
        const QString out2 = args.at(drawingDeleteIndex + 2);
        QTimer::singleShot(500, &window, [&window, out1, out2] {
            window.debugSetupOnionDemo();  // 動画3枚、1コマ打ち、現在コマ2(動画2)を表示
            QTimer::singleShot(200, &window, [&window, out1, out2] {
                window.canvas()->grabFramebuffer().save(out1);  // 動画2(黒縦線)
                window.debugDeleteDrawing(1);                    // 動画2を削除
                QTimer::singleShot(200, &window, [&window, out2] {
                    window.canvas()->grabFramebuffer().save(out2);  // コマ2は空欄→白紙
                    QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
                });
            });
        });
    }

    // 動作確認用: --cels-ui-test <出力PNG> でセルデモ(セルA/セルB)のウィンドウ全体を保存する。
    // タイムシートの列反転確認用(セルB列が左、セルA列が右に表示されるはず)
    const int celsUiIndex = args.indexOf("--cels-ui-test");
    if (celsUiIndex >= 0 && celsUiIndex + 1 < args.size()) {
        const QString outputPath = args.at(celsUiIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupCelDemo();
            QTimer::singleShot(200, &window, [&window, outputPath] {
                window.grab().save(outputPath);
                QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
            });
        });
    }

    // 動作確認用: --colortrace-test <PNG1> <PNG2> で塗分け線ワークフローを検証する。
    // 矩形枠+赤トレス縦線→左半分を塗る→PNG1(トレス線表示)→仕上げ表示+右半分を塗る→PNG2
    // (PNG2でトレス線は見えないが境界として効いていること=右半分だけ塗られることを確認)
    const int traceIndex = args.indexOf("--colortrace-test");
    if (traceIndex >= 0 && traceIndex + 2 < args.size()) {
        const QString out1 = args.at(traceIndex + 1);
        const QString out2 = args.at(traceIndex + 2);
        QTimer::singleShot(500, &window, [&window, out1, out2] {
            window.debugSetupColorTraceDemo();
            window.canvas()->setPenColor(QColor(255, 160, 40));  // オレンジ
            const qreal w = window.canvas()->width();
            const qreal h = window.canvas()->height();
            window.canvas()->debugFillAt(QPointF(w * 0.42, h * 0.5));  // 左半分
            QTimer::singleShot(200, &window, [&window, out1, out2, w, h] {
                window.canvas()->grabFramebuffer().save(out1);
                window.debugSetCleanView(true);  // トレス線を隠す
                window.canvas()->setPenColor(QColor(60, 180, 90));  // 緑
                window.canvas()->debugFillAt(QPointF(w * 0.58, h * 0.5));  // 右半分(隠れた線が境界)
                QTimer::singleShot(200, &window, [&window, out2] {
                    window.canvas()->grabFramebuffer().save(out2);
                    QApplication::exit(0);
                });
            });
        });
    }

    // 動作確認用: --lighttable-test <出力PNG> でオニオンデモ(動画3枚、1コマ打ち、
    // コマ2=動画2を表示)を組んだ後、動画1・3をライトテーブル(青系透かし)に指定し、
    // オニオンスキンは無効化した状態のフレームバッファを保存する。
    // 期待結果: 青系の縦線2本(動画1・3)+黒の実線1本(動画2、通常表示)
    const int lightTableIndex = args.indexOf("--lighttable-test");
    if (lightTableIndex >= 0 && lightTableIndex + 1 < args.size()) {
        const QString outputPath = args.at(lightTableIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupOnionDemo();  // 動画3枚、1コマ打ち、現在コマ2(動画2)を表示
            window.debugSetLightTable(QList<int>{0, 2});  // 動画1・3を透かし指定
            window.debugSetOnionEnabled(false);            // オニオンスキンは無効化
            QTimer::singleShot(200, &window, [&window, outputPath] {
                window.canvas()->grabFramebuffer().save(outputPath);
                QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
            });
        });
    }

    // 動作確認用: --export-test <出力フォルダ> でタイムシートデモ(尺6・2コマ打ち)を組んでから
    // 全コマを連番PNGで書き出す。debugExportSequence()の戻り値(0=成功)でそのままexitする
    const int exportIndex = args.indexOf("--export-test");
    if (exportIndex >= 0 && exportIndex + 1 < args.size()) {
        const QString outputDir = args.at(exportIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputDir] {
            const int result = window.debugExportSequence(outputDir);
            QApplication::exit(result);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
        });
    }

    // 動作確認用: --tap-test <PNG1> <PNG2> <PNG3> でタップ移動(位置キー補間)を検証する。
    // 矩形(止め)がコマ1→3で右下へ等速移動する様子を3枚保存する
    const int tapIndex = args.indexOf("--tap-test");
    if (tapIndex >= 0 && tapIndex + 3 < args.size()) {
        const QString out1 = args.at(tapIndex + 1);
        const QString out2 = args.at(tapIndex + 2);
        const QString out3 = args.at(tapIndex + 3);
        QTimer::singleShot(500, &window, [&window, out1, out2, out3] {
            window.debugSetupTapDemo();
            QTimer::singleShot(150, &window, [&window, out1, out2, out3] {
                window.canvas()->grabFramebuffer().save(out1);  // コマ1: 原点
                window.debugSetCurrentFrame(1);
                QTimer::singleShot(150, &window, [&window, out2, out3] {
                    window.canvas()->grabFramebuffer().save(out2);  // コマ2: 中間(200,100)
                    window.debugSetCurrentFrame(2);
                    QTimer::singleShot(150, &window, [&window, out3] {
                        window.canvas()->grabFramebuffer().save(out3);  // コマ3: (400,200)
                        QApplication::exit(0);
                    });
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
