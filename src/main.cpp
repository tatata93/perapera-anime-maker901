#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QImage>
#include <QTimer>
#include <algorithm>
#include <cmath>

#include "core/Multiplane.h"
#include "previz/PrevizViewport.h"
#include "previz/PrevizWindow.h"
#include "render/GLCanvas.h"
#include "ui/EditWindow.h"
#include "ui/MainWindow.h"
#include "ui/SettingBoardWindow.h"
#include "ui/ShootingWindow.h"
#include "ui/StoryboardWindow.h"

namespace {

// core::BitmapをQImage(Format_RGBA8888)に変換する(--multiplane-testの保存用)
QImage bitmapToQImage(const core::Bitmap& bmp) {
    QImage image(bmp.width(), bmp.height(), QImage::Format_RGBA8888);
    for (int y = 0; y < bmp.height(); ++y) {
        for (int x = 0; x < bmp.width(); ++x) {
            const core::Bitmap::Pixel p = bmp.pixel(x, y);
            image.setPixelColor(x, y, QColor(p.r, p.g, p.b, p.a));
        }
    }
    return image;
}

// --multiplane-test用のデモアートワーク「背景」: 薄青地+グリッド線
core::Bitmap makeMultiplaneBackgroundArt() {
    core::Bitmap bmp(160, 160);
    bmp.fill({210, 230, 245, 255});  // 薄青
    for (int i = 0; i <= 160; i += 20) {
        for (int y = 0; y < 160; ++y) {
            if (i < 160) bmp.setPixel(i, y, {120, 150, 190, 255});
        }
        for (int x = 0; x < 160; ++x) {
            if (i < 160) bmp.setPixel(x, i, {120, 150, 190, 255});
        }
    }
    return bmp;
}

// --multiplane-test用のデモアートワーク「セルA」: 透明地に赤い矩形枠(中央)
core::Bitmap makeMultiplaneCelAArt() {
    core::Bitmap bmp(80, 80);
    bmp.fill({0, 0, 0, 0});
    const int x0 = 20, y0 = 20, x1 = 59, y1 = 59;
    for (int x = x0; x <= x1; ++x) {
        bmp.setPixel(x, y0, {220, 30, 30, 255});
        bmp.setPixel(x, y1, {220, 30, 30, 255});
    }
    for (int y = y0; y <= y1; ++y) {
        bmp.setPixel(x0, y, {220, 30, 30, 255});
        bmp.setPixel(x1, y, {220, 30, 30, 255});
    }
    return bmp;
}

// --multiplane-test用のデモアートワーク「セルB」: 透明地に左寄りの緑の丸
core::Bitmap makeMultiplaneCelBArt() {
    core::Bitmap bmp(60, 60);
    bmp.fill({0, 0, 0, 0});
    const double cx = 20.0, cy = 30.0, r = 7.0;
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 60; ++x) {
            const double dx = x - cx;
            const double dy = y - cy;
            if (dx * dx + dy * dy <= r * r) bmp.setPixel(x, y, {40, 170, 70, 255});
        }
    }
    return bmp;
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 動作確認用: --multiplane-test <出力PNG> でマルチプレーン撮影台のデモシーン
    // (奥=背景/中=セルA/手前=セルB、セルAにピント)をレイトレースし、
    // ウィンドウを開かずにPNGへ保存して終了する
    {
        const QStringList earlyArgs = app.arguments();
        const int multiplaneIndex = earlyArgs.indexOf("--multiplane-test");
        if (multiplaneIndex >= 0 && multiplaneIndex + 1 < earlyArgs.size()) {
            const QString outputPath = earlyArgs.at(multiplaneIndex + 1);

            static core::Bitmap backgroundArt = makeMultiplaneBackgroundArt();
            static core::Bitmap celAArt = makeMultiplaneCelAArt();
            static core::Bitmap celBArt = makeMultiplaneCelBArt();

            core::MultiplanePlane background;
            background.artwork = &backgroundArt;
            background.distanceMm = 1000.0;
            background.widthMm = 800.0;

            core::MultiplanePlane celA;
            celA.artwork = &celAArt;
            celA.distanceMm = 500.0;
            celA.widthMm = 400.0;

            // セルB自体のアートワーク内で丸を左寄りに描いているため、平面自体は中央(offset 0)でよい
            core::MultiplanePlane celB;
            celB.artwork = &celBArt;
            celB.distanceMm = 300.0;
            celB.widthMm = 300.0;

            core::MultiplaneCamera camera;
            camera.focalLengthMm = 50.0;
            camera.sensorWidthMm = 36.0;
            camera.apertureFStop = 2.0;
            camera.focusDistanceMm = 500.0;  // セルAにピント

            const core::Bitmap result =
                core::renderMultiplane({background, celA, celB}, camera, 960, 540, 16, 1);
            bitmapToQImage(result).save(outputPath);
            return 0;
        }
    }

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

    // 動作確認用: --frameguide-test <出力PNG> でストロークを描いた上でフレーム枠ガイドを
    // 有効にし、フレームバッファを保存して終了する
    const int frameGuideIndex = args.indexOf("--frameguide-test");
    if (frameGuideIndex >= 0 && frameGuideIndex + 1 < args.size()) {
        const QString outputPath = args.at(frameGuideIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.canvas()->debugSimulateStroke();
            window.canvas()->setFrameGuides(true);
            QTimer::singleShot(200, &window, [&window, outputPath] {
                window.canvas()->grabFramebuffer().save(outputPath);
                QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
            });
        });
    }

    // 動作確認用: --camframe-test <出力PNG> でカメラフレームデモ(ストローク+コマ0/23に
    // カメラキー)を組んでコマ12へ移動し、カメラパネルとオーバーレイ枠が見える状態で
    // ウィンドウ全体(パネル込み)を保存して終了する
    const int camFrameIndex = args.indexOf("--camframe-test");
    if (camFrameIndex >= 0 && camFrameIndex + 1 < args.size()) {
        const QString outputPath = args.at(camFrameIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupCameraDemo();
            QTimer::singleShot(300, &window, [&window, outputPath] {
                window.grab().save(outputPath);
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

    // 動作確認用: --oversize-test <出力PNG> で引きセル(セルの用紙サイズをキャンバスより
    // 大きくする)を検証する。アクティブセルを横2倍にリサイズし、左半分=赤/右半分=青の
    // 縦線を描いた上で位置キー(コマ0=0、コマ2=-キャンバス幅)を打ち、コマ2(右半分=青が
    // 見えているはず)へ移動した状態を保存して終了する
    const int oversizeIndex = args.indexOf("--oversize-test");
    if (oversizeIndex >= 0 && oversizeIndex + 1 < args.size()) {
        const QString outputPath = args.at(oversizeIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupOversizeDemo();
            QTimer::singleShot(200, &window, [&window, outputPath] {
                window.debugSetCurrentFrame(2);
                QTimer::singleShot(150, &window, [&window, outputPath] {
                    window.canvas()->grabFramebuffer().save(outputPath);
                    QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
                });
            });
        });
    }

    // 動作確認用: --move-test <PNG1> <PNG2> で移動ツール(タップ/ペグ移動)のドラッグを検証する。
    // 矩形枠を描いた状態を保存後、中央から(120,60)ドラッグして移動後の状態を保存する
    // (PNG1→PNG2で矩形が右下へ移動しているはず)
    const int moveIndex = args.indexOf("--move-test");
    if (moveIndex >= 0 && moveIndex + 2 < args.size()) {
        const QString out1 = args.at(moveIndex + 1);
        const QString out2 = args.at(moveIndex + 2);
        QTimer::singleShot(500, &window, [&window, out1, out2] {
            window.debugSetupFillDemo();  // 閉じた矩形枠(黒)を描く
            QTimer::singleShot(200, &window, [&window, out1, out2] {
                window.canvas()->grabFramebuffer().save(out1);  // 移動前
                window.canvas()->debugSimulateMoveDrag(QPointF(120, 60));
                QTimer::singleShot(200, &window, [&window, out2] {
                    window.canvas()->grabFramebuffer().save(out2);  // 移動後(右下へ移動)
                    QApplication::exit(0);
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

    // 動作確認用: --previz-test <出力PNG> でプリビズウィンドウを開き、
    // 3Dビューポート(グリッド+目安キューブ、物理カメラ)を保存して終了する
    const int previzIndex = args.indexOf("--previz-test");
    if (previzIndex >= 0 && previzIndex + 1 < args.size()) {
        const QString outputPath = args.at(previzIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugOpenPreviz();
            QTimer::singleShot(400, &window, [&window, outputPath] {
                window.previzWindow()->viewport()->grabFramebuffer().save(outputPath);
                QApplication::exit(0);
            });
        });
    }

    // 動作確認用: --previz-ui-test <出力PNG> でプリビズウィンドウ全体
    // (プリビズシート+十字リモコンの操作ドックを含む)を保存して終了する
    const int previzUiIndex = args.indexOf("--previz-ui-test");
    if (previzUiIndex >= 0 && previzUiIndex + 1 < args.size()) {
        const QString outputPath = args.at(previzUiIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugOpenPreviz();
            QTimer::singleShot(400, &window, [&window, outputPath] {
                window.previzWindow()->grab().save(outputPath);
                QApplication::exit(0);
            });
        });
    }

    // 動作確認用: --previz-underlay-test <出力PNG> でプリビズの絵(グリッド+キューブ)を
    // 作画キャンバスの下敷きとして透かした状態を保存する(なぞり作画の検証)
    const int pvuIndex = args.indexOf("--previz-underlay-test");
    if (pvuIndex >= 0 && pvuIndex + 1 < args.size()) {
        const QString outputPath = args.at(pvuIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugOpenPreviz();
            QTimer::singleShot(400, &window, [&window, outputPath] {
                window.debugSetPrevizUnderlay(true);
                window.canvas()->debugSimulateStroke();  // 下敷きの上に手描き線
                QTimer::singleShot(300, &window, [&window, outputPath] {
                    window.canvas()->grabFramebuffer().save(outputPath);
                    QApplication::exit(0);
                });
            });
        });
    }

    // 動作確認用: --previz-pen-test <出力PNG> でプリビズ(箱モデル)を作画キャンバスの下敷きに表示し、
    // 筆圧ペンでなぞり描き(debugSimulateStroke)した状態を保存する。--previz-underlay-testと
    // ほぼ同じ手順だが、「プリビズを下敷きに筆圧ペンでなぞっている」証跡として別名で確認できるようにする
    const int previzPenIndex = args.indexOf("--previz-pen-test");
    if (previzPenIndex >= 0 && previzPenIndex + 1 < args.size()) {
        const QString outputPath = args.at(previzPenIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugOpenPreviz();
            QTimer::singleShot(400, &window, [&window, outputPath] {
                window.debugSetPrevizUnderlay(true);
                window.canvas()->debugSimulateStroke();  // 下敷きの上に筆圧ペンでなぞり描き
                QTimer::singleShot(300, &window, [&window, outputPath] {
                    window.canvas()->grabFramebuffer().save(outputPath);
                    QApplication::exit(0);
                });
            });
        });
    }

    // 動作確認用: --storyboard-test <出力PNG> で絵コンテデモ(パネル2枚、カット番号1が2つ、
    // 尺36/12、パネル1に赤い斜め線)を組んでから絵コンテウィンドウを開き、
    // その全体(パネル表+描画エリア)を保存して終了する
    const int storyboardIndex = args.indexOf("--storyboard-test");
    if (storyboardIndex >= 0 && storyboardIndex + 1 < args.size()) {
        const QString outputPath = args.at(storyboardIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupStoryboardDemo();
            window.debugOpenStoryboard();
            QTimer::singleShot(400, &window, [&window, outputPath] {
                window.storyboardWindow()->grab().save(outputPath);
                QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
            });
        });
    }

    // 動作確認用: --storyboard-zoom-test <出力PNG> で絵コンテデモを組んで絵コンテウィンドウを開き、
    // debugZoomToFrame()で絵の枠を拡大表示させた状態のウィンドウ全体を保存して終了する
    const int storyboardZoomIndex = args.indexOf("--storyboard-zoom-test");
    if (storyboardZoomIndex >= 0 && storyboardZoomIndex + 1 < args.size()) {
        const QString outputPath = args.at(storyboardZoomIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupStoryboardDemo();
            window.debugOpenStoryboard();
            QTimer::singleShot(300, &window, [&window, outputPath] {
                window.storyboardWindow()->debugZoomToFrame();
                QTimer::singleShot(200, &window, [&window, outputPath] {
                    window.storyboardWindow()->grab().save(outputPath);
                    QApplication::exit(0);  // exit()で直接終了(quit()はcloseEventのダイアログを経由するため)
                });
            });
        });
    }

    // 動作確認用: --storyboard-preview-test <出力PNG> で絵コンテデモを組んで絵コンテウィンドウを開き、
    // debugOpenPreview()でプレビュー(ビデオコンテ)ダイアログを開いて自動再生させ、
    // 数百ms後にダイアログのスナップショットを保存して終了する
    const int storyboardPreviewIndex = args.indexOf("--storyboard-preview-test");
    if (storyboardPreviewIndex >= 0 && storyboardPreviewIndex + 1 < args.size()) {
        const QString outputPath = args.at(storyboardPreviewIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupStoryboardDemo();
            window.debugOpenStoryboard();
            QTimer::singleShot(300, &window, [&window, outputPath] {
                window.storyboardWindow()->debugOpenPreview();
                QTimer::singleShot(400, &window, [&window, outputPath] {
                    if (QDialog* dialog = window.storyboardWindow()->debugPreviewDialog()) {
                        dialog->grab().save(outputPath);
                    }
                    QApplication::exit(0);  // exit()で直接終了(quit()はcloseEventのダイアログを経由するため)
                });
            });
        });
    }

    // 動作確認用: --settingboard-test <出力PNG> で設定ボードデモ(ボード2枚、1枚目に赤い線+色指定3色)を
    // 組んでから設定ボードウィンドウを開き、その全体(ボード一覧+描画エリア+色指定)を保存する。
    // 併せてメインウィンドウ(参照ドックの色指定込み)も「ベース名_main.png」で保存して終了する
    const int settingBoardIndex = args.indexOf("--settingboard-test");
    if (settingBoardIndex >= 0 && settingBoardIndex + 1 < args.size()) {
        const QString outputPath = args.at(settingBoardIndex + 1);
        const int dotIndex = outputPath.lastIndexOf('.');
        const QString mainOutputPath = dotIndex >= 0 ? outputPath.left(dotIndex) + "_main" + outputPath.mid(dotIndex)
                                                       : outputPath + "_main";
        QTimer::singleShot(500, &window, [&window, outputPath, mainOutputPath] {
            window.debugSetupSettingBoardDemo();
            window.debugOpenSettingBoard();
            QTimer::singleShot(400, &window, [&window, outputPath, mainOutputPath] {
                window.settingBoardWindow()->grab().save(outputPath);
                window.grab().save(mainOutputPath);  // 参照ドック(色指定含む)を確認するためメインウィンドウも保存
                QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
            });
        });
    }

    // 動作確認用: --edit-test <出力PNG> で編集(カッティング)デモ(カット3つ、尺12/24/12、
    // 進捗: 原画/レイアウト/未着手、カット1に赤ストローク・カット2に青ストローク)を組み、
    // 編集ウィンドウを開いてグローバルコマ18(カット2内)へシークした状態を保存して終了する
    const int editIndex = args.indexOf("--edit-test");
    if (editIndex >= 0 && editIndex + 1 < args.size()) {
        const QString outputPath = args.at(editIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupEditDemo();
            QTimer::singleShot(400, &window, [&window, outputPath] {
                window.editWindow()->grab().save(outputPath);
                QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
            });
        });
    }

    // 動作確認用: --shooting-test <出力PNG> で撮影デモ(ストローク1本+尺24の止め+
    // 全体ブラーのコマキー(コマ0=半径0→コマ23=半径10)+全体パラ)を組み、撮影ウィンドウ
    // (シートに●2つ、コマ12選択=中間ボケのプレビュー)を保存して終了する
    const int shootingIndex = args.indexOf("--shooting-test");
    if (shootingIndex >= 0 && shootingIndex + 1 < args.size()) {
        const QString outputPath = args.at(shootingIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupShootingDemo();
            QTimer::singleShot(400, &window, [&window, outputPath] {
                window.shootingWindow()->grab().save(outputPath);
                QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
            });
        });
    }

    // 動作確認用: --shooting-mask-test <出力PNG> で撮影デモ(--shooting-testと同じ、ブラーに
    // 画面右半分マスク付き)を組んでから、そのブラー(エフェクトindex0)のマスク編集ダイアログ
    // (GLCanvas+ツール行)を開いた状態を保存して終了する
    const int shootingMaskIndex = args.indexOf("--shooting-mask-test");
    if (shootingMaskIndex >= 0 && shootingMaskIndex + 1 < args.size()) {
        const QString outputPath = args.at(shootingMaskIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupShootingDemo();
            window.shootingWindow()->debugOpenMaskEditDialog(0);  // ブラー(index0)のマスク編集ダイアログを開く
            QTimer::singleShot(400, &window, [&window, outputPath] {
                QWidget* dialog = window.shootingWindow()->maskEditDialogWidget();
                if (dialog) dialog->grab().save(outputPath);
                QApplication::exit(dialog ? 0 : 1);  // quit()はcloseEvent経由のためexit()で直接終了する
            });
        });
    }

    // 動作確認用: --classic-test <出力PNG> でクラシック撮影(マルチプレーン撮影台)デモ
    // (セルA=赤い矩形枠[距離500mm]・セルB=左寄り緑丸[距離300mm]、f/2.0・フォーカス500mm・
    // samples=8)を組み、撮影ウィンドウ(クラシック撮影グループON+プレビュー)を保存して終了する
    const int classicIndex = args.indexOf("--classic-test");
    if (classicIndex >= 0 && classicIndex + 1 < args.size()) {
        const QString outputPath = args.at(classicIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupClassicDemo();
            // マルチプレーンのレイトレースはプレビュー描画に時間がかかりうるため、grabまでの待ちを長めに取る
            QTimer::singleShot(800, &window, [&window, outputPath] {
                window.shootingWindow()->grab().save(outputPath);
                QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
            });
        });
    }

    // 動作確認用: --fulldemo <出力mp4パス> でこれまで実装した全機能を統合した4カットデモ
    // (カット0: プリビズなぞり作画 / カット1: PAN+T.U.+グロー / カット2: クラシック撮影DoF+黒パラ /
    // カット3: シェイク+オレンジパラ)を組み、全カットを連結した通しmp4へ書き出す。
    // カット0はプリビズのカメラ視点画像(FBOレンダ)を下敷きに焼き込むため、GLコンテキストの
    // 初期化(initializeGL)が済んでいる必要がある。そのため先にプリビズウィンドウを開いて
    // 表示・描画が一巡するのを待ってから(500→400ms)debugBuildFullDemo()を呼ぶ2段構えにする。
    // マルチプレーンのレイトレース+ffmpegエンコードで時間がかかるため、待ち時間は長めに取る。
    // mp4の成否によらず代表PNG(fulldemo_cut1〜4.png)は保存されるので、mp4が失敗してもPNGで
    // 目視確認できる
    const int fullDemoIndex = args.indexOf("--fulldemo");
    if (fullDemoIndex >= 0 && fullDemoIndex + 1 < args.size()) {
        const QString outputPath = args.at(fullDemoIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugOpenPreviz();  // GLコンテキスト初期化のため先に開いておく(カット0のFBOレンダに必要)
            QTimer::singleShot(400, &window, [&window, outputPath] {
                window.debugBuildFullDemo();  // カット0(プリビズなぞり作画)込みの4カットを構築
                QTimer::singleShot(1000, &window, [&window, outputPath] {
                    const bool ok = window.exportAllCutsMovie(outputPath, 24);
                    QApplication::exit(ok ? 0 : 1);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
                });
            });
        });
    }

    return app.exec();
}
