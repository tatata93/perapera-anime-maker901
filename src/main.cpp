#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QTabWidget>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/Bitmap.h"
#include "core/Effect.h"
#include "core/EffectProcessor.h"
#include "core/Multiplane.h"
#include "previz/PrevizViewport.h"
#include "previz/PrevizWindow.h"
#include "render/GLCanvas.h"
#include "ui/EditWindow.h"
#include "ui/ExportDialog.h"
#include "ui/FloatingCanvasWindow.h"
#include "ui/MainWindow.h"
#include "ui/NewCutDialog.h"
#include "ui/NewProjectDialog.h"
#include "ui/ProjectManagerWindow.h"
#include "ui/RetroTheme.h"
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

// --previz-stl-test用: 既知の小さなバイナリSTL(1辺10のCAD由来立方体、12三角形、面ごとに
// 正しい法線)を一時ファイルへ書き出す。STL特有の「頂点共有せず面ごと独立・インデックス連番」を
// 素直に再現するための最小サンプル
void writeLeFloatTo(std::ofstream& out, float v) { out.write(reinterpret_cast<const char*>(&v), sizeof(float)); }
void writeLeU16To(std::ofstream& out, uint16_t v) { out.write(reinterpret_cast<const char*>(&v), sizeof(uint16_t)); }
void writeLeU32To(std::ofstream& out, uint32_t v) { out.write(reinterpret_cast<const char*>(&v), sizeof(uint32_t)); }

QString writeDebugCubeStl(const QString& path) {
    // path.toStdString()(UTF-8)をそのままstd::ofstreamへ渡すと、Windowsでは実行時のANSIコードページ
    // (日本語環境では通常CP932)で解釈され、非ASCII文字を含むパスで書き込みに失敗する。
    // toStdWString()でstd::filesystem::pathを作る(既存のProjectIO呼び出しと同じ方式)ことで回避する
    std::ofstream out(std::filesystem::path(path.toStdWString()), std::ios::binary);
    char header[80] = {};
    std::memcpy(header, "perapera debug cube", 20);
    out.write(header, sizeof(header));
    writeLeU32To(out, 12);  // 三角形数(立方体=6面×2三角形)

    const float s = 10.0f;  // CAD由来を模した一辺10の立方体(0..10)
    const float v[8][3] = {{0, 0, 0}, {s, 0, 0}, {s, s, 0}, {0, s, 0},
                           {0, 0, s}, {s, 0, s}, {s, s, s}, {0, s, s}};
    // 各面2三角形(頂点インデックス)+外向き法線
    const int faces[6][6] = {
        {0, 1, 2, 0, 2, 3},  // -Z
        {4, 6, 5, 4, 7, 6},  // +Z
        {0, 4, 5, 0, 5, 1},  // -Y
        {3, 2, 6, 3, 6, 7},  // +Y
        {0, 3, 7, 0, 7, 4},  // -X
        {1, 5, 6, 1, 6, 2},  // +X
    };
    const float normals[6][3] = {{0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}};

    for (int f = 0; f < 6; ++f) {
        for (int t = 0; t < 2; ++t) {
            writeLeFloatTo(out, normals[f][0]);
            writeLeFloatTo(out, normals[f][1]);
            writeLeFloatTo(out, normals[f][2]);
            for (int c = 0; c < 3; ++c) {
                const int idx = faces[f][t * 3 + c];
                writeLeFloatTo(out, v[idx][0]);
                writeLeFloatTo(out, v[idx][1]);
                writeLeFloatTo(out, v[idx][2]);
            }
            writeLeU16To(out, 0);
        }
    }
    out.close();
    return path;
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_UseStyleSheetPropagationInWidgetStyles, true);
    QApplication app(argc, argv);
    const QStringList args = app.arguments();

    perapera::ui::RetroThemeVariant retroVariant = perapera::ui::RetroThemeVariant::Windows95;
    bool retroUiEnabled = true;
    for (const QString& arg : args) {
        const QString lower = arg.toLower();
        if (lower == QStringLiteral("--normal-ui")) retroUiEnabled = false;
        if (lower == QStringLiteral("--retro-ui") || lower == QStringLiteral("--retro-ui=95") ||
            lower == QStringLiteral("--retro-ui=win95") || lower == QStringLiteral("--windows95-ui")) {
            retroUiEnabled = true;
            retroVariant = perapera::ui::RetroThemeVariant::Windows95;
        }
        if (lower == QStringLiteral("--retro-ui=xp") || lower == QStringLiteral("--windowsxp-ui")) {
            retroUiEnabled = true;
            retroVariant = perapera::ui::RetroThemeVariant::WindowsXp;
        }
    }
    perapera::ui::setRetroThemeAvailable(app, true);
    if (retroUiEnabled) perapera::ui::applyRetroTheme(app, retroVariant);
    if (args.contains(QStringLiteral("--retro-ui-smoke"))) return 0;

    // 動作確認用: --multiplane-test <出力PNG> でマルチプレーン撮影台のデモシーン
    // (奥=背景/中=セルA/手前=セルB、セルAにピント)をレイトレースし、
    // ウィンドウを開かずにPNGへ保存して終了する
    {
        const int multiplaneIndex = args.indexOf("--multiplane-test");
        if (multiplaneIndex >= 0 && multiplaneIndex + 1 < args.size()) {
            const QString outputPath = args.at(multiplaneIndex + 1);

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
    perapera::ui::keepWindowOnScreen(&window);
    QTimer::singleShot(0, &window, [&window] { perapera::ui::keepWindowOnScreen(&window); });

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

    // 動作確認用: --io-test <.ppprojフォルダパス> <出力PNG> でストローク描画→保存→新規→
    // 読み込み→画面保存を行い、UI経由の保存/読み込みを一括検証する
    const int ioIndex = args.indexOf("--io-test");
    if (ioIndex >= 0 && ioIndex + 2 < args.size()) {
        const QString projectPath = args.at(ioIndex + 1);
        const QString outputPath = args.at(ioIndex + 2);
        QTimer::singleShot(500, &window, [&window, projectPath, outputPath] {
            window.canvas()->debugSimulateStroke();
            const bool saved = window.debugSaveTo(projectPath);
            window.debugNewDocument();  // 一度白紙に戻す
            const bool loaded = window.debugLoadFrom(projectPath);
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
            const QString autosavedPath = window.debugTriggerAutosave();  // .ppprojフォルダのパス
            window.debugNewDocument();  // 一度白紙に戻す
            const bool loaded = !autosavedPath.isEmpty() && window.debugLoadFrom(autosavedPath);
            QTimer::singleShot(200, &window, [&window, outputPath, autosavedPath, loaded] {
                window.canvas()->grabFramebuffer().save(outputPath);
                // テストで作った自動保存フォルダを残すと次回通常起動時に偽のリカバリ提案が出るため削除する
                if (!autosavedPath.isEmpty()) QDir(autosavedPath).removeRecursively();
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

    // 動作確認用: --palette-test <.ppprojフォルダパス> でパレット追加→保存→新規→読込の往復を検証し、
    // 結果に応じた終了コードで終了する(0=成功, 1=不一致)
    const int paletteIndex = args.indexOf("--palette-test");
    if (paletteIndex >= 0 && paletteIndex + 1 < args.size()) {
        const QString projectPath = args.at(paletteIndex + 1);
        QTimer::singleShot(500, &window, [&window, projectPath] {
            const int result = window.debugPaletteRoundTrip(projectPath);
            QApplication::exit(result);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
        });
    }

    // 動作確認用: --role-test <.ppprojフォルダパス> でレイヤー種別(色トレス線/作監修正)の設定→保存→
    // 新規→読込の往復を検証し、結果に応じた終了コードで終了する(0=成功, 1=不一致)
    const int roleIndex = args.indexOf("--role-test");
    if (roleIndex >= 0 && roleIndex + 1 < args.size()) {
        const QString projectPath = args.at(roleIndex + 1);
        QTimer::singleShot(500, &window, [&window, projectPath] {
            const int result = window.debugRoleRoundTrip(projectPath);
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
    // 全コマを連番PNGで書き出す。debugExportSequence()の戻り値(0=成功)でそのままexitする。
    // 続けてキャンバスサイズを4:3(1440x1080)に変更した状態でも書き出しが通ることを確認する
    // (<出力フォルダ>_4x3へ書き出す。こちらも失敗すれば非0で終了する)
    const int exportIndex = args.indexOf("--export-test");
    if (exportIndex >= 0 && exportIndex + 1 < args.size()) {
        const QString outputDir = args.at(exportIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputDir] {
            int result = window.debugExportSequence(outputDir);
            if (result == 0) {
                window.debugSetCanvasSize(1440, 1080);  // 4:3 SD
                result = window.debugExportSequence(outputDir + QStringLiteral("_4x3"));
            }
            QApplication::exit(result);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
        });
    }

    // 動作確認用: --canvassize-test <出力PNG> でプロジェクトのキャンバスサイズをシネスコ(2048x858、
    // 2.39:1の横長)に変更し、ストロークを1本描いてから「プロジェクト設定」ダイアログを非モーダルで
    // 開いた状態を撮る。ダイアログは別ウィンドウ(top-level)なので、メインウィンドウの
    // grab()にダイアログのgrab()をその画面上の相対位置で重ね描きし、1枚の画像として保存する
    const int canvasSizeIndex = args.indexOf("--canvassize-test");
    if (canvasSizeIndex >= 0 && canvasSizeIndex + 1 < args.size()) {
        const QString outputPath = args.at(canvasSizeIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetCanvasSize(2048, 858);  // シネスコ(2.39:1)
            window.canvas()->debugSimulateStroke();
            window.canvas()->grabFramebuffer();  // paintGLを強制実行させ、キャンバスサイズ変更を反映させる
            QTimer::singleShot(200, &window, [&window, outputPath] {
                QDialog* dialog = window.debugOpenCanvasSizeDialog();
                QTimer::singleShot(300, &window, [&window, outputPath, dialog] {
                    QPixmap composed = window.grab();
                    QPainter painter(&composed);
                    const QPoint offset = dialog->frameGeometry().topLeft() - window.frameGeometry().topLeft();
                    painter.drawPixmap(offset, dialog->grab());
                    painter.end();
                    composed.save(outputPath);
                    QApplication::exit(0);
                });
            });
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

    // 距離ブラシ確認: --distbrush-test <PNG>。縦バー背景に横グラデの距離マップを付け、強いDoFで
    // レンダリングする。距離がfocus(=700)に近い所はシャープ、離れる所ほどボケるはず
    const int distIdx = args.indexOf("--distbrush-test");
    if (distIdx >= 0 && distIdx + 1 < args.size()) {
        const QString out = args.at(distIdx + 1);
        const int AW = 900, AH = 506;
        core::Bitmap art(AW, AH);
        art.fill({0, 0, 0, 0});
        for (int x = 0; x < AW; ++x)
            if ((x / 40) % 2 == 0)
                for (int y = 0; y < AH; ++y) art.setPixel(x, y, {20, 20, 20, 255});  // 縦バー(黒・不透明)
        core::Bitmap dm(AW, AH);
        for (int y = 0; y < AH; ++y)
            for (int x = 0; x < AW; ++x) {
                const auto g = static_cast<uint8_t>(255.0 * x / (AW - 1));  // 左=near(0)→右=far(255)
                dm.setPixel(x, y, {g, g, g, 255});
            }
        core::MultiplanePlane pl;
        pl.artwork = &art;
        pl.distanceMm = 700;
        pl.widthMm = 400;
        pl.distanceMap = &dm;
        pl.distanceNearMm = 250;
        pl.distanceFarMm = 1500;
        core::MultiplaneCamera cam;
        cam.focalLengthMm = 50;
        cam.sensorWidthMm = 36;
        cam.apertureFStop = 1.4;
        cam.focusDistanceMm = 700;
        const core::Bitmap res = core::renderMultiplane({pl}, cam, 960, 540, 64, 1, nullptr);
        const QImage img(res.data(), res.width(), res.height(), QImage::Format_RGBA8888);
        return img.save(out) ? 0 : 1;
    }

    // ロボット作品生成: --build-robot <.ppprojフォルダ> <確認用PNGフォルダ>
    const int buildRobotIdx = args.indexOf("--build-robot");
    if (buildRobotIdx >= 0 && buildRobotIdx + 2 < args.size()) {
        const QString folder = args.at(buildRobotIdx + 1);
        const QString previewDir = args.at(buildRobotIdx + 2);
        QTimer::singleShot(400, &window, [&window, folder, previewDir] {
            QApplication::exit(window.debugBuildAndSaveRobot(folder, previewDir) ? 0 : 1);
        });
    }

    // メカ見た目確認: --mech-test <PNG>
    const int mechTestIdx = args.indexOf("--mech-test");
    if (mechTestIdx >= 0 && mechTestIdx + 1 < args.size()) {
        const QString out = args.at(mechTestIdx + 1);
        QTimer::singleShot(300, &window, [&window, out] { QApplication::exit(window.debugRenderMechTest(out) ? 0 : 1); });
    }

    // サンプル作品生成: --build-work <.ppprojフォルダ> <確認用PNGフォルダ> で10秒の作品「流星の夜」を
    // 構築し、確認用PNGとプロジェクトを保存する
    const int buildWorkIdx = args.indexOf("--build-work");
    if (buildWorkIdx >= 0 && buildWorkIdx + 2 < args.size()) {
        const QString folder = args.at(buildWorkIdx + 1);
        const QString previewDir = args.at(buildWorkIdx + 2);
        QTimer::singleShot(400, &window, [&window, folder, previewDir] {
            const bool ok = window.debugBuildAndSaveWork(folder, previewDir);
            QApplication::exit(ok ? 0 : 1);
        });
    }

    // 動作確認用: --anaflare-test <出力PNG> で暗背景に輝点を置きアナモルフィックフレアを適用保存する
    const int anaIdx = args.indexOf("--anaflare-test");
    if (anaIdx >= 0 && anaIdx + 1 < args.size()) {
        const QString out = args.at(anaIdx + 1);
        core::Bitmap bmp(960, 540);
        bmp.fill({18, 18, 24, 255});
        const auto dot = [&](int cx, int cy, int r) {
            for (int y = -r; y <= r; ++y)
                for (int x = -r; x <= r; ++x)
                    if (x * x + y * y <= r * r) {
                        const int px = cx + x, py = cy + y;
                        if (px >= 0 && py >= 0 && px < 960 && py < 540) bmp.setPixel(px, py, {255, 255, 255, 255});
                    }
        };
        dot(300, 200, 10);
        dot(660, 340, 6);
        core::Effect e;
        e.type = core::EffectType::AnaFlare;
        e.enabled = true;
        e.params = core::effectDefaultParams(core::EffectType::AnaFlare);
        core::applyEffect(bmp, e, 0, 1.0);
        const QImage img(bmp.data(), bmp.width(), bmp.height(), QImage::Format_RGBA8888);
        return img.save(out) ? 0 : 1;
    }

    // 動作確認用: --previz-distort-test <歪みなしPNG> <魚眼PNG> でプリビズのカメラ絵を歪曲0/0.9で保存する
    const int previzDistortIdx = args.indexOf("--previz-distort-test");
    if (previzDistortIdx >= 0 && previzDistortIdx + 2 < args.size()) {
        const QString out0 = args.at(previzDistortIdx + 1);
        const QString out1 = args.at(previzDistortIdx + 2);
        QTimer::singleShot(500, &window, [&window, out0, out1] {
            window.debugOpenPreviz();
            QTimer::singleShot(300, &window, [&window, out0, out1] {
                window.previzWindow()->debugSetLensDistortion(0.0);
                window.previzWindow()->viewport()->renderCameraViewImage(16.0f / 9.0f).save(out0);
                window.previzWindow()->debugSetLensDistortion(0.9);
                window.previzWindow()->viewport()->renderCameraViewImage(16.0f / 9.0f).save(out1);
                QApplication::exit(0);
            });
        });
    }

    // 動作確認用: --previz-prim-test <出力PNG> でプリビズウィンドウを開き、円柱・球の
    // プリミティブを追加する(既定の箱・円柱・球が重ならないようX方向に離して配置)。
    // 球は選択したまま非一様スケール(X=2倍)を掛けて楕円体に変形し、
    // 箱・円柱・球(うち1つ変形)が並んだ3Dビューポートを保存して終了する
    const int previzPrimIndex = args.indexOf("--previz-prim-test");
    if (previzPrimIndex >= 0 && previzPrimIndex + 1 < args.size()) {
        const QString outputPath = args.at(previzPrimIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugOpenPreviz();
            QTimer::singleShot(300, &window, [&window, outputPath] {
                // 空シーンには既定で箱が1つ置かれる(setScene)。それはX=-1.5へ寄せておく
                window.previzWindow()->debugSetSelectedPosition(-1.5, 0.0, 0.0);
                window.previzWindow()->debugAddPrimitive(":cylinder");
                window.previzWindow()->debugSetSelectedPosition(0.0, 0.0, 0.0);
                window.previzWindow()->debugAddPrimitive(":sphere");  // 追加後、球が選択状態になる
                window.previzWindow()->debugSetSelectedPosition(1.5, 0.0, 0.0);
                window.previzWindow()->debugSetSelectedScale(2.0, 1.0, 1.0);  // 球→楕円体に変形
                QTimer::singleShot(300, &window, [&window, outputPath] {
                    window.previzWindow()->viewport()->grabFramebuffer().save(outputPath);
                    QApplication::exit(0);
                });
            });
        });
    }

    // 動作確認用: --previz-stl-test <出力PNG> でプリビズウィンドウを開き、
    // コード内で生成したバイナリSTL立方体(12三角形、CAD由来の一辺10)を一時ファイルへ書き出して
    // debugAddModelFile()で読み込み(QFileDialogは使わない)、3Dビューポートを保存して終了する。
    // 「STLを読み込んでも表示されない」報告の再現・検証用。
    // 空シーンには既定で組み込みの箱(":box")が原点に自動配置される(見た目・色ともSTL読込直後の
    // 自動フィット結果と酷似する)ため、判定を曖昧にしないよう既定の箱は先に原点から遠ざけておく
    const int previzWireIndex = args.indexOf("--previz-wire-test");
    if (previzWireIndex >= 0 && previzWireIndex + 1 < args.size()) {
        const QString outputPath = args.at(previzWireIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugOpenPreviz();
            QTimer::singleShot(300, &window, [&window, outputPath] {
                window.previzWindow()->debugSetSelectedPosition(-1.5, 0.0, 0.0);
                window.previzWindow()->debugAddPrimitive(":cylinder");
                window.previzWindow()->debugSetSelectedPosition(0.0, 0.0, 0.0);
                window.previzWindow()->debugAddPrimitive(":sphere");
                window.previzWindow()->debugSetSelectedPosition(1.5, 0.0, 0.0);
                window.previzWindow()->debugSetSelectedScale(2.0, 1.0, 1.0);
                window.previzWindow()->viewport()->setWireframeEnabled(true);
                QTimer::singleShot(300, &window, [&window, outputPath] {
                    window.previzWindow()->viewport()->grabFramebuffer().save(outputPath);
                    QApplication::exit(0);
                });
            });
        });
    }

    // 動作確認用: --shortcut-settings-test <PNG> でウインドウ別ショートカット設定を保存する
    const int shortcutSettingsIndex = args.indexOf("--shortcut-settings-test");
    if (shortcutSettingsIndex >= 0 && shortcutSettingsIndex + 1 < args.size()) {
        const QString outputPath = args.at(shortcutSettingsIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            QDialog* dialog = window.debugOpenCanvasSizeDialog();
            if (auto* tabs = dialog->findChild<QTabWidget*>()) tabs->setCurrentIndex(1);
            QTimer::singleShot(300, &window, [dialog, outputPath] {
                dialog->grab().save(outputPath);
                QApplication::exit(0);
            });
        });
    }

    // 動作確認用: --lasso-test <PNG1> <PNG2> <PNG3> で投げ縄塗り→Undo→Redoを保存する
    const int lassoIndex = args.indexOf("--lasso-test");
    if (lassoIndex >= 0 && lassoIndex + 3 < args.size()) {
        const QString out1 = args.at(lassoIndex + 1);
        const QString out2 = args.at(lassoIndex + 2);
        const QString out3 = args.at(lassoIndex + 3);
        QTimer::singleShot(500, &window, [&window, out1, out2, out3] {
            window.canvas()->setPenColor(QColor(30, 150, 230));
            window.canvas()->debugLassoFill(
                {{420, 220}, {1500, 180}, {1720, 540}, {1320, 900}, {520, 820}, {260, 500}});
            QTimer::singleShot(100, &window, [&window, out1, out2, out3] {
                window.canvas()->grabFramebuffer().save(out1);
                window.debugUndo();
                QTimer::singleShot(100, &window, [&window, out2, out3] {
                    window.canvas()->grabFramebuffer().save(out2);
                    window.debugRedo();
                    QTimer::singleShot(100, &window, [&window, out3] {
                        window.canvas()->grabFramebuffer().save(out3);
                        QApplication::exit(0);
                    });
                });
            });
        });
    }

    const int previzHumanoidIndex = args.indexOf("--previz-humanoid-test");
    if (previzHumanoidIndex >= 0 && previzHumanoidIndex + 1 < args.size()) {
        const QString outputPath = args.at(previzHumanoidIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugOpenPreviz();
            QTimer::singleShot(300, &window, [&window, outputPath] {
                window.previzWindow()->debugSetSelectedPosition(50.0, 0.0, 50.0);
                window.previzWindow()->debugAddPrimitive(":humanoid");
                window.previzWindow()->debugSetHumanoidBodyPreset(4);
                window.previzWindow()->debugAddHumanoidWalkCycleKeys();
                window.previzWindow()->setTimeline(12, 24);
                QTimer::singleShot(400, &window, [&window, outputPath] {
                    window.previzWindow()->viewport()->grabFramebuffer().save(outputPath);
                    QApplication::exit(0);
                });
            });
        });
    }

    const int previzStlIndex = args.indexOf("--previz-stl-test");
    if (previzStlIndex >= 0 && previzStlIndex + 1 < args.size()) {
        const QString outputPath = args.at(previzStlIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugOpenPreviz();
            QTimer::singleShot(300, &window, [&window, outputPath] {
                // 既定の箱(行0、setScene直後にcurrentRow=0で選択済み)を原点から遠ざけ、
                // STLモデルだけが原点付近に見える状態にする
                window.previzWindow()->debugSetSelectedPosition(50.0, 0.0, 50.0);
                const QString stlPath = writeDebugCubeStl(QDir::tempPath() + "/previz_stl_test_cube.stl");
                window.previzWindow()->debugAddModelFile(stlPath);
                QTimer::singleShot(400, &window, [&window, outputPath, stlPath] {
                    window.previzWindow()->viewport()->grabFramebuffer().save(outputPath);
                    QFile::remove(stlPath);
                    QApplication::exit(0);
                });
            });
        });
    }

    // 調査用(一時): --previz-stl-jp-test <出力PNG> で日本語を含むパス(一時フォルダ配下)に
    // STLを書き出し、debugAddModelFileで読み込んだ結果を保存する。日本語Windows(CP932)環境で
    // パスのエンコーディング不整合が読込失敗の原因になっていないかを検証する
    const int previzStlJpIndex = args.indexOf("--previz-stl-jp-test");
    if (previzStlJpIndex >= 0 && previzStlJpIndex + 1 < args.size()) {
        const QString outputPath = args.at(previzStlJpIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugOpenPreviz();
            QTimer::singleShot(300, &window, [&window, outputPath] {
                window.previzWindow()->debugSetSelectedPosition(50.0, 0.0, 50.0);
                const QString jpDir = QDir::tempPath() + QStringLiteral("/プレビズ検証フォルダ");
                QDir().mkpath(jpDir);
                const QString stlPath = writeDebugCubeStl(jpDir + QStringLiteral("/立方体モデル.stl"));
                window.previzWindow()->debugAddModelFile(stlPath);
                QTimer::singleShot(400, &window, [&window, outputPath, jpDir] {
                    window.previzWindow()->viewport()->grabFramebuffer().save(outputPath);
                    QDir(jpDir).removeRecursively();
                    QApplication::exit(0);
                });
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

    const int storyboardPdfIndex = args.indexOf("--storyboard-pdf-test");
    if (storyboardPdfIndex >= 0 && storyboardPdfIndex + 1 < args.size()) {
        const QString outputPath = args.at(storyboardPdfIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupStoryboardDemo();
            window.debugOpenStoryboard();
            QTimer::singleShot(300, &window, [&window, outputPath] {
                const bool ok = window.storyboardWindow() &&
                                window.storyboardWindow()->debugExportStoryboardPdf(outputPath);
                QApplication::exit(ok ? 0 : 1);
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
    const int storyboardDetachIndex = args.indexOf("--storyboard-detach-test");
    if (storyboardDetachIndex >= 0 && storyboardDetachIndex + 1 < args.size()) {
        const QString outputPath = args.at(storyboardDetachIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupStoryboardDemo();
            window.debugOpenStoryboard();
            QTimer::singleShot(300, &window, [&window, outputPath] {
                window.storyboardWindow()->debugDetachCanvas();
                QTimer::singleShot(300, &window, [&window, outputPath] {
                    auto* floating = window.storyboardWindow()->debugFloatingCanvasWindow();
                    if (!floating) {
                        QApplication::exit(1);
                        return;
                    }
                    auto* canvas = floating->findChild<GLCanvas*>();
                    if (!canvas) {
                        QApplication::exit(1);
                        return;
                    }
                    canvas->debugSimulateStroke();
                    floating->grab().save(outputPath);
                    floating->close();
                    QTimer::singleShot(100, &window, [] { QApplication::exit(0); });
                });
            });
        });
    }

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

    const int settingBoardExportIndex = args.indexOf("--settingboard-export-test");
    if (settingBoardExportIndex >= 0 && settingBoardExportIndex + 1 < args.size()) {
        const QString outputPath = args.at(settingBoardExportIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupSettingBoardDemo();
            window.debugOpenSettingBoard();
            QTimer::singleShot(300, &window, [&window, outputPath] {
                const bool ok = window.settingBoardWindow() &&
                                window.settingBoardWindow()->debugExportSelectedBoardImage(outputPath);
                QApplication::exit(ok ? 0 : 1);
            });
        });
    }

    // 動作確認用: --edit-test <出力PNG> で編集(カッティング)デモ(カット3つ、尺12/24/12、
    // 進捗: 原画/レイアウト/未着手、カット1に赤ストローク・カット2に青ストローク)を組み、
    // 編集ウィンドウを開いてグローバルコマ18(カット2内)へシークした状態を保存して終了する
    const int settingBoardDetachIndex = args.indexOf("--settingboard-detach-test");
    if (settingBoardDetachIndex >= 0 && settingBoardDetachIndex + 1 < args.size()) {
        const QString outputPath = args.at(settingBoardDetachIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupSettingBoardDemo();
            window.debugOpenSettingBoard();
            QTimer::singleShot(300, &window, [&window, outputPath] {
                window.settingBoardWindow()->debugDetachCanvas();
                QTimer::singleShot(300, &window, [&window, outputPath] {
                    auto* floating = window.settingBoardWindow()->debugFloatingCanvasWindow();
                    if (!floating) {
                        QApplication::exit(1);
                        return;
                    }
                    auto* canvas = floating->findChild<GLCanvas*>();
                    if (!canvas) {
                        QApplication::exit(1);
                        return;
                    }
                    canvas->debugSimulateStroke();
                    floating->grab().save(outputPath);
                    floating->close();
                    QTimer::singleShot(100, &window, [] { QApplication::exit(0); });
                });
            });
        });
    }

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

    // 動作確認用: --projectmgr-test <出力PNG> でプロジェクト管理ウィンドウ(進行管理表+集計)を
    // 編集デモ(カット3つ、尺12/24/12、進捗: 原画/レイアウト/未着手)で開いた状態を保存して終了する
    const int projectMgrIndex = args.indexOf("--projectmgr-test");
    if (projectMgrIndex >= 0 && projectMgrIndex + 1 < args.size()) {
        const QString outputPath = args.at(projectMgrIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupEditDemo();
            window.debugOpenProjectManagerWindow();
            QTimer::singleShot(400, &window, [&window, outputPath] {
                window.projectManagerWindow()->grab().save(outputPath);
                QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
            });
        });
    }

    // 動作確認用: --exportdialog-test <PNG> で書き出しダイアログ(内容選択・詳細設定)を表示保存する
    const int exportDlgIdx = args.indexOf("--exportdialog-test");
    if (exportDlgIdx >= 0 && exportDlgIdx + 1 < args.size()) {
        const QString outputPath = args.at(exportDlgIdx + 1);
        QTimer::singleShot(300, &window, [outputPath] {
            auto* d = new ExportDialog(QStringList{QStringLiteral("A"), QStringLiteral("B")}, 24);
            d->show();
            QTimer::singleShot(200, d, [d, outputPath] {
                d->grab().save(outputPath);
                QApplication::exit(0);
            });
        });
    }
    // 動作確認用: --exportmode-test <出力フォルダ> で作画/プリビズ/両方/透明の各モードを1枚ずつ書き出す
    const int exportModeIdx = args.indexOf("--exportmode-test");
    if (exportModeIdx >= 0 && exportModeIdx + 1 < args.size()) {
        const QString dir = args.at(exportModeIdx + 1);
        QTimer::singleShot(500, &window, [&window, dir] {
            window.debugSetupEditDemo();
            QDir().mkpath(dir);
            const bool a = window.debugExportFrame(dir + "/drawing.png", 0, false);
            const bool b = window.debugExportFrame(dir + "/drawing_alpha.png", 0, true);
            const bool c = window.debugExportFrame(dir + "/previz.png", 1, false);
            const bool d = window.debugExportFrame(dir + "/both.png", 2, false);
            QApplication::exit((a && b && c && d) ? 0 : 1);
        });
    }

    // 動作確認用: --exportall-test <出力フォルダ> で全カット通し(編集デモ=尺12+24+12=48コマ)を書き出す
    const int exportAllIdx = args.indexOf("--exportall-test");
    if (exportAllIdx >= 0 && exportAllIdx + 1 < args.size()) {
        const QString dir = args.at(exportAllIdx + 1);
        QTimer::singleShot(500, &window, [&window, dir] {
            window.debugSetupEditDemo();
            const int n = window.debugExportAllCuts(dir);
            QApplication::exit(n == 48 ? 0 : 1);  // 3カット合計48コマになるはず
        });
    }

    // 動作確認用: --newproject-dialog-test <PNG> / --newcut-dialog-test <PNG> で作成ダイアログを表示保存する
    const int newProjIdx = args.indexOf("--newproject-dialog-test");
    if (newProjIdx >= 0 && newProjIdx + 1 < args.size()) {
        const QString outputPath = args.at(newProjIdx + 1);
        QTimer::singleShot(300, &window, [outputPath] {
            auto* d = new NewProjectDialog(24);
            d->show();
            QTimer::singleShot(200, d, [d, outputPath] {
                d->grab().save(outputPath);
                QApplication::exit(0);
            });
        });
    }
    const int newCutIdx = args.indexOf("--newcut-dialog-test");
    if (newCutIdx >= 0 && newCutIdx + 1 < args.size()) {
        const QString outputPath = args.at(newCutIdx + 1);
        QTimer::singleShot(300, &window, [outputPath] {
            auto* d = new NewCutDialog(QStringLiteral("カット 4"), 24);
            d->show();
            QTimer::singleShot(200, d, [d, outputPath] {
                d->grab().save(outputPath);
                QApplication::exit(0);
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

    // 動作確認用: --shooting-playback-test <出力PNG1> <出力PNG2> で撮影デモを組み、実際に再生を
    // 開始してから数秒間(実時間)そのまま実行し続け(=QTimer駆動のonPlaybackTickが実際に何度も
    // 走る)、再生停止直後のウィンドウ(出力PNG1、コマ番号ラベルの描画時間/キャッシュ表示込み)を
    // 保存する。続けてコマ12(setup直後に一度プレビュー済み=キャッシュ済みのはずのコマ)へ
    // スクラブし直し、その結果(出力PNG2、「キャッシュ」表示になるはず)も保存して終了する。
    // プレビュー高速化(コマ指紋キャッシュ・実時間フレームスキップ)の効果を確認するためのもの
    const int shootingPlaybackIndex = args.indexOf("--shooting-playback-test");
    if (shootingPlaybackIndex >= 0 && shootingPlaybackIndex + 2 < args.size()) {
        const QString outputPath1 = args.at(shootingPlaybackIndex + 1);
        const QString outputPath2 = args.at(shootingPlaybackIndex + 2);
        QTimer::singleShot(500, &window, [&window, outputPath1, outputPath2] {
            window.debugSetupShootingDemo();  // 内部でdebugSelectKoma(12)まで行う(=コマ12を一度プレビュー)
            QTimer::singleShot(400, &window, [&window, outputPath1, outputPath2] {
                window.shootingWindow()->debugTogglePlayback();  // 再生開始
                QTimer::singleShot(3000, &window, [&window, outputPath1, outputPath2] {
                    window.shootingWindow()->debugTogglePlayback();  // 再生停止(状態を固定してから撮る)
                    QTimer::singleShot(200, &window, [&window, outputPath1, outputPath2] {
                        window.shootingWindow()->grab().save(outputPath1);
                        window.shootingWindow()->debugSelectKoma(12);  // 既にキャッシュ済みのはずのコマへスクラブ
                        QTimer::singleShot(300, &window, [&window, outputPath2] {
                            window.shootingWindow()->grab().save(outputPath2);
                            QApplication::exit(
                                0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
                        });
                    });
                });
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

    // 動作確認用: --backlight-test <出力PNG> で透過光(T光)デモ(黒塗りセルに丸い穴を数個開け、
    // クラシック撮影を距離500mm・f/2.0・フォーカス250mm[穴の平面はピント外れ=玉ボケ]で有効化し、
    // 透過光ON[強度4・にじみ半径24/強さ0.8]にする)を組み、撮影ウィンドウ(クラシック撮影+
    // 透過光UI込み)を保存して終了する
    const int backlightIndex = args.indexOf("--backlight-test");
    if (backlightIndex >= 0 && backlightIndex + 1 < args.size()) {
        const QString outputPath = args.at(backlightIndex + 1);
        QTimer::singleShot(500, &window, [&window, outputPath] {
            window.debugSetupBacklightDemo();
            // マルチプレーンのレイトレース(samples=16)は時間がかかりうるため待ちを長めに取る
            QTimer::singleShot(900, &window, [&window, outputPath] {
                window.shootingWindow()->grab().save(outputPath);
                QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
            });
        });
    }

    // 動作確認用: --backlight-blink-test <消灯PNG> <点灯PNG> で透過光の点滅キー
    // (強度キー: 偶数コマ=0/奇数コマ=4)がプレビュー(指紋キャッシュ込み)で正しく変わることを確認する。
    // コマ0(消灯)とコマ1(点灯)をそれぞれ保存して終了する
    const int blinkIndex = args.indexOf("--backlight-blink-test");
    if (blinkIndex >= 0 && blinkIndex + 2 < args.size()) {
        const QString offPath = args.at(blinkIndex + 1);
        const QString onPath = args.at(blinkIndex + 2);
        QTimer::singleShot(500, &window, [&window, offPath, onPath] {
            window.debugSetupBacklightDemo();
            QTimer::singleShot(900, &window, [&window, offPath, onPath] {
                window.shootingWindow()->debugSelectKoma(0);  // 消灯コマ
                QTimer::singleShot(900, &window, [&window, offPath, onPath] {
                    window.shootingWindow()->grab().save(offPath);
                    window.shootingWindow()->debugSelectKoma(1);  // 点灯コマ
                    QTimer::singleShot(900, &window, [&window, onPath] {
                        window.shootingWindow()->grab().save(onPath);
                        QApplication::exit(0);  // quit()はcloseEventを経由するためexit()で直接終了する
                    });
                });
            });
        });
    }

    // 動作確認用: --film-test <出力PNG0> <出力PNG1> でフィルムエフェクトデモ(グレー地+カラーバー風の
    // 縦帯数本を描いた止めセル、尺4、フィルムエフェクト[既定値、grain=0.5]を全体に適用)を組み、
    // コマ1・コマ2をdebugSelectKomaで切り替えてそれぞれ撮影ウィンドウを保存する。粒状ノイズが
    // フレームごとに変わっている(無相関な擬似乱数)ことを2枚のPNGを見比べて確認するためのもの
    const int filmIndex = args.indexOf("--film-test");
    if (filmIndex >= 0 && filmIndex + 2 < args.size()) {
        const QString out0 = args.at(filmIndex + 1);
        const QString out1 = args.at(filmIndex + 2);
        QTimer::singleShot(500, &window, [&window, out0, out1] {
            window.debugSetupFilmDemo();
            QTimer::singleShot(400, &window, [&window, out0, out1] {
                window.shootingWindow()->debugSelectKoma(1);
                QTimer::singleShot(300, &window, [&window, out0, out1] {
                    window.shootingWindow()->grab().save(out0);
                    window.shootingWindow()->debugSelectKoma(2);
                    QTimer::singleShot(300, &window, [&window, out1] {
                        window.shootingWindow()->grab().save(out1);
                        QApplication::exit(0);  // quit()はcloseEvent(未保存確認ダイアログ)を経由するためexit()で直接終了する
                    });
                });
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
