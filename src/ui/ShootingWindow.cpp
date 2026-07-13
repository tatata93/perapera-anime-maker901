#include "ShootingWindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <utility>

#include "core/Compositor.h"
#include "core/Effect.h"
#include "core/Project.h"
#include "render/GLCanvas.h"
#include "FilmCurveWidget.h"

namespace {

// パラメータ名(core::Effect::params のキー)の日本語表示名
QString paramLabel(const std::string& key) {
    static const std::map<std::string, QString> kLabels = {
        {"radius", QObject::tr("半径")},         {"threshold", QObject::tr("しきい値")},
        {"strength", QObject::tr("強さ")},       {"top", QObject::tr("上濃度")},
        {"bottom", QObject::tr("下濃度")},       {"r", QObject::tr("R")},
        {"g", QObject::tr("G")},                 {"b", QObject::tr("B")},
        {"amplitudeX", QObject::tr("振幅X")},     {"amplitudeY", QObject::tr("振幅Y")},
        {"seed", QObject::tr("シード")},          {"brightness", QObject::tr("明るさ")},
        {"contrast", QObject::tr("コントラスト")}, {"saturation", QObject::tr("彩度")},
        {"hue", QObject::tr("色相")},             {"centerX", QObject::tr("中心X")},
        {"centerY", QObject::tr("中心Y")},        {"amount", QObject::tr("量")},
        {"taps", QObject::tr("タップ数")},        {"size", QObject::tr("粒サイズ")},
        {"softness", QObject::tr("柔らかさ")},    {"exposure", QObject::tr("露出(EV)")},
        {"fade", QObject::tr("黒浮き")},          {"warmth", QObject::tr("色温度")},
        {"crosstalk", QObject::tr("分光クロストーク")}, {"grain", QObject::tr("粒状")},
        {"grainSize", QObject::tr("粒サイズ")},
    };
    const auto it = kLabels.find(key);
    if (it != kLabels.end()) return it->second;

    // resp{R|G|B}{0..4}: 層別応答カーブの制御点。スピン行としては出さずFilmCurveWidgetの
    // グラフで編集するため通常はここに来ないが、念のためのフォールバック表示("R応答0"等)
    if (key.rfind("resp", 0) == 0 && key.size() == 6) {
        return QStringLiteral("%1応答%2").arg(QChar::fromLatin1(key[4])).arg(QChar::fromLatin1(key[5]));
    }

    return QString::fromStdString(key);  // 未知のキーはそのまま表示(将来のパラメータ追加に備えた保険)
}

// 濃度系パラメータ(0〜1程度の細かい調整が必要)はステップ0.05、それ以外(半径・角度系)は1.0
bool isDensityParam(const std::string& key) {
    return key == "top" || key == "bottom" || key == "strength" || key == "amount" || key == "softness" ||
           key == "centerX" || key == "centerY" || key == "exposure" || key == "fade" || key == "warmth" ||
           key == "crosstalk" || key == "grain";
}

bool isRgbParam(const std::string& key) { return key == "r" || key == "g" || key == "b"; }

// パラメータの表示範囲(min, max)。"amount"のように同じキー名でもエフェクト種別によって
// 意味・スケールが異なるものがあるため、effect.typeも見て判定する
std::pair<double, double> paramRange(core::EffectType type, const std::string& key) {
    if (key == "brightness") return {-255.0, 255.0};
    if (key == "hue") return {-180.0, 180.0};
    if (key == "contrast") return type == core::EffectType::Film ? std::pair<double, double>{0.0, 1.0}
                                                                   : std::pair<double, double>{0.0, 3.0};
    if (key == "saturation") return {0.0, 3.0};
    if (key == "centerX" || key == "centerY") return {0.0, 1.0};
    if (key == "softness") return {0.05, 1.0};
    if (key == "taps") return {2.0, 32.0};
    if (key == "size") return {1.0, 4.0};
    if (key == "exposure") return {-2.0, 2.0};
    if (key == "fade") return {0.0, 0.3};
    if (key == "warmth") return {-1.0, 1.0};
    if (key == "crosstalk") return {0.0, 0.5};
    if (key == "grain") return {0.0, 1.0};
    if (key == "grainSize") return {1.0, 4.0};
    // resp{R|G|B}{0..4}: 層別応答カーブの制御点(0〜1)。グラフでしか編集しないが安全のため定義しておく
    if (key.rfind("resp", 0) == 0) return {0.0, 1.0};
    if (key == "amount") {
        switch (type) {
            case core::EffectType::RadialBlur:
                return {0.0, 0.2};
            case core::EffectType::Vignette:
            case core::EffectType::Grain:
                return {0.0, 1.0};
            case core::EffectType::ChromAb:
                return {0.0, 20.0};
            default:
                break;
        }
    }
    if (isRgbParam(key)) return {0.0, 255.0};
    return {0.0, 4096.0};
}

// エフェクトGroupBoxのタイトル(「ブラー (全体)」「グロー (A)」等)
QString effectRowLabel(const core::Effect& effect, const QStringList& celNames) {
    QString target = QObject::tr("全体");
    if (effect.targetCel >= 0 && effect.targetCel < celNames.size()) target = celNames.at(effect.targetCel);
    return QObject::tr("%1 (%2)").arg(QString::fromUtf8(core::effectTypeName(effect.type)), target);
}

// CTI(現在コマ)列のハイライト色
const QColor kCtiColumnColor(70, 110, 170);
const QColor kCtiHeaderColor(90, 140, 210);

// タイムラインのエフェクト見出し行: そのエフェクトが存在する範囲(常に全コマ)を示す薄い色帯
const QColor kEffectBandColor(60, 75, 95);
// タイムラインの「撮影台」見出し行の色帯(エフェクトの色帯とは別の色味で見分ける、常に全コマ)
const QColor kStandBandColor(90, 65, 110);

// マスク編集で使うペン色(赤=alphaがそのままエフェクト適用強度になるため塗った所が赤く見える)
const QColor kMaskPenColor(255, 0, 0, 255);

}  // namespace

ShootingWindow::ShootingWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("撮影 - perapera-anime-maker901"));
    resize(1400, 900);

    // ツールバー: カット選択+更新
    QToolBar* toolBar = addToolBar(tr("撮影"));
    toolBar->setMovable(false);
    toolBar->addWidget(new QLabel(tr(" カット: "), toolBar));
    m_cutCombo = new QComboBox(toolBar);
    m_cutCombo->setMinimumWidth(160);
    toolBar->addWidget(m_cutCombo);
    QAction* refreshAction = toolBar->addAction(tr("更新"));
    connect(refreshAction, &QAction::triggered, this, &ShootingWindow::refresh);
    connect(m_cutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_updating || index < 0) return;
        m_cutIndex = index;
        m_koma = 0;
        clearFrameCache();  // カット切替で見た目の元が変わるため、以前のカットのキャッシュは無効
        refresh();
    });

    // プレビュー画質: 撮影ウィンドウのプレビューは応答性優先(core::RenderOptions::proxyScale)。
    // 書き出しは常にフル品質のまま(renderPreviewNowだけがこの値を使う)。
    // itemDataは「キャンバスの長辺に対する比率」ではなく「プレビュー長辺の絶対px上限」にしてある
    // (computeProxyScale()参照)。比率のままだと4K等の大きいキャンバスではプレビューでも
    // 長辺1920px前後の縮小になり、依然として非常に重かった(ユーザー報告「開くだけで止まる」の
    // 主因)。絶対px上限にすることでキャンバスサイズに関わらずプレビューの重さがほぼ一定になる
    toolBar->addWidget(new QLabel(tr(" プレビュー画質: "), toolBar));
    m_previewQualityCombo = new QComboBox(toolBar);
    m_previewQualityCombo->addItem(tr("フル"), 900.0);
    m_previewQualityCombo->addItem(tr("1/2"), 600.0);
    m_previewQualityCombo->addItem(tr("1/4"), 360.0);
    m_previewQualityCombo->setCurrentIndex(1);  // 既定=1/2
    toolBar->addWidget(m_previewQualityCombo);
    connect(m_previewQualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_updating || index < 0) return;
        m_previewQuality = m_previewQualityCombo->itemData(index).toDouble();
        clearFrameCache();  // 画質が変わると過去のキャッシュ画像はサイズ/精細さが合わなくなる
        requestPreview();
    });

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(4, 4, 4, 4);

    auto* vSplit = new QSplitter(Qt::Vertical, central);

    // ==== 上段: 左「エフェクトコントロール」/ 右「プレビュー」 ====
    auto* topSplit = new QSplitter(Qt::Horizontal, vSplit);

    // --- 左: エフェクトコントロールパネル(スクロール) ---
    m_effectScroll = new QScrollArea(topSplit);
    m_effectScroll->setWidgetResizable(true);
    m_effectScroll->setMinimumWidth(340);
    m_effectContainer = new QWidget();
    m_effectContainerLayout = new QVBoxLayout(m_effectContainer);
    m_effectContainerLayout->setAlignment(Qt::AlignTop);

    // 「エフェクトを追加」ボタン(エフェクトGroupBox群の下、常にレイアウトの固定位置に残す)
    m_addEffectButton = new QPushButton(tr("エフェクトを追加"), m_effectContainer);
    auto* addMenu = new QMenu(m_addEffectButton);
    const struct {
        core::EffectType type;
        const char* label;
    } kTypes[] = {
        {core::EffectType::Blur, "ブラー"},
        {core::EffectType::Glow, "グロー"},
        {core::EffectType::Para, "パラ"},
        {core::EffectType::Shake, "シェイク"},
        {core::EffectType::ColorCorrect, "色調補正"},
        {core::EffectType::Diffusion, "ディフュージョン"},
        {core::EffectType::RadialBlur, "放射ブラー"},
        {core::EffectType::Vignette, "ビネット"},
        {core::EffectType::Grain, "グレイン"},
        {core::EffectType::ChromAb, "色収差"},
        {core::EffectType::Film, "フィルム"},
    };
    for (const auto& entry : kTypes) {
        QAction* action = addMenu->addAction(QString::fromUtf8(entry.label));
        const int typeInt = static_cast<int>(entry.type);
        connect(action, &QAction::triggered, this, [this, typeInt] { addEffectOfType(typeInt); });
    }
    m_addEffectButton->setMenu(addMenu);
    m_effectContainerLayout->addWidget(m_addEffectButton);

    // クラシック撮影(マルチプレーン撮影台)パネル。追加ボタンのさらに下に配置する
    m_multiplaneGroup = new QGroupBox(tr("クラシック撮影(マルチプレーン)"), m_effectContainer);
    m_multiplaneGroup->setCheckable(true);
    m_multiplaneGroup->setChecked(false);
    auto* mpLayout = new QVBoxLayout(m_multiplaneGroup);

    // キー追加/削除の小ボタンを1つ作る共通ヘルパー(◆規則: 現在コマにキーを追加/削除する)
    const auto makeKeyButton = [this](const QString& text, const QString& tooltip) {
        auto* button = new QToolButton(m_multiplaneGroup);
        button->setText(text);
        button->setToolTip(tooltip);
        return button;
    };

    auto* mpForm = new QFormLayout();
    m_mpFocalSpin = new QDoubleSpinBox(m_multiplaneGroup);
    m_mpFocalSpin->setRange(1.0, 300.0);
    m_mpFocalSpin->setDecimals(1);
    m_mpFocalSpin->setSuffix(tr(" mm"));
    m_mpFocalSpin->setFocusPolicy(Qt::ClickFocus);
    m_mpFocalSpin->setKeyboardTracking(false);  // 入力途中で処理を走らせない(確定時のみ)
    m_mpFocalKeyAddButton = makeKeyButton(tr("キー追加"), tr("現在コマに焦点距離のキーを追加"));
    m_mpFocalKeyRemoveButton = makeKeyButton(tr("キー削除"), tr("現在コマの焦点距離キーを削除"));
    auto* focalRow = new QHBoxLayout();
    focalRow->addWidget(m_mpFocalSpin, 1);
    focalRow->addWidget(m_mpFocalKeyAddButton);
    focalRow->addWidget(m_mpFocalKeyRemoveButton);
    mpForm->addRow(tr("焦点距離"), focalRow);

    m_mpSensorSpin = new QDoubleSpinBox(m_multiplaneGroup);
    m_mpSensorSpin->setRange(1.0, 100.0);
    m_mpSensorSpin->setDecimals(1);
    m_mpSensorSpin->setSuffix(tr(" mm"));
    m_mpSensorSpin->setFocusPolicy(Qt::ClickFocus);
    m_mpSensorSpin->setKeyboardTracking(false);
    m_mpSensorSpin->setToolTip(tr("「写る幅で固定」がONのときはこの値は使われず、写る幅/基準距離から自動算出されます"));
    mpForm->addRow(tr("センサー幅"), m_mpSensorSpin);

    // フレーミング固定: 焦点距離を変えても基準距離での構図が変わらないようにする(望遠でも構図維持)
    m_mpFramingLockCheck = new QCheckBox(tr("写る幅で固定"), m_multiplaneGroup);
    m_mpFramingLockCheck->setToolTip(
        tr("ONのとき「写る幅」「基準距離」からセンサー幅を自動算出する(焦点距離を変えても\n"
           "基準距離での構図が変わらない)。センサー幅は編集できなくなる"));
    mpForm->addRow(QString(), m_mpFramingLockCheck);

    m_mpFramingWidthSpin = new QDoubleSpinBox(m_multiplaneGroup);
    m_mpFramingWidthSpin->setRange(10.0, 10000.0);
    m_mpFramingWidthSpin->setDecimals(1);
    m_mpFramingWidthSpin->setSuffix(tr(" mm"));
    m_mpFramingWidthSpin->setFocusPolicy(Qt::ClickFocus);
    m_mpFramingWidthSpin->setKeyboardTracking(false);
    mpForm->addRow(tr("写る幅"), m_mpFramingWidthSpin);

    m_mpFramingRefDistanceSpin = new QDoubleSpinBox(m_multiplaneGroup);
    m_mpFramingRefDistanceSpin->setRange(10.0, 10000.0);
    m_mpFramingRefDistanceSpin->setDecimals(1);
    m_mpFramingRefDistanceSpin->setSuffix(tr(" mm"));
    m_mpFramingRefDistanceSpin->setFocusPolicy(Qt::ClickFocus);
    m_mpFramingRefDistanceSpin->setKeyboardTracking(false);
    mpForm->addRow(tr("基準距離"), m_mpFramingRefDistanceSpin);

    m_mpFStopSpin = new QDoubleSpinBox(m_multiplaneGroup);
    m_mpFStopSpin->setRange(0.0, 32.0);
    m_mpFStopSpin->setDecimals(1);
    m_mpFStopSpin->setSpecialValueText(tr("パンフォーカス"));
    m_mpFStopSpin->setFocusPolicy(Qt::ClickFocus);
    m_mpFStopSpin->setKeyboardTracking(false);
    m_mpFStopKeyAddButton = makeKeyButton(tr("キー追加"), tr("現在コマに絞り(F値)のキーを追加"));
    m_mpFStopKeyRemoveButton = makeKeyButton(tr("キー削除"), tr("現在コマの絞り(F値)キーを削除"));
    auto* fstopRow = new QHBoxLayout();
    fstopRow->addWidget(m_mpFStopSpin, 1);
    fstopRow->addWidget(m_mpFStopKeyAddButton);
    fstopRow->addWidget(m_mpFStopKeyRemoveButton);
    mpForm->addRow(tr("絞り(F値)"), fstopRow);

    m_mpFocusSpin = new QDoubleSpinBox(m_multiplaneGroup);
    m_mpFocusSpin->setRange(10.0, 10000.0);
    m_mpFocusSpin->setDecimals(1);
    m_mpFocusSpin->setSuffix(tr(" mm"));
    m_mpFocusSpin->setFocusPolicy(Qt::ClickFocus);
    m_mpFocusSpin->setKeyboardTracking(false);
    m_mpFocusKeyAddButton = makeKeyButton(tr("キー追加"), tr("現在コマにフォーカス距離のキーを追加"));
    m_mpFocusKeyRemoveButton = makeKeyButton(tr("キー削除"), tr("現在コマのフォーカス距離キーを削除"));
    auto* focusRow = new QHBoxLayout();
    focusRow->addWidget(m_mpFocusSpin, 1);
    focusRow->addWidget(m_mpFocusKeyAddButton);
    focusRow->addWidget(m_mpFocusKeyRemoveButton);
    mpForm->addRow(tr("フォーカス距離"), focusRow);

    m_mpSamplesSpin = new QSpinBox(m_multiplaneGroup);
    m_mpSamplesSpin->setRange(1, 128);
    m_mpSamplesSpin->setFocusPolicy(Qt::ClickFocus);
    m_mpSamplesSpin->setKeyboardTracking(false);
    m_mpSamplesSpin->setToolTip(tr("作業表示・編集ウィンドウ用のサンプル数。撮影ウィンドウのプレビューは常に軽く絞られます"));
    mpForm->addRow(tr("サンプル数(作業)"), m_mpSamplesSpin);

    // 書き出し用サンプル数(高いほどレイトレースのノイズが減ってなめらか)。作業用と分離する
    m_mpExportSamplesSpin = new QSpinBox(m_multiplaneGroup);
    m_mpExportSamplesSpin->setRange(1, 512);
    m_mpExportSamplesSpin->setFocusPolicy(Qt::ClickFocus);
    m_mpExportSamplesSpin->setKeyboardTracking(false);
    m_mpExportSamplesSpin->setToolTip(tr("連番PNG/mp4の書き出し時に使うサンプル数。大きいほどなめらかだが書き出しは遅くなります"));
    mpForm->addRow(tr("サンプル数(書き出し)"), m_mpExportSamplesSpin);
    mpLayout->addLayout(mpForm);

    m_mpTable = new QTableWidget(m_multiplaneGroup);
    m_mpTable->setColumnCount(3);
    m_mpTable->setHorizontalHeaderLabels({tr("セル"), tr("距離mm"), tr("幅mm")});
    m_mpTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_mpTable->verticalHeader()->setVisible(false);
    m_mpTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mpTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mpTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mpTable->setMaximumHeight(140);
    mpLayout->addWidget(m_mpTable);

    auto* mpButtonRow = new QHBoxLayout();
    m_mpAddButton = new QPushButton(tr("段を追加"), m_multiplaneGroup);
    m_mpRemoveButton = new QPushButton(tr("段を削除"), m_multiplaneGroup);
    mpButtonRow->addWidget(m_mpAddButton);
    mpButtonRow->addWidget(m_mpRemoveButton);
    mpLayout->addLayout(mpButtonRow);

    // 透過光(T光)パネル: クラシック撮影台の下からの光源(実物の透過光撮影の二重露光を再現する)。
    // 段割付テーブルの下に配置する。複数灯対応: 灯一覧(チェック=enabled+名前)から選んだ灯の
    // パラメータ群を編集する(蛍光灯と液晶が別タイミングで明滅する、等の使い方を想定)
    m_backlightGroup = new QGroupBox(tr("透過光(T光)"), m_multiplaneGroup);
    auto* blGroupLayout = new QVBoxLayout(m_backlightGroup);

    m_blList = new QListWidget(m_backlightGroup);
    m_blList->setFixedHeight(100);
    m_blList->setSelectionMode(QAbstractItemView::SingleSelection);
    blGroupLayout->addWidget(m_blList);

    auto* blListButtonRow = new QHBoxLayout();
    m_blAddButton = new QPushButton(tr("灯を追加"), m_backlightGroup);
    m_blRemoveButton = new QPushButton(tr("灯を削除"), m_backlightGroup);
    m_blRenameButton = new QPushButton(tr("名前変更"), m_backlightGroup);
    blListButtonRow->addWidget(m_blAddButton);
    blListButtonRow->addWidget(m_blRemoveButton);
    blListButtonRow->addWidget(m_blRenameButton);
    blGroupLayout->addLayout(blListButtonRow);

    auto* blForm = new QFormLayout();

    m_blIntensitySpin = new QDoubleSpinBox(m_backlightGroup);
    m_blIntensitySpin->setRange(0.0, 16.0);
    m_blIntensitySpin->setDecimals(2);
    m_blIntensitySpin->setSingleStep(0.1);
    m_blIntensitySpin->setFocusPolicy(Qt::ClickFocus);
    m_blIntensitySpin->setKeyboardTracking(false);
    m_blIntensityKeyAddButton = makeKeyButton(tr("キー追加"), tr("現在コマに強度のキーを追加(蛍光灯/液晶の点滅に)"));
    m_blIntensityKeyRemoveButton = makeKeyButton(tr("キー削除"), tr("現在コマの強度キーを削除"));
    auto* intensityRow = new QHBoxLayout();
    intensityRow->addWidget(m_blIntensitySpin, 1);
    intensityRow->addWidget(m_blIntensityKeyAddButton);
    intensityRow->addWidget(m_blIntensityKeyRemoveButton);
    blForm->addRow(tr("強度"), intensityRow);

    auto* blColorRow = new QHBoxLayout();
    m_blColorRSpin = new QDoubleSpinBox(m_backlightGroup);
    m_blColorGSpin = new QDoubleSpinBox(m_backlightGroup);
    m_blColorBSpin = new QDoubleSpinBox(m_backlightGroup);
    for (QDoubleSpinBox* spin : {m_blColorRSpin, m_blColorGSpin, m_blColorBSpin}) {
        spin->setRange(0.0, 1.0);
        spin->setDecimals(2);
        spin->setSingleStep(0.05);
        spin->setFocusPolicy(Qt::ClickFocus);
        spin->setKeyboardTracking(false);
    }
    blColorRow->addWidget(m_blColorRSpin);
    blColorRow->addWidget(m_blColorGSpin);
    blColorRow->addWidget(m_blColorBSpin);
    // 色見本(スウォッチ): 数値だけだと見にくいのでクリックでQColorDialogから選べる背景色ボタン
    m_blColorSwatch = new QPushButton(m_backlightGroup);
    m_blColorSwatch->setFixedWidth(40);
    m_blColorSwatch->setToolTip(tr("クリックして光源色を選ぶ"));
    connect(m_blColorSwatch, &QPushButton::clicked, this, &ShootingWindow::onBacklightColorSwatchClicked);
    blColorRow->addWidget(m_blColorSwatch);
    blForm->addRow(tr("光源色 R/G/B"), blColorRow);

    m_blTransmittanceSpin = new QDoubleSpinBox(m_backlightGroup);
    m_blTransmittanceSpin->setRange(0.0, 1.0);
    m_blTransmittanceSpin->setDecimals(2);
    m_blTransmittanceSpin->setSingleStep(0.05);
    m_blTransmittanceSpin->setFocusPolicy(Qt::ClickFocus);
    m_blTransmittanceSpin->setKeyboardTracking(false);
    blForm->addRow(tr("塗料透過率"), m_blTransmittanceSpin);

    m_blBloomRadiusSpin = new QDoubleSpinBox(m_backlightGroup);
    m_blBloomRadiusSpin->setRange(0.0, 200.0);
    m_blBloomRadiusSpin->setDecimals(1);
    m_blBloomRadiusSpin->setSuffix(tr(" px"));
    m_blBloomRadiusSpin->setFocusPolicy(Qt::ClickFocus);
    m_blBloomRadiusSpin->setKeyboardTracking(false);
    blForm->addRow(tr("にじみ半径"), m_blBloomRadiusSpin);

    m_blBloomStrengthSpin = new QDoubleSpinBox(m_backlightGroup);
    m_blBloomStrengthSpin->setRange(0.0, 4.0);
    m_blBloomStrengthSpin->setDecimals(2);
    m_blBloomStrengthSpin->setSingleStep(0.05);
    m_blBloomStrengthSpin->setFocusPolicy(Qt::ClickFocus);
    m_blBloomStrengthSpin->setKeyboardTracking(false);
    blForm->addRow(tr("にじみ強さ"), m_blBloomStrengthSpin);

    // 光源マスク(T光にもマスクを): ペンで塗る、またはセル/レイヤーの形を光源として使う
    auto* blMaskRow = new QHBoxLayout();
    m_blMaskEditButton = new QToolButton(m_backlightGroup);
    m_blMaskEditButton->setText(tr("マスクをペンで編集"));
    m_blMaskEditButton->setToolTip(tr("ペンで塗った範囲だけに透過光を絞る(AEのマスクと同様)"));
    connect(m_blMaskEditButton, &QToolButton::clicked, this, &ShootingWindow::openBacklightMaskEditDialog);
    blMaskRow->addWidget(m_blMaskEditButton);
    m_blMaskClearButton = new QPushButton(tr("マスク全消去"), m_backlightGroup);
    connect(m_blMaskClearButton, &QPushButton::clicked, this, [this] {
        core::MultiplaneBacklight* bl = selectedBacklight();
        if (!bl) return;
        bl->mask = core::Bitmap();
        if (m_backlightMaskDialog) closeBacklightMaskDialogIfOpen();  // 開いていれば作り直しのため一旦閉じる
        markEdited();
    });
    blMaskRow->addWidget(m_blMaskClearButton);
    blForm->addRow(tr("光源マスク"), blMaskRow);

    m_blMaskCelCombo = new QComboBox(m_backlightGroup);
    connect(m_blMaskCelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ShootingWindow::onBacklightMaskCelChanged);
    blForm->addRow(tr("マスクセル"), m_blMaskCelCombo);

    m_blMaskLayerCombo = new QComboBox(m_backlightGroup);
    connect(m_blMaskLayerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ShootingWindow::onBacklightMaskLayerChanged);
    blForm->addRow(tr("マスクレイヤー"), m_blMaskLayerCombo);

    blGroupLayout->addLayout(blForm);
    mpLayout->addWidget(m_backlightGroup);

    m_effectContainerLayout->addWidget(m_multiplaneGroup);

    m_effectScroll->setWidget(m_effectContainer);
    topSplit->addWidget(m_effectScroll);

    // --- 右: プレビュー+トランスポート ---
    auto* rightContainer = new QWidget(topSplit);
    auto* rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(4, 0, 0, 0);

    m_previewLabel = new QLabel(rightContainer);
    m_previewLabel->setMinimumSize(640, 360);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet(QStringLiteral("background-color: black;"));
    rightLayout->addWidget(m_previewLabel, 1);

    auto* transportRow = new QHBoxLayout();
    m_playButton = new QPushButton(tr("再生"), rightContainer);
    transportRow->addWidget(m_playButton);
    m_komaLabel = new QLabel(rightContainer);
    transportRow->addWidget(m_komaLabel);
    transportRow->addStretch(1);
    rightLayout->addLayout(transportRow);

    topSplit->addWidget(rightContainer);
    topSplit->setStretchFactor(0, 0);
    topSplit->setStretchFactor(1, 1);
    topSplit->setSizes({380, 1000});

    vSplit->addWidget(topSplit);

    // ==== 下段: タイムライン ====
    auto* timelinePanel = new QWidget(vSplit);
    auto* timelineLayout = new QVBoxLayout(timelinePanel);
    timelineLayout->setContentsMargins(0, 4, 0, 0);
    timelineLayout->addWidget(new QLabel(tr("タイムライン"), timelinePanel));
    m_timeline = new QTableWidget(timelinePanel);
    m_timeline->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_timeline->setSelectionMode(QAbstractItemView::NoSelection);
    m_timeline->horizontalHeader()->setDefaultSectionSize(28);
    m_timeline->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    timelineLayout->addWidget(m_timeline);
    vSplit->addWidget(timelinePanel);
    vSplit->setStretchFactor(0, 3);
    vSplit->setStretchFactor(1, 1);
    vSplit->setSizes({640, 220});

    rootLayout->addWidget(vSplit);
    setCentralWidget(central);

    connect(m_playButton, &QPushButton::clicked, this, &ShootingWindow::togglePlayback);
    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(1000 / 24);
    connect(m_playTimer, &QTimer::timeout, this, &ShootingWindow::onPlaybackTick);

    // プレビュー更新のデバウンス: 値編集やスクラブが連続しても、止まってから1回だけ重い合成を走らせる
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(200);
    connect(m_previewTimer, &QTimer::timeout, this, &ShootingWindow::renderPreviewNow);

    connect(m_timeline, &QTableWidget::cellClicked, this, &ShootingWindow::onTimelineCellClicked);
    connect(m_timeline, &QTableWidget::cellDoubleClicked, this, &ShootingWindow::onTimelineCellDoubleClicked);
    connect(m_timeline->horizontalHeader(), &QHeaderView::sectionClicked, this,
            &ShootingWindow::onTimelineHeaderClicked);

    connect(m_multiplaneGroup, &QGroupBox::toggled, this, &ShootingWindow::onMultiplaneToggled);
    connect(m_mpFocalSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onMultiplaneCameraChanged(); });
    connect(m_mpSensorSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onMultiplaneCameraChanged(); });
    connect(m_mpFStopSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onMultiplaneCameraChanged(); });
    connect(m_mpFocusSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onMultiplaneCameraChanged(); });
    connect(m_mpSamplesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { onMultiplaneCameraChanged(); });
    connect(m_mpExportSamplesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { onMultiplaneCameraChanged(); });
    connect(m_mpAddButton, &QPushButton::clicked, this, &ShootingWindow::addMultiplanePlaneRow);
    connect(m_mpRemoveButton, &QPushButton::clicked, this, &ShootingWindow::removeMultiplanePlaneRow);

    connect(m_mpFramingLockCheck, &QCheckBox::toggled, this, &ShootingWindow::onFramingLockToggled);
    connect(m_mpFramingWidthSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onFramingChanged(); });
    connect(m_mpFramingRefDistanceSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onFramingChanged(); });

    // 灯一覧: 選択変更(データ変更ではない)・チェック(enabled)トグル・追加/削除/名前変更
    connect(m_blList, &QListWidget::currentRowChanged, this, &ShootingWindow::onBacklightSelectionChanged);
    connect(m_blList, &QListWidget::itemChanged, this, &ShootingWindow::onBacklightItemChanged);
    connect(m_blAddButton, &QPushButton::clicked, this, &ShootingWindow::onBacklightAddClicked);
    connect(m_blRemoveButton, &QPushButton::clicked, this, &ShootingWindow::onBacklightRemoveClicked);
    connect(m_blRenameButton, &QPushButton::clicked, this, &ShootingWindow::onBacklightRenameClicked);

    // 選択中の灯のパラメータ群
    connect(m_blIntensitySpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onBacklightChanged(); });
    connect(m_blColorRSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onBacklightChanged(); });
    connect(m_blColorGSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onBacklightChanged(); });
    connect(m_blColorBSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onBacklightChanged(); });
    connect(m_blTransmittanceSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onBacklightChanged(); });
    connect(m_blBloomRadiusSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onBacklightChanged(); });
    connect(m_blBloomStrengthSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { onBacklightChanged(); });
    // 光源色スピンの変更でスウォッチ(見本)の表示も追従させる
    connect(m_blColorRSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { updateBacklightColorSwatch(); });
    connect(m_blColorGSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { updateBacklightColorSwatch(); });
    connect(m_blColorBSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { updateBacklightColorSwatch(); });

    // コマキー(点滅=透過光強度、滑らかなカメラ変化=焦点距離/フォーカス距離/絞り)の追加/削除ボタン
    connect(m_blIntensityKeyAddButton, &QToolButton::clicked, this, &ShootingWindow::onIntensityKeyAddClicked);
    connect(m_blIntensityKeyRemoveButton, &QToolButton::clicked, this,
            &ShootingWindow::onIntensityKeyRemoveClicked);
    connect(m_mpFocalKeyAddButton, &QToolButton::clicked, this, &ShootingWindow::onFocalKeyAddClicked);
    connect(m_mpFocalKeyRemoveButton, &QToolButton::clicked, this, &ShootingWindow::onFocalKeyRemoveClicked);
    connect(m_mpFocusKeyAddButton, &QToolButton::clicked, this, &ShootingWindow::onFocusKeyAddClicked);
    connect(m_mpFocusKeyRemoveButton, &QToolButton::clicked, this, &ShootingWindow::onFocusKeyRemoveClicked);
    connect(m_mpFStopKeyAddButton, &QToolButton::clicked, this, &ShootingWindow::onFStopKeyAddClicked);
    connect(m_mpFStopKeyRemoveButton, &QToolButton::clicked, this, &ShootingWindow::onFStopKeyRemoveClicked);
}

void ShootingWindow::setProject(core::Project* project) {
    m_project = project;
    m_cutIndex = 0;
    m_koma = 0;
    clearFrameCache();
}

void ShootingWindow::setCanvasSize(int width, int height) {
    m_canvasWidth = width;
    m_canvasHeight = height;
    clearFrameCache();  // 出力サイズが変わるとキャッシュ画像のサイズも合わなくなる
}

void ShootingWindow::setCutIndex(int index) {
    if (index == m_cutIndex) return;
    m_cutIndex = index;
    m_koma = 0;
    clearFrameCache();
    refresh();
}

core::Cut* ShootingWindow::currentCut() const {
    if (!m_project || m_project->sceneCount() == 0) return nullptr;
    core::Scene& scene = m_project->scene(0);
    if (m_cutIndex < 0 || static_cast<size_t>(m_cutIndex) >= scene.cutCount()) return nullptr;
    return &scene.cut(static_cast<size_t>(m_cutIndex));
}

void ShootingWindow::refresh() {
    clearFrameCache();  // カット構成(セル/エフェクト等)が変わりうるため、キャッシュは信頼できない
    closeBacklightMaskDialogIfOpen();  // カット/プロジェクト差し替えでmaskへの生ポインタ束縛が無効化する前に閉じる
    if (m_playTimer->isActive()) {
        m_playTimer->stop();
        m_playing = false;
        m_playButton->setText(tr("再生"));
    }

    m_updating = true;
    m_cutCombo->clear();
    if (m_project && m_project->sceneCount() > 0) {
        core::Scene& scene = m_project->scene(0);
        for (size_t i = 0; i < scene.cutCount(); ++i) {
            m_cutCombo->addItem(QString::fromStdString(scene.cut(i).name()));
        }
        m_cutIndex = std::clamp(m_cutIndex, 0, static_cast<int>(scene.cutCount()) - 1);
        m_cutCombo->setCurrentIndex(m_cutIndex);
    }
    m_updating = false;

    core::Cut* cut = currentCut();
    m_koma = cut ? std::clamp(m_koma, 0, static_cast<int>(cut->frameCount()) - 1) : 0;

    rebuildEffectControls();
    rebuildTimeline();
    rebuildMultiplanePanel();
    requestPreview();
    updateTransportLabel();
}

QGroupBox* ShootingWindow::buildEffectGroupBox(int effectIndex, const QStringList& celNames) {
    core::Cut* cut = currentCut();
    core::Effect& effect = cut->effects()[static_cast<size_t>(effectIndex)];

    auto* box = new QGroupBox(effectRowLabel(effect, celNames));
    box->setCheckable(true);
    box->setChecked(effect.enabled);
    connect(box, &QGroupBox::toggled, this,
            [this, effectIndex](bool checked) { onEffectEnabledChanged(effectIndex, checked); });

    auto* vlayout = new QVBoxLayout(box);

    // ヘッダ行: 対象コンボ + 上へ/下へ/削除の小ボタン
    auto* headerRow = new QHBoxLayout();
    headerRow->addWidget(new QLabel(tr("対象:"), box));
    auto* targetCombo = new QComboBox(box);
    targetCombo->addItem(tr("全体"));
    for (const QString& name : celNames) targetCombo->addItem(name);
    int comboIndex = 0;
    if (effect.targetCel >= 0 && effect.targetCel < celNames.size()) comboIndex = effect.targetCel + 1;
    targetCombo->setCurrentIndex(comboIndex);
    connect(targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, effectIndex](int index) { onEffectTargetChanged(effectIndex, index); });
    headerRow->addWidget(targetCombo, 1);

    auto* upButton = new QToolButton(box);
    upButton->setText(tr("上へ"));
    upButton->setEnabled(effectIndex > 0);
    connect(upButton, &QToolButton::clicked, this, [this, effectIndex] { moveEffect(effectIndex, -1); });
    headerRow->addWidget(upButton);

    auto* downButton = new QToolButton(box);
    downButton->setText(tr("下へ"));
    downButton->setEnabled(effectIndex + 1 < static_cast<int>(cut->effects().size()));
    connect(downButton, &QToolButton::clicked, this, [this, effectIndex] { moveEffect(effectIndex, +1); });
    headerRow->addWidget(downButton);

    auto* removeButton = new QToolButton(box);
    removeButton->setText(tr("削除"));
    connect(removeButton, &QToolButton::clicked, this, [this, effectIndex] { removeEffect(effectIndex); });
    headerRow->addWidget(removeButton);

    // 「特定の部分にエフェクトをかけたい」要望: ペンで塗った範囲だけにエフェクトを適用するマスクを編集する
    auto* maskButton = new QToolButton(box);
    maskButton->setText(tr("マスク編集"));
    maskButton->setToolTip(tr("ペンで塗った範囲だけにこのエフェクトを適用する(AEのマスクと同様)"));
    connect(maskButton, &QToolButton::clicked, this, [this, effectIndex] { openMaskEditDialog(effectIndex); });
    headerRow->addWidget(maskButton);

    vlayout->addLayout(headerRow);

    // 適用範囲行(After Effectsのin/out点): 開始コマ〜終了コマの間だけこのエフェクトを適用する。
    // 表示は1始まり(内部は0始まり)、終了コマは0=「末尾」(内部-1)
    auto* rangeRow = new QHBoxLayout();
    rangeRow->addWidget(new QLabel(tr("開始コマ:"), box));
    auto* startSpin = new QSpinBox(box);
    startSpin->setRange(1, 9999);
    startSpin->setValue(effect.startFrame + 1);
    startSpin->setFocusPolicy(Qt::ClickFocus);
    startSpin->setKeyboardTracking(false);  // 入力途中で合成を走らせない(確定時のみ)
    connect(startSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this, effectIndex](int v) { onEffectStartFrameChanged(effectIndex, v); });
    rangeRow->addWidget(startSpin);

    rangeRow->addWidget(new QLabel(tr("終了コマ:"), box));
    auto* endSpin = new QSpinBox(box);
    endSpin->setRange(0, 9999);
    endSpin->setSpecialValueText(tr("末尾"));  // 0=カット末尾まで(内部endFrame=-1)
    endSpin->setValue(effect.endFrame < 0 ? 0 : effect.endFrame + 1);
    endSpin->setFocusPolicy(Qt::ClickFocus);
    endSpin->setKeyboardTracking(false);
    connect(endSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this, effectIndex](int v) { onEffectEndFrameChanged(effectIndex, v); });
    rangeRow->addWidget(endSpin);
    rangeRow->addStretch(1);

    vlayout->addLayout(rangeRow);

    // パラメータ行: [ストップウォッチ] [パラメータ名] [スピン] [◆キー有無/移動]
    // フィルムの層別応答カーブ(respR/G/B0..4、計15個)はスピン行としては出さず、後述の
    // FilmCurveWidget(グラフ)で編集する
    const std::map<std::string, double> shown = effect.paramsAt(static_cast<size_t>(m_koma));
    for (const auto& [key, value] : shown) {
        if (key.rfind("resp", 0) == 0) continue;
        auto* row = new QHBoxLayout();
        const std::string keyCopy = key;
        const bool hasCurve = effect.hasCurve(key);

        auto* stopwatch = new QToolButton(box);
        stopwatch->setCheckable(true);
        stopwatch->setChecked(hasCurve);
        stopwatch->setText(QStringLiteral("⏱"));  // ⏱
        stopwatch->setToolTip(tr("キーフレーム有効化"));
        connect(stopwatch, &QToolButton::toggled, this,
                [this, effectIndex, keyCopy](bool checked) { onStopwatchToggled(effectIndex, keyCopy, checked); });
        row->addWidget(stopwatch);

        row->addWidget(new QLabel(paramLabel(key), box));

        auto* spin = new QDoubleSpinBox(box);
        const auto range = paramRange(effect.type, key);
        spin->setRange(range.first, range.second);
        spin->setDecimals(2);
        spin->setSingleStep(key == "grainSize" ? 0.1 : (isDensityParam(key) ? 0.05 : 1.0));
        spin->setValue(value);
        spin->setFocusPolicy(Qt::ClickFocus);
        spin->setKeyboardTracking(false);  // 入力途中で合成を走らせない(確定時のみ)
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, effectIndex, keyCopy](double v) { onParamSpinChanged(effectIndex, keyCopy, v); });
        row->addWidget(spin, 1);

        auto* diamond = new QToolButton(box);
        const bool hasKeyHere = effect.hasKeyAt(key, static_cast<size_t>(m_koma));
        diamond->setText(hasKeyHere ? QStringLiteral("◆") : (hasCurve ? QStringLiteral("◇") : QString()));
        diamond->setEnabled(hasCurve);
        diamond->setToolTip(tr("現在コマのキーをトグル"));
        connect(diamond, &QToolButton::clicked, this,
                [this, effectIndex, keyCopy] { onKeyDiamondClicked(effectIndex, keyCopy); });
        row->addWidget(diamond);

        vlayout->addLayout(row);

        ParamRowWidgets rw;
        rw.effectIndex = effectIndex;
        rw.key = key;
        rw.stopwatch = stopwatch;
        rw.spin = spin;
        rw.diamond = diamond;
        m_paramRows.push_back(rw);
    }

    // フィルム: プリセット(銘柄の特徴をまとめて適用)+層別応答カーブ(グラフ編集)
    if (effect.type == core::EffectType::Film) {
        auto* presetRow = new QHBoxLayout();
        presetRow->addWidget(new QLabel(tr("プリセット:"), box));
        auto* presetCombo = new QComboBox(box);
        presetCombo->addItem(tr("(選択して適用)"));
        presetCombo->addItem(tr("標準(恒等)"));
        presetCombo->addItem(tr("暖色ネガ"));
        presetCombo->addItem(tr("リバーサル"));
        presetCombo->addItem(tr("褪色"));
        presetCombo->setCurrentIndex(0);
        connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, effectIndex](int presetIndex) { onFilmPresetSelected(effectIndex, presetIndex); });
        presetRow->addWidget(presetCombo, 1);
        vlayout->addLayout(presetRow);

        auto* curveWidget = new FilmCurveWidget(box);
        const std::map<std::string, double> respValues = effect.paramsAt(static_cast<size_t>(m_koma));
        auto readLayer = [&](const char* prefix, double* out) {
            for (int i = 0; i < 5; ++i) {
                const std::string respKey = std::string(prefix) + std::to_string(i);
                const auto it = respValues.find(respKey);
                out[i] = it != respValues.end() ? it->second : (i / 4.0);  // 見つからなければ恒等
            }
        };
        double rPts[5], gPts[5], bPts[5];
        readLayer("respR", rPts);
        readLayer("respG", gPts);
        readLayer("respB", bPts);
        curveWidget->setPoints(0, rPts);
        curveWidget->setPoints(1, gPts);
        curveWidget->setPoints(2, bPts);
        connect(curveWidget, &FilmCurveWidget::curveChanged, this,
                [this, effectIndex](int layer, int pointIndex, double value) {
                    onFilmCurveChanged(effectIndex, layer, pointIndex, value);
                });
        connect(curveWidget, &FilmCurveWidget::curveResetRequested, this,
                [this, effectIndex](int layer) { onFilmCurveResetRequested(effectIndex, layer); });
        vlayout->addWidget(curveWidget);

        FilmCurveRowWidgets fw;
        fw.effectIndex = effectIndex;
        fw.widget = curveWidget;
        m_filmCurveRows.push_back(fw);
    }

    return box;
}

void ShootingWindow::ensureMaskAllocated(core::Effect& effect) const {
    if (!effect.mask.isEmpty()) return;
    core::Bitmap mask(m_canvasWidth, m_canvasHeight);
    mask.fill({0, 0, 0, 0});  // 全面透明(=描画前は非マスク=全面適用のまま)
    effect.mask = std::move(mask);
}

void ShootingWindow::closeMaskEditDialogIfOpen() {
    if (!m_maskEditDialog) return;
    m_maskEditDialog->close();  // WA_DeleteOnCloseで破棄される。finishedハンドラでポインタもクリアされる
}

void ShootingWindow::openMaskEditDialog(int effectIndex) {
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;

    closeMaskEditDialogIfOpen();  // 通常起きないが、念のため既存のダイアログを閉じてから開き直す

    core::Effect& effect = cut->effects()[static_cast<size_t>(effectIndex)];
    ensureMaskAllocated(effect);

    QStringList celNames;
    for (size_t ci = 0; ci < cut->celCount(); ++ci) celNames.append(QString::fromStdString(cut->cel(ci).name()));

    auto* dialog = new QDialog(this);
    dialog->setWindowTitle(tr("マスク編集 - %1").arg(effectRowLabel(effect, celNames)));
    dialog->resize(960, 540);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QVBoxLayout(dialog);

    // ツール行: ペン/消しゴムのトグル・太さスライダー・全消去・反転
    auto* toolRow = new QHBoxLayout();
    auto* penButton = new QToolButton(dialog);
    penButton->setText(tr("ペン"));
    penButton->setCheckable(true);
    penButton->setChecked(true);
    auto* eraserButton = new QToolButton(dialog);
    eraserButton->setText(tr("消しゴム"));
    eraserButton->setCheckable(true);
    penButton->setAutoExclusive(true);
    eraserButton->setAutoExclusive(true);
    toolRow->addWidget(penButton);
    toolRow->addWidget(eraserButton);

    toolRow->addWidget(new QLabel(tr("太さ"), dialog));
    constexpr int kMaskRadiusMin = 10;
    constexpr int kMaskRadiusMax = 200;
    constexpr int kMaskRadiusDefault = 40;
    auto* radiusSlider = new QSlider(Qt::Horizontal, dialog);
    radiusSlider->setRange(kMaskRadiusMin, kMaskRadiusMax);
    radiusSlider->setValue(kMaskRadiusDefault);
    radiusSlider->setFixedWidth(160);
    toolRow->addWidget(radiusSlider);
    auto* radiusValueLabel = new QLabel(QString::number(kMaskRadiusDefault), dialog);
    radiusValueLabel->setFixedWidth(32);
    toolRow->addWidget(radiusValueLabel);

    auto* clearButton = new QPushButton(tr("全消去"), dialog);
    toolRow->addWidget(clearButton);
    auto* invertButton = new QPushButton(tr("反転"), dialog);
    toolRow->addWidget(invertButton);
    toolRow->addStretch(1);
    layout->addLayout(toolRow);

    auto* canvas = new GLCanvas(dialog);
    canvas->setCanvasSize(m_canvasWidth, m_canvasHeight);
    canvas->setTool(GLCanvas::Tool::Pen);
    canvas->setPenColor(kMaskPenColor);
    canvas->setPenRadius(static_cast<float>(kMaskRadiusDefault));
    canvas->setEraserRadius(static_cast<float>(kMaskRadiusDefault));
    canvas->setBitmap(&effect.mask);
    layout->addWidget(canvas, 1);

    // 下敷き: このエフェクトを一時的に無効化した状態の現在コマ合成画像を薄く表示し、塗る目安にする
    {
        const bool originalEnabled = effect.enabled;
        effect.enabled = false;
        core::RenderOptions options;
        options.multiplaneSampleCap = 4;
        const core::Bitmap under =
            core::renderCutFrame(*cut, static_cast<size_t>(m_koma), m_canvasWidth, m_canvasHeight, options);
        effect.enabled = originalEnabled;
        // QImageはunder(ローカル変数)のバッファを参照するだけなので、copy()して寿命を切り離してから渡す
        const QImage underImage =
            QImage(under.data(), under.width(), under.height(), QImage::Format_RGBA8888).copy();
        canvas->setUnderlayImage(underImage);
        canvas->setUnderlayOpacity(0.5f);
    }

    connect(penButton, &QToolButton::toggled, canvas, [canvas, radiusSlider](bool checked) {
        if (!checked) return;
        canvas->setTool(GLCanvas::Tool::Pen);
        canvas->setPenRadius(static_cast<float>(radiusSlider->value()));
    });
    connect(eraserButton, &QToolButton::toggled, canvas, [canvas, radiusSlider](bool checked) {
        if (!checked) return;
        canvas->setTool(GLCanvas::Tool::Eraser);
        canvas->setEraserRadius(static_cast<float>(radiusSlider->value()));
    });
    connect(radiusSlider, &QSlider::valueChanged, dialog, [canvas, radiusValueLabel, penButton](int value) {
        radiusValueLabel->setText(QString::number(value));
        if (penButton->isChecked()) {
            canvas->setPenRadius(static_cast<float>(value));
        } else {
            canvas->setEraserRadius(static_cast<float>(value));
        }
    });

    connect(clearButton, &QPushButton::clicked, this, [this, effectIndex, canvas] {
        core::Cut* c = currentCut();
        if (!c || effectIndex < 0 || effectIndex >= static_cast<int>(c->effects().size())) return;
        core::Effect& e = c->effects()[static_cast<size_t>(effectIndex)];
        e.mask = core::Bitmap();  // 空に戻す(core::renderCutFrame上は「空=全面適用」になる)
        ensureMaskAllocated(e);   // 引き続きこのダイアログで塗れるよう、透明ビットマップを確保し直す
        canvas->setBitmap(&e.mask);
        canvas->clearTextureCache();
        markEdited();
    });

    connect(invertButton, &QPushButton::clicked, this, [this, effectIndex, canvas] {
        core::Cut* c = currentCut();
        if (!c || effectIndex < 0 || effectIndex >= static_cast<int>(c->effects().size())) return;
        core::Effect& e = c->effects()[static_cast<size_t>(effectIndex)];
        if (e.mask.isEmpty()) return;
        for (int y = 0; y < e.mask.height(); ++y) {
            for (int x = 0; x < e.mask.width(); ++x) {
                core::Bitmap::Pixel p = e.mask.pixel(x, y);
                p.r = kMaskPenColor.red();
                p.g = kMaskPenColor.green();
                p.b = kMaskPenColor.blue();
                p.a = static_cast<uint8_t>(255 - p.a);
                e.mask.setPixel(x, y, p);
            }
        }
        canvas->clearTextureCache();
        canvas->update();
        markEdited();
    });

    // ストローク完了ごとにプレビュー更新+edited通知(Undoコマンド自体はここでは使わない)
    canvas->setStrokeCommandSink([this](std::unique_ptr<core::Command>) { markEdited(); });

    m_maskEditDialog = dialog;
    m_maskEditEffectIndex = effectIndex;
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        if (m_maskEditDialog == dialog) {
            m_maskEditDialog = nullptr;
            m_maskEditEffectIndex = -1;
        }
    });

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void ShootingWindow::rebuildEffectControls() {
    // エフェクトの追加/削除/並べ替え/対象変更でCut::effects()が再配置されうるため、
    // マスク編集ダイアログがeffect.maskへ束縛している生ポインタが無効化する前に閉じる(簡易な安全策)
    closeMaskEditDialogIfOpen();

    m_updating = true;
    m_paramRows.clear();
    m_filmCurveRows.clear();

    // 既存のエフェクトGroupBoxを全て破棄する(「エフェクトを追加」ボタン以降は維持する)
    while (m_effectContainerLayout->count() > 0) {
        QLayoutItem* item = m_effectContainerLayout->itemAt(0);
        if (item->widget() == m_addEffectButton) break;
        m_effectContainerLayout->removeItem(item);
        delete item->widget();
        delete item;
    }

    core::Cut* cut = currentCut();
    if (cut) {
        QStringList celNames;
        for (size_t ci = 0; ci < cut->celCount(); ++ci) celNames.append(QString::fromStdString(cut->cel(ci).name()));
        for (int i = 0; i < static_cast<int>(cut->effects().size()); ++i) {
            m_effectContainerLayout->insertWidget(i, buildEffectGroupBox(i, celNames));
        }
    }
    m_addEffectButton->setEnabled(cut != nullptr);
    m_updating = false;
}

void ShootingWindow::refreshParamRowValues() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    m_updating = true;
    for (const ParamRowWidgets& rw : m_paramRows) {
        if (rw.effectIndex < 0 || rw.effectIndex >= static_cast<int>(cut->effects().size())) continue;
        const core::Effect& effect = cut->effects()[static_cast<size_t>(rw.effectIndex)];
        const double fallback = effect.params.count(rw.key) ? effect.params.at(rw.key) : 0.0;
        if (rw.spin) rw.spin->setValue(effect.valueAt(rw.key, static_cast<size_t>(m_koma), fallback));
        if (rw.diamond) {
            const bool hasCurve = effect.hasCurve(rw.key);
            const bool hasKeyHere = effect.hasKeyAt(rw.key, static_cast<size_t>(m_koma));
            rw.diamond->setText(hasKeyHere ? QStringLiteral("◆")
                                            : (hasCurve ? QStringLiteral("◇") : QString()));
            rw.diamond->setEnabled(hasCurve);
        }
    }
    m_updating = false;
}

void ShootingWindow::rebuildTimeline() {
    m_updating = true;
    m_timelineRows.clear();
    core::Cut* cut = currentCut();
    if (!cut) {
        m_timeline->setRowCount(0);
        m_timeline->setColumnCount(0);
        m_updating = false;
        return;
    }

    QStringList celNames;
    for (size_t ci = 0; ci < cut->celCount(); ++ci) celNames.append(QString::fromStdString(cut->cel(ci).name()));

    // After Effectsのレイヤータイムライン風: エフェクトごとに「見出し行」1つ+その直下に
    // キー持ちパラメータの「プロパティ行」をインデントして並べる
    QStringList rowLabels;
    for (int i = 0; i < static_cast<int>(cut->effects().size()); ++i) {
        const core::Effect& effect = cut->effects()[static_cast<size_t>(i)];

        TimelineRow headerRow;
        headerRow.kind = TimelineRow::Kind::Header;
        headerRow.effectIndex = i;
        m_timelineRows.push_back(headerRow);
        rowLabels.append(effectRowLabel(effect, celNames));

        // hasCurveなパラメータ(paramCurvesに空でない曲線を持つもの)だけをプロパティ行にする
        for (const auto& [key, curve] : effect.paramCurves) {
            if (curve.empty()) continue;
            TimelineRow paramRow;
            paramRow.kind = TimelineRow::Kind::Param;
            paramRow.effectIndex = i;
            paramRow.key = key;
            m_timelineRows.push_back(paramRow);
            rowLabels.append(QStringLiteral("   └ %1").arg(paramLabel(key)));  // "  └ パラメータ名"
        }
    }

    // クラシック撮影(マルチプレーン)有効時は「撮影台」セクションを常時表示で追加する:
    // 見出し行+焦点距離/フォーカス距離/絞り(F値)+灯ごとの強度行(キーの有無に関わらず常に並べる)
    const core::MultiplaneSetup& mp = cut->multiplane();
    if (mp.enabled) {
        TimelineRow standHeader;
        standHeader.kind = TimelineRow::Kind::StandHeader;
        m_timelineRows.push_back(standHeader);
        rowLabels.append(tr("撮影台"));

        TimelineRow focalRow;
        focalRow.kind = TimelineRow::Kind::StandFocal;
        m_timelineRows.push_back(focalRow);
        rowLabels.append(QStringLiteral("   └ %1").arg(tr("焦点距離")));

        TimelineRow focusRow;
        focusRow.kind = TimelineRow::Kind::StandFocus;
        m_timelineRows.push_back(focusRow);
        rowLabels.append(QStringLiteral("   └ %1").arg(tr("フォーカス距離")));

        TimelineRow fstopRow;
        fstopRow.kind = TimelineRow::Kind::StandFStop;
        m_timelineRows.push_back(fstopRow);
        rowLabels.append(QStringLiteral("   └ %1").arg(tr("絞り(F値)")));

        for (int bi = 0; bi < static_cast<int>(mp.backlights.size()); ++bi) {
            const core::MultiplaneBacklight& bl = mp.backlights[static_cast<size_t>(bi)];
            TimelineRow blRow;
            blRow.kind = TimelineRow::Kind::StandBacklight;
            blRow.backlightIndex = bi;
            m_timelineRows.push_back(blRow);
            const QString name = bl.name.empty() ? tr("(無名灯)") : QString::fromStdString(bl.name);
            rowLabels.append(QStringLiteral("   └ %1 %2").arg(name, tr("強度")));
        }
    }

    const int rows = static_cast<int>(m_timelineRows.size());
    const int cols = static_cast<int>(cut->frameCount());
    m_timeline->setRowCount(rows);
    m_timeline->setColumnCount(cols);
    m_timeline->setVerticalHeaderLabels(rowLabels);

    QStringList colLabels;
    for (int c = 0; c < cols; ++c) colLabels.append(QString::number(c + 1));
    m_timeline->setHorizontalHeaderLabels(colLabels);

    for (int r = 0; r < rows; ++r) {
        const TimelineRow& rowInfo = m_timelineRows[static_cast<size_t>(r)];

        if (rowInfo.kind == TimelineRow::Kind::Header || rowInfo.kind == TimelineRow::Kind::Param) {
            const core::Effect& effect = cut->effects()[static_cast<size_t>(rowInfo.effectIndex)];
            if (rowInfo.kind == TimelineRow::Kind::Header) {
                // 見出し行: セルにテキストは置かず、そのエフェクトが存在する範囲(常に全コマ)を
                // refreshTimelineHighlight()で薄い色帯として塗る。有効/無効は行ヘッダの文字色で示す
                for (int c = 0; c < cols; ++c) {
                    m_timeline->setItem(r, c, new QTableWidgetItem());
                }
                if (QTableWidgetItem* vHeader = m_timeline->verticalHeaderItem(r)) {
                    QFont f = vHeader->font();
                    f.setBold(true);
                    vHeader->setFont(f);
                    vHeader->setForeground(effect.enabled ? QBrush() : QBrush(QColor(140, 140, 140)));
                }
            } else {
                for (int c = 0; c < cols; ++c) {
                    const bool hasKey = effect.hasKeyAt(rowInfo.key, static_cast<size_t>(c));
                    auto* item = new QTableWidgetItem(hasKey ? QStringLiteral("◆") : QString());
                    item->setTextAlignment(Qt::AlignCenter);
                    m_timeline->setItem(r, c, item);
                }
            }
        } else if (rowInfo.kind == TimelineRow::Kind::StandHeader) {
            // 撮影台見出し行: エフェクト見出しと同様、色帯(kStandBandColor)は
            // refreshTimelineHighlight()で全コマに塗る(in/out点の概念が無いため常に全コマ)
            for (int c = 0; c < cols; ++c) {
                m_timeline->setItem(r, c, new QTableWidgetItem());
            }
            if (QTableWidgetItem* vHeader = m_timeline->verticalHeaderItem(r)) {
                QFont f = vHeader->font();
                f.setBold(true);
                vHeader->setFont(f);
            }
        } else {
            // StandFocal/StandFocus/StandFStop/StandBacklight: キーの有無に関わらず常時表示のプロパティ行。
            // 対応するキーmapに現在コマのキーがあれば◆を表示する
            const std::map<size_t, double>* keys = nullptr;
            switch (rowInfo.kind) {
                case TimelineRow::Kind::StandFocal:
                    keys = &mp.focalKeys;
                    break;
                case TimelineRow::Kind::StandFocus:
                    keys = &mp.focusKeys;
                    break;
                case TimelineRow::Kind::StandFStop:
                    keys = &mp.fstopKeys;
                    break;
                case TimelineRow::Kind::StandBacklight:
                    if (rowInfo.backlightIndex >= 0 &&
                        rowInfo.backlightIndex < static_cast<int>(mp.backlights.size())) {
                        keys = &mp.backlights[static_cast<size_t>(rowInfo.backlightIndex)].intensityKeys;
                    }
                    break;
                default:
                    break;
            }
            for (int c = 0; c < cols; ++c) {
                const bool hasKey = keys && keys->count(static_cast<size_t>(c)) > 0;
                auto* item = new QTableWidgetItem(hasKey ? QStringLiteral("◆") : QString());
                item->setTextAlignment(Qt::AlignCenter);
                m_timeline->setItem(r, c, item);
            }
        }
    }

    m_updating = false;
    refreshTimelineHighlight();
}

void ShootingWindow::refreshTimelineHighlight() {
    if (!m_timeline) return;
    core::Cut* cut = currentCut();
    const int cols = m_timeline->columnCount();
    const int rows = m_timeline->rowCount();
    for (int c = 0; c < cols; ++c) {
        const bool isCurrent = (c == m_koma);
        QTableWidgetItem* headerItem = m_timeline->horizontalHeaderItem(c);
        if (headerItem) {
            QFont f = headerItem->font();
            f.setBold(isCurrent);
            headerItem->setFont(f);
            headerItem->setBackground(isCurrent ? QBrush(kCtiHeaderColor) : QBrush());
        }
        for (int r = 0; r < rows; ++r) {
            QTableWidgetItem* item = m_timeline->item(r, c);
            if (!item) continue;
            const TimelineRow::Kind kind = static_cast<size_t>(r) < m_timelineRows.size()
                                                ? m_timelineRows[static_cast<size_t>(r)].kind
                                                : TimelineRow::Kind::Param;
            if (isCurrent) {
                item->setBackground(QBrush(kCtiColumnColor));
            } else if (kind == TimelineRow::Kind::Header) {
                // 色帯(kEffectBandColor)は適用範囲(in/out点)のコマだけに塗る。範囲外は無地
                const int effectIndex = m_timelineRows[static_cast<size_t>(r)].effectIndex;
                const bool inRange = cut && effectIndex >= 0 &&
                                      effectIndex < static_cast<int>(cut->effects().size()) &&
                                      cut->effects()[static_cast<size_t>(effectIndex)].activeAt(
                                          static_cast<size_t>(c));
                item->setBackground(inRange ? QBrush(kEffectBandColor) : QBrush());
            } else if (kind == TimelineRow::Kind::StandHeader) {
                // 撮影台見出しはin/out点の概念が無いため常に全コマへ色帯(kStandBandColor)を塗る
                item->setBackground(QBrush(kStandBandColor));
            } else {
                item->setBackground(QBrush());
            }
        }
    }
}

double ShootingWindow::computeProxyScale() const {
    const double longest = static_cast<double>(std::max(m_canvasWidth, m_canvasHeight));
    if (longest <= 0.0) return 1.0;
    // m_previewQualityはプレビュー長辺の絶対px上限。キャンバスの長辺がそれ以下ならフル解像度
    // (拡大はしない=1.0が上限)。4K等の大きいキャンバスでは自動的に強く縮小され、プレビューの
    // 重さがキャンバスサイズに関わらずほぼ一定になる
    return std::min(1.0, m_previewQuality / longest);
}

void ShootingWindow::requestPreview() {
    // 再生中は各コマを間引かず即時に描く(デバウンスすると再生が飛ぶため)。
    // それ以外(編集・スクラブ)はデバウンス: 連続変更が落ち着いてから1回だけ重い合成を走らせる
    if (m_playing) {
        renderPreviewNow();
    } else {
        m_previewTimer->start();
    }
}

// 現在コマの見た目を決める全状態を文字列化する(コマ指紋)。同じ指紋になるコマは同じ絵になるため、
// renderPreviewNow()はこれをキーにm_frameCacheを引き、ヒットすれば合成を丸ごと省略できる
QString ShootingWindow::frameFingerprint(const core::Cut& cut, int koma) const {
    const size_t frame = static_cast<size_t>(koma);
    QString fp;
    fp.reserve(512);

    // 各セル: visible・露出(動画番号)・位置キー(x,y)
    for (size_t ci = 0; ci < cut.celCount(); ++ci) {
        const core::Cel& cel = cut.cel(ci);
        const core::Vec2 pos = cel.positionAt(frame);
        fp += cel.visible() ? QStringLiteral("1,") : QStringLiteral("0,");
        fp += QString::number(cel.exposure(frame));
        fp += QLatin1Char(',');
        fp += QString::number(pos.x, 'f', 4);
        fp += QLatin1Char(',');
        fp += QString::number(pos.y, 'f', 4);
        fp += QLatin1Char(';');
    }
    fp += QLatin1Char('|');

    // 各エフェクト: 種類・有効・適用範囲内か・対象・マスク有無・(Shake/Grainのみ)コマ番号・全パラメータ値
    for (const core::Effect& effect : cut.effects()) {
        const bool active = effect.activeAt(frame);
        fp += QString::number(static_cast<int>(effect.type));
        fp += effect.enabled ? QStringLiteral(",1,") : QStringLiteral(",0,");
        fp += active ? QStringLiteral("1,") : QStringLiteral("0,");
        fp += QString::number(effect.targetCel);
        fp += effect.mask.isEmpty() ? QStringLiteral(",0,") : QStringLiteral(",1,");
        // Shake/Grain/Filmは有効かつ適用範囲内ならコマごとに乱数(seed+koma)が変わるため、
        // komaそのものを含める(Filmは粒状パラメータがフレーム依存のハッシュノイズを使うため)
        if (active && effect.enabled &&
            (effect.type == core::EffectType::Shake || effect.type == core::EffectType::Grain ||
             effect.type == core::EffectType::Film)) {
            fp += QStringLiteral("k");
            fp += QString::number(koma);
            fp += QLatin1Char(',');
        }
        for (const auto& [key, value] : effect.paramsAt(frame)) {
            fp += QString::fromStdString(key);
            fp += QLatin1Char('=');
            fp += QString::number(value, 'f', 4);
            fp += QLatin1Char(',');
        }
        fp += QLatin1Char(';');
    }
    fp += QLatin1Char('|');

    // カメラ(PAN/T.U.)
    if (const auto camera = cut.cameraFrameAt(frame)) {
        fp += QStringLiteral("c1,");
        fp += QString::number(camera->center.x, 'f', 4);
        fp += QLatin1Char(',');
        fp += QString::number(camera->center.y, 'f', 4);
        fp += QLatin1Char(',');
        fp += QString::number(camera->scale, 'f', 4);
    } else {
        fp += QStringLiteral("c0");
    }
    fp += QLatin1Char('|');

    // クラシック撮影: パラメータ自体の変更はmarkEdited()経由の全クリアで対応するが、
    // コマキー(点滅の強度/焦点距離/フォーカス距離)はコマごとに値が変わるため、
    // このコマへ解決した値を必ず含める(含め漏れると点滅がキャッシュで固まる)
    const core::MultiplaneSetup& mp = cut.multiplane();
    fp += mp.enabled ? QStringLiteral("m1") : QStringLiteral("m0");
    if (mp.enabled) {
        fp += QLatin1Char(',');
        fp += QString::number(core::MultiplaneSetup::valueAt(mp.focalKeys, frame, mp.camera.focalLengthMm), 'f',
                              4);
        fp += QLatin1Char(',');
        fp += QString::number(core::MultiplaneSetup::valueAt(mp.focusKeys, frame, mp.camera.focusDistanceMm),
                              'f', 4);
        fp += QLatin1Char(',');
        fp += QString::number(core::MultiplaneSetup::valueAt(mp.fstopKeys, frame, mp.camera.apertureFStop), 'f',
                              4);
        fp += QLatin1Char(',');
        fp += mp.framingLock ? QStringLiteral("fl1") : QStringLiteral("fl0");
        fp += QLatin1Char(',');
        fp += QString::number(mp.framingWidthMm, 'f', 4);
        fp += QLatin1Char(',');
        fp += QString::number(mp.framingRefDistanceMm, 'f', 4);

        // 透過光(T光、複数灯): 灯ごとに有効/解決後の強度/光源マスク(ペン/セル/レイヤー)有無を含める。
        // セルマスクの形はマスクセルの露出・位置に依存するが、それらは冒頭のセル節で既に指紋へ
        // 含まれている
        for (const core::MultiplaneBacklight& bl : mp.backlights) {
            fp += QLatin1Char(';');
            fp += bl.enabled ? QStringLiteral("b1") : QStringLiteral("b0");
            if (bl.enabled) {
                fp += QLatin1Char(',');
                fp += QString::number(core::MultiplaneSetup::valueAt(bl.intensityKeys, frame, bl.intensity), 'f',
                                      4);
                fp += bl.mask.isEmpty() ? QStringLiteral(",p0,") : QStringLiteral(",p1,");
                fp += QString::number(bl.maskCelIndex);
                fp += QLatin1Char(',');
                fp += QString::number(bl.maskLayerIndex);
            }
        }
    }

    // プレビュー画質(出力解像度が変わる)。画質変更時はclearFrameCache()するので通常は不要だが、
    // 念のため指紋にも含めておく(二重の安全策)。実際にproxyScaleへ渡す計算後の値を含める
    // (m_previewQualityが同じでもキャンバスサイズが変われば実解像度が変わるため)
    fp += QLatin1Char('|');
    fp += QString::number(computeProxyScale(), 'f', 4);

    return fp;
}

void ShootingWindow::renderPreviewNow() {
    core::Cut* cut = currentCut();
    if (!cut) {
        m_previewLabel->clear();
        return;
    }

    const QString fingerprint = frameFingerprint(*cut, m_koma);
    const auto cacheIt = m_frameCache.constFind(fingerprint);
    if (cacheIt != m_frameCache.constEnd()) {
        m_previewLabel->setPixmap(*cacheIt);
        m_lastRenderNote = tr("キャッシュ");
        updateTransportLabel();
        return;
    }

    QElapsedTimer timer;
    timer.start();

    // 撮影ウィンドウのプレビューは応答性優先: クラシック撮影のサンプル数を上限2に抑え、
    // プレビュー長辺の絶対px上限(computeProxyScale、キャンバスサイズに関わらず重さがほぼ一定)
    // で縮小レンダリングする(proxyScale)。プレビューのノイズが増える分は許容する。
    // 書き出し・編集ウィンドウの通しプレビューはこれらを渡さないのでフル品質のまま
    core::RenderOptions options;
    options.multiplaneFastPreview = true;  // 被写界深度を省いたピンホール高速合成(固まらない・ノイズ無し)
    options.multiplaneSampleCap = 1;       // ピンホールなので1サンプルで十分(念のため上限も1に)
    options.proxyScale = computeProxyScale();
    const core::Bitmap bitmap =
        core::renderCutFrame(*cut, static_cast<size_t>(m_koma), m_canvasWidth, m_canvasHeight, options);
    const QImage image(bitmap.data(), bitmap.width(), bitmap.height(), QImage::Format_RGBA8888);
    QSize target = m_previewLabel->size();
    if (target.width() < 64 || target.height() < 64) target = QSize(640, 360);
    // proxyScaleにより出力は既に縮小済み(表示先とほぼ同サイズ)なので、Smoothではなく
    // 高速なFastTransformationで十分きれいに表示できる
    const QPixmap pixmap =
        QPixmap::fromImage(image.scaled(target, Qt::KeepAspectRatio, Qt::FastTransformation));
    m_previewLabel->setPixmap(pixmap);

    m_lastRenderNote = tr("描画 %1ms").arg(timer.elapsed());
    updateTransportLabel();

    if (m_frameCache.size() >= kFrameCacheLimit) m_frameCache.clear();  // 上限超過は単純に全クリア
    m_frameCache.insert(fingerprint, pixmap);
}

void ShootingWindow::updateTransportLabel() {
    core::Cut* cut = currentCut();
    if (!cut) {
        m_komaLabel->clear();
        return;
    }
    const double seconds = static_cast<double>(m_koma) / 24.0;
    QString text = tr("コマ %1 / %2 (%3s)").arg(m_koma + 1).arg(cut->frameCount()).arg(seconds, 0, 'f', 2);
    // 計測ログ(検証用): 直近のrenderPreviewNow()の所要時間、またはキャッシュヒットを付記する
    if (!m_lastRenderNote.isEmpty()) text += QStringLiteral(" ") + m_lastRenderNote;
    m_komaLabel->setText(text);
}

void ShootingWindow::setKoma(int koma, bool lightweight) {
    core::Cut* cut = currentCut();
    const int maxKoma = cut ? static_cast<int>(cut->frameCount()) - 1 : 0;
    m_koma = std::clamp(koma, 0, std::max(0, maxKoma));
    // 再生中(lightweight)は左パネルのスピン値同期を省く(誰も見ていないフォーカスの無い
    // パネルを毎コマ更新するのは無駄な負荷なので、停止時に一度フル同期すれば十分)
    if (!lightweight) {
        refreshParamRowValues();
        refreshFilmCurveWidgets();       // 開いているフィルムの応答カーブも現在コマの値へ
        refreshMultiplaneKeyedFields();  // キー持ちの強度/焦点距離/フォーカスも現在コマの補間値へ
    }
    refreshTimelineHighlight();
    requestPreview();
    updateTransportLabel();
}

// パネル+タイムラインの作り直しを次のイベントループへ遅延させる(連続要求は1回にまとめる)。
// シグナル処理中にrebuildEffectControls()を直接呼ぶと、発信元のボタン/コンボ自身を
// deleteしてしまいクラッシュするため、シグナルハンドラからは必ずこちらを使う
void ShootingWindow::scheduleRebuild() {
    if (m_rebuildScheduled) return;
    m_rebuildScheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_rebuildScheduled = false;
        rebuildEffectControls();
        rebuildTimeline();
    });
}

// タイムラインだけを次のイベントループへ遅延・合流して作り直す(scheduleRebuild()と同様の
// singleShot(0)+フラグだが、こちらはエフェクトコントロールパネルを作り直さない=編集中の
// スピンのフォーカスを守る)。スピン連打(onParamSpinChanged)のような高頻度発火元から
// 直接rebuildTimeline()を呼ぶと、連打のたびにタイムライン全体(セル群)を再構築してしまい重いため
void ShootingWindow::scheduleTimelineRebuild() {
    if (m_timelineRebuildScheduled) return;
    m_timelineRebuildScheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_timelineRebuildScheduled = false;
        rebuildTimeline();
    });
}

void ShootingWindow::addEffectOfType(int typeInt) {
    core::Cut* cut = currentCut();
    if (!cut) return;
    core::Effect effect;
    effect.type = static_cast<core::EffectType>(typeInt);
    effect.enabled = true;
    effect.targetCel = -1;  // 既定は全体
    effect.params = core::effectDefaultParams(effect.type);
    cut->effects().push_back(std::move(effect));
    scheduleRebuild();
    markEdited();
}

void ShootingWindow::removeEffect(int effectIndex) {
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    cut->effects().erase(cut->effects().begin() + effectIndex);
    scheduleRebuild();
    markEdited();
}

void ShootingWindow::moveEffect(int effectIndex, int delta) {
    core::Cut* cut = currentCut();
    if (!cut) return;
    const int newIndex = effectIndex + delta;
    if (effectIndex < 0 || newIndex < 0 || newIndex >= static_cast<int>(cut->effects().size())) return;
    std::swap(cut->effects()[static_cast<size_t>(effectIndex)], cut->effects()[static_cast<size_t>(newIndex)]);
    scheduleRebuild();
    markEdited();
}

void ShootingWindow::onEffectEnabledChanged(int effectIndex, bool enabled) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    cut->effects()[static_cast<size_t>(effectIndex)].enabled = enabled;
    markEdited();
}

void ShootingWindow::onEffectTargetChanged(int effectIndex, int comboIndex) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    cut->effects()[static_cast<size_t>(effectIndex)].targetCel = comboIndex <= 0 ? -1 : comboIndex - 1;
    scheduleRebuild();  // タイトル・タイムライン行ラベルの対象表示が変わるので作り直す(遅延)
    markEdited();
}

void ShootingWindow::onEffectStartFrameChanged(int effectIndex, int displayValue) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    cut->effects()[static_cast<size_t>(effectIndex)].startFrame = displayValue - 1;  // 表示1始まり→内部0始まり
    refreshTimelineHighlight();  // 見出し行の色帯(適用範囲)を更新する(再構築は不要)
    markEdited();
}

void ShootingWindow::onEffectEndFrameChanged(int effectIndex, int displayValue) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    cut->effects()[static_cast<size_t>(effectIndex)].endFrame = displayValue <= 0 ? -1 : displayValue - 1;
    refreshTimelineHighlight();
    markEdited();
}

void ShootingWindow::onStopwatchToggled(int effectIndex, const std::string& key, bool checked) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    core::Effect& effect = cut->effects()[static_cast<size_t>(effectIndex)];
    if (checked) {
        // 現在コマに現在値でキーを1個打つ(キーフレーム化)
        const double fallback = effect.params.count(key) ? effect.params.at(key) : 0.0;
        const double current = effect.valueAt(key, static_cast<size_t>(m_koma), fallback);
        effect.setKey(key, static_cast<size_t>(m_koma), current);
    } else {
        // 全キーを消す(以後は基本値=paramsを使う)
        effect.paramCurves[key].clear();
    }
    scheduleRebuild();
    markEdited();
}

void ShootingWindow::onParamSpinChanged(int effectIndex, const std::string& key, double value) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    core::Effect& effect = cut->effects()[static_cast<size_t>(effectIndex)];
    if (effect.hasCurve(key)) {
        // AE同様、キー持ちパラメータはスピン編集のたびに現在コマへ自動でキーを打つ
        effect.setKey(key, static_cast<size_t>(m_koma), value);
    } else {
        effect.params[key] = value;
    }

    // 対応する◆ボタンだけを軽量更新する(パネル全体は作り直さず、編集中のフォーカスを保つ)
    for (const ParamRowWidgets& rw : m_paramRows) {
        if (rw.effectIndex == effectIndex && rw.key == key && rw.diamond) {
            const bool hasCurve = effect.hasCurve(key);
            const bool hasKeyHere = effect.hasKeyAt(key, static_cast<size_t>(m_koma));
            rw.diamond->setText(hasKeyHere ? QStringLiteral("◆")
                                            : (hasCurve ? QStringLiteral("◇") : QString()));
            rw.diamond->setEnabled(hasCurve);
            break;
        }
    }

    scheduleTimelineRebuild();  // ◆有無の見た目が変わりうるが、スピン連打中に毎回全体再構築しない
    markEdited();
}

void ShootingWindow::onKeyDiamondClicked(int effectIndex, const std::string& key) {
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    core::Effect& effect = cut->effects()[static_cast<size_t>(effectIndex)];
    if (!effect.hasCurve(key)) return;  // キーフレーム化されていないパラメータは対象外
    const size_t frame = static_cast<size_t>(m_koma);
    if (effect.hasKeyAt(key, frame)) {
        effect.removeKey(key, frame);  // 最後の1個ならストップウォッチも自動でOFFになる(hasCurve==false)
    } else {
        effect.setKey(key, frame, effect.valueAt(key, frame));
    }
    scheduleRebuild();
    markEdited();
}

void ShootingWindow::onFilmCurveChanged(int effectIndex, int layer, int pointIndex, double value) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    if (layer < 0 || layer > 2 || pointIndex < 0 || pointIndex > 4) return;
    core::Effect& effect = cut->effects()[static_cast<size_t>(effectIndex)];

    const char* prefix = layer == 0 ? "respR" : (layer == 1 ? "respG" : "respB");
    const std::string key = std::string(prefix) + std::to_string(pointIndex);

    // onParamSpinChangedと同じ規則: キー持ちパラメータは現在コマへ自動でキーを打つ、
    // 無ければ基本値(params)を直接更新する
    if (effect.hasCurve(key)) {
        effect.setKey(key, static_cast<size_t>(m_koma), value);
    } else {
        effect.params[key] = value;
    }

    // ドラッグは高頻度に発火するため、ここでは軽いmarkEdited(→requestPreviewはデバウンス済み)
    // だけに留め、パネル/タイムラインの再構築は行わない(既存の「シグナルから直接rebuildを
    // 呼ばない」規約通り、必要ならscheduleRebuild経由にする)
    markEdited();
}

void ShootingWindow::onFilmCurveResetRequested(int effectIndex, int layer) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    core::Effect& effect = cut->effects()[static_cast<size_t>(effectIndex)];

    constexpr double kIdentity[5] = {0.0, 0.25, 0.5, 0.75, 1.0};
    const int layerFrom = layer < 0 ? 0 : layer;
    const int layerTo = layer < 0 ? 2 : layer;
    for (int l = layerFrom; l <= layerTo; ++l) {
        const char* prefix = l == 0 ? "respR" : (l == 1 ? "respG" : "respB");
        for (int i = 0; i < 5; ++i) {
            const std::string key = std::string(prefix) + std::to_string(i);
            if (effect.hasCurve(key)) {
                effect.setKey(key, static_cast<size_t>(m_koma), kIdentity[i]);
            } else {
                effect.params[key] = kIdentity[i];
            }
        }
    }
    scheduleRebuild();  // グラフの表示を恒等へ戻すため作り直す(次イベントループへ遅延)
    markEdited();
}

void ShootingWindow::onFilmPresetSelected(int effectIndex, int presetIndex) {
    if (m_updating || presetIndex <= 0) return;  // index0=「(選択して適用)」は何もしない
    core::Cut* cut = currentCut();
    if (!cut || effectIndex < 0 || effectIndex >= static_cast<int>(cut->effects().size())) return;
    core::Effect& effect = cut->effects()[static_cast<size_t>(effectIndex)];
    if (effect.type != core::EffectType::Film) return;

    // フィルム銘柄の特徴プリセット: 層別応答カーブ(resp15)+基本パラメータをまとめて適用する。
    // 各層はrespX0..respX4(入力光の強さ0,0.25,0.5,0.75,1.0における記録量)
    struct FilmPreset {
        double contrast, fade, warmth, crosstalk;
        double respR[5], respG[5], respB[5];
    };
    static const FilmPreset kPresets[] = {
        // 1: 標準(恒等) — 既定のフィルムパラメータそのもの
        {0.35, 0.04, 0.1, 0.08,
         {0.0, 0.25, 0.5, 0.75, 1.0}, {0.0, 0.25, 0.5, 0.75, 1.0}, {0.0, 0.25, 0.5, 0.75, 1.0}},
        // 2: 暖色ネガ — シャドウが青浮きし、ハイライトが暖色(R)寄りに持ち上がる
        {0.3, 0.05, 0.25, 0.1,
         {0.0, 0.27, 0.55, 0.85, 1.0}, {0.0, 0.25, 0.5, 0.75, 0.97}, {0.08, 0.28, 0.5, 0.72, 0.95}},
        // 3: リバーサル — 高コントラスト・彩度高め(各層とも中間を締めたS字+クロストーク低め)
        {0.65, 0.0, 0.0, 0.03,
         {0.0, 0.16, 0.5, 0.84, 1.0}, {0.0, 0.16, 0.5, 0.84, 1.0}, {0.0, 0.16, 0.5, 0.84, 1.0}},
        // 4: 褪色 — ローコントラスト・緑被り(G持ち上げ、R/Bはやや抑える、黒浮き大きめ)
        {0.1, 0.18, -0.05, 0.2,
         {0.0, 0.22, 0.45, 0.68, 0.9}, {0.1, 0.32, 0.55, 0.75, 0.92}, {0.0, 0.2, 0.42, 0.65, 0.88}},
    };
    const int idx = presetIndex - 1;
    if (idx < 0 || idx >= static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]))) return;
    const FilmPreset& preset = kPresets[idx];

    // プリセットは基本値(params)を直接上書きする(既存のキーフレームには触れない仕様)
    effect.params["contrast"] = preset.contrast;
    effect.params["fade"] = preset.fade;
    effect.params["warmth"] = preset.warmth;
    effect.params["crosstalk"] = preset.crosstalk;
    for (int i = 0; i < 5; ++i) {
        effect.params["respR" + std::to_string(i)] = preset.respR[i];
        effect.params["respG" + std::to_string(i)] = preset.respG[i];
        effect.params["respB" + std::to_string(i)] = preset.respB[i];
    }

    scheduleRebuild();  // スピン値・グラフをプリセット後の値へ揃えるため作り直す
    markEdited();
}

void ShootingWindow::refreshFilmCurveWidgets() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    for (const FilmCurveRowWidgets& fw : m_filmCurveRows) {
        if (!fw.widget) continue;
        if (fw.effectIndex < 0 || fw.effectIndex >= static_cast<int>(cut->effects().size())) continue;
        const core::Effect& effect = cut->effects()[static_cast<size_t>(fw.effectIndex)];
        const std::map<std::string, double> values = effect.paramsAt(static_cast<size_t>(m_koma));
        auto readLayer = [&](const char* prefix, double* out) {
            for (int i = 0; i < 5; ++i) {
                const std::string key = std::string(prefix) + std::to_string(i);
                const auto it = values.find(key);
                out[i] = it != values.end() ? it->second : (i / 4.0);  // 見つからなければ恒等
            }
        };
        double rPts[5], gPts[5], bPts[5];
        readLayer("respR", rPts);
        readLayer("respG", gPts);
        readLayer("respB", bPts);
        fw.widget->setPoints(0, rPts);
        fw.widget->setPoints(1, gPts);
        fw.widget->setPoints(2, bPts);
    }
}

void ShootingWindow::onTimelineCellClicked(int row, int column) {
    if (m_updating || column < 0) return;
    Q_UNUSED(row);
    setKoma(column);
}

void ShootingWindow::onTimelineCellDoubleClicked(int row, int column) {
    if (row < 0 || row >= static_cast<int>(m_timelineRows.size()) || column < 0) return;
    const TimelineRow& rowInfo = m_timelineRows[static_cast<size_t>(row)];

    setKoma(column);  // ダブルクリックでもCTIを移動させておく(見た目の一貫性)

    core::Cut* cut = currentCut();
    if (!cut) return;
    const size_t frame = static_cast<size_t>(column);

    if (rowInfo.kind == TimelineRow::Kind::Param) {
        if (rowInfo.effectIndex < 0 || rowInfo.effectIndex >= static_cast<int>(cut->effects().size())) return;
        core::Effect& effect = cut->effects()[static_cast<size_t>(rowInfo.effectIndex)];
        if (effect.hasKeyAt(rowInfo.key, frame)) {
            effect.removeKey(rowInfo.key, frame);
        } else {
            effect.setKey(rowInfo.key, frame, effect.valueAt(rowInfo.key, frame));
        }
        scheduleRebuild();
        markEdited();
        return;
    }

    // 撮影台(クラシック撮影)のプロパティ行: キーの有無に関わらず常時表示なので、キーが無ければ
    // 現在の補間値(MultiplaneSetup::valueAt)で追加、あれば削除する。行数は変わらないため
    // scheduleTimelineRebuild()で十分(scheduleRebuild()はエフェクトコントロールも作り直してしまう)
    if (rowInfo.kind == TimelineRow::Kind::StandFocal || rowInfo.kind == TimelineRow::Kind::StandFocus ||
        rowInfo.kind == TimelineRow::Kind::StandFStop) {
        core::MultiplaneSetup& mp = cut->multiplane();
        std::map<size_t, double>* keys = nullptr;
        double base = 0.0;
        if (rowInfo.kind == TimelineRow::Kind::StandFocal) {
            keys = &mp.focalKeys;
            base = mp.camera.focalLengthMm;
        } else if (rowInfo.kind == TimelineRow::Kind::StandFocus) {
            keys = &mp.focusKeys;
            base = mp.camera.focusDistanceMm;
        } else {
            keys = &mp.fstopKeys;
            base = mp.camera.apertureFStop;
        }
        if (keys->count(frame) > 0) {
            keys->erase(frame);
        } else {
            const double value = core::MultiplaneSetup::valueAt(*keys, frame, base);
            (*keys)[frame] = value;
        }
        scheduleTimelineRebuild();
        refreshMultiplaneKeyedFields();
        markEdited();
        return;
    }

    if (rowInfo.kind == TimelineRow::Kind::StandBacklight) {
        core::MultiplaneSetup& mp = cut->multiplane();
        if (rowInfo.backlightIndex < 0 || rowInfo.backlightIndex >= static_cast<int>(mp.backlights.size())) return;
        core::MultiplaneBacklight& bl = mp.backlights[static_cast<size_t>(rowInfo.backlightIndex)];
        if (bl.intensityKeys.count(frame) > 0) {
            bl.intensityKeys.erase(frame);
        } else {
            const double value = core::MultiplaneSetup::valueAt(bl.intensityKeys, frame, bl.intensity);
            bl.intensityKeys[frame] = value;
        }
        scheduleTimelineRebuild();
        refreshMultiplaneKeyedFields();
        markEdited();
        return;
    }

    // Header/StandHeader: 見出し行のダブルクリックはCTI移動のみ(キー操作なし)
}

void ShootingWindow::onTimelineHeaderClicked(int column) {
    if (m_updating || column < 0) return;
    setKoma(column);
}

void ShootingWindow::togglePlayback() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    if (m_playTimer->isActive()) {
        m_playTimer->stop();
        m_playing = false;
        m_playButton->setText(tr("再生"));
        refreshParamRowValues();  // 再生中は省いていた左パネルのスピン値同期を、停止時に一度だけ行う
    } else {
        m_playing = true;
        m_playStartKoma = m_koma;
        m_playElapsed.restart();  // 実時間基準のフレームスキップ用(onPlaybackTick参照)
        m_playTimer->start();
        m_playButton->setText(tr("一時停止"));
    }
}

void ShootingWindow::onPlaybackTick() {
    core::Cut* cut = currentCut();
    if (!cut) {
        m_playTimer->stop();
        m_playing = false;
        return;
    }
    const int frameCount = static_cast<int>(cut->frameCount());
    if (frameCount <= 0) return;
    // 「本来今表示すべきコマ」を実経過時間から計算する(1コマずつ進めない)。
    // 描画が24fpsに追いつかず遅れても、コマ送りのテンポ自体は実時間どおりに保たれる
    // (キャッシュが温まれば見た目も滑らかになる)
    const qint64 elapsedMs = m_playElapsed.elapsed();
    const int advanced = static_cast<int>(elapsedMs * 24 / 1000);
    const int next = (m_playStartKoma + advanced) % frameCount;
    setKoma(next, /*lightweight=*/true);
}

void ShootingWindow::debugSelectKoma(int koma) { setKoma(koma); }

void ShootingWindow::debugTogglePlayback() { togglePlayback(); }

void ShootingWindow::debugOpenMaskEditDialog(int effectIndex) { openMaskEditDialog(effectIndex); }

QWidget* ShootingWindow::maskEditDialogWidget() const { return m_maskEditDialog; }

void ShootingWindow::markEdited() {
    clearFrameCache();  // データが変わったので、以前のコマ指紋キャッシュ画像は信頼できない
    requestPreview();
    emit edited();
}

void ShootingWindow::clearFrameCache() { m_frameCache.clear(); }

void ShootingWindow::rebuildMultiplanePanel() {
    m_updating = true;
    core::Cut* cut = currentCut();
    const core::MultiplaneSetup setup = cut ? cut->multiplane() : core::MultiplaneSetup{};

    m_multiplaneGroup->setEnabled(cut != nullptr);
    m_multiplaneGroup->setChecked(setup.enabled);
    m_mpFocalSpin->setValue(setup.camera.focalLengthMm);
    m_mpSensorSpin->setValue(setup.camera.sensorWidthMm);
    m_mpSensorSpin->setEnabled(cut != nullptr && !setup.framingLock);
    m_mpFStopSpin->setValue(setup.camera.apertureFStop);
    m_mpFocusSpin->setValue(setup.camera.focusDistanceMm);
    m_mpSamplesSpin->setValue(setup.samplesPerPixel);
    m_mpExportSamplesSpin->setValue(setup.exportSamplesPerPixel);
    m_mpFramingLockCheck->setChecked(setup.framingLock);
    m_mpFramingWidthSpin->setValue(setup.framingWidthMm);
    m_mpFramingRefDistanceSpin->setValue(setup.framingRefDistanceMm);

    QStringList celNames;
    if (cut) {
        for (size_t ci = 0; ci < cut->celCount(); ++ci) celNames.append(QString::fromStdString(cut->cel(ci).name()));
    }

    m_mpTable->setRowCount(static_cast<int>(setup.planes.size()));
    for (int r = 0; r < static_cast<int>(setup.planes.size()); ++r) {
        const core::MultiplaneCelPlane& plane = setup.planes[static_cast<size_t>(r)];

        auto* combo = new QComboBox(m_mpTable);
        combo->addItems(celNames);
        if (plane.celIndex >= 0 && plane.celIndex < celNames.size()) combo->setCurrentIndex(plane.celIndex);
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, r](int index) {
            if (m_updating) return;
            core::Cut* c = currentCut();
            if (!c || r >= static_cast<int>(c->multiplane().planes.size())) return;
            c->multiplane().planes[static_cast<size_t>(r)].celIndex = index;
            markEdited();
        });
        m_mpTable->setCellWidget(r, 0, combo);

        auto* distSpin = new QDoubleSpinBox(m_mpTable);
        distSpin->setRange(1.0, 100000.0);
        distSpin->setDecimals(1);
        distSpin->setValue(plane.distanceMm);
        distSpin->setFocusPolicy(Qt::ClickFocus);
        distSpin->setKeyboardTracking(false);
        connect(distSpin, &QDoubleSpinBox::valueChanged, this, [this, r](double value) {
            if (m_updating) return;
            core::Cut* c = currentCut();
            if (!c || r >= static_cast<int>(c->multiplane().planes.size())) return;
            c->multiplane().planes[static_cast<size_t>(r)].distanceMm = value;
            markEdited();
        });
        m_mpTable->setCellWidget(r, 1, distSpin);

        auto* widthSpin = new QDoubleSpinBox(m_mpTable);
        widthSpin->setRange(1.0, 100000.0);
        widthSpin->setDecimals(1);
        widthSpin->setValue(plane.widthMm);
        widthSpin->setFocusPolicy(Qt::ClickFocus);
        widthSpin->setKeyboardTracking(false);
        connect(widthSpin, &QDoubleSpinBox::valueChanged, this, [this, r](double value) {
            if (m_updating) return;
            core::Cut* c = currentCut();
            if (!c || r >= static_cast<int>(c->multiplane().planes.size())) return;
            c->multiplane().planes[static_cast<size_t>(r)].widthMm = value;
            markEdited();
        });
        m_mpTable->setCellWidget(r, 2, widthSpin);
    }

    m_mpAddButton->setEnabled(cut != nullptr);
    m_mpRemoveButton->setEnabled(!setup.planes.empty());

    m_backlightGroup->setEnabled(cut != nullptr);
    m_updating = false;

    // 灯一覧(選択インデックスをクランプして維持)+選択灯のパラメータ群を作り直す。
    // 内部でrebuildBacklightParamsPanel()→refreshMultiplaneKeyedFields()まで一括して行われるため、
    // ここで改めて呼ぶ必要は無い(焦点距離/フォーカス距離/絞りの補間値上書きも含まれる)
    rebuildBacklightList();
}

void ShootingWindow::onMultiplaneToggled(bool checked) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut) return;
    cut->multiplane().enabled = checked;
    markEdited();
}

void ShootingWindow::onMultiplaneCameraChanged() {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut) return;
    core::MultiplaneSetup& setup = cut->multiplane();
    setup.camera.focalLengthMm = m_mpFocalSpin->value();
    setup.camera.sensorWidthMm = m_mpSensorSpin->value();
    setup.camera.apertureFStop = m_mpFStopSpin->value();
    setup.camera.focusDistanceMm = m_mpFocusSpin->value();
    setup.samplesPerPixel = m_mpSamplesSpin->value();
    setup.exportSamplesPerPixel = m_mpExportSamplesSpin->value();
    markEdited();
}

void ShootingWindow::onFramingLockToggled(bool checked) {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut) return;
    cut->multiplane().framingLock = checked;
    m_mpSensorSpin->setEnabled(!checked);  // ONの間はセンサー幅が導出値になるため編集不可にする
    markEdited();
}

void ShootingWindow::onFramingChanged() {
    if (m_updating) return;
    core::Cut* cut = currentCut();
    if (!cut) return;
    core::MultiplaneSetup& setup = cut->multiplane();
    setup.framingWidthMm = m_mpFramingWidthSpin->value();
    setup.framingRefDistanceMm = m_mpFramingRefDistanceSpin->value();
    markEdited();
}

void ShootingWindow::addMultiplanePlaneRow() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    core::MultiplaneSetup& setup = cut->multiplane();

    // 未割付(planesに無い)の先頭セルを既定対象にする。全セル割付済みならセル0のまま
    int celIndex = 0;
    for (size_t ci = 0; ci < cut->celCount(); ++ci) {
        const bool assigned =
            std::any_of(setup.planes.begin(), setup.planes.end(),
                        [ci](const core::MultiplaneCelPlane& p) { return p.celIndex == static_cast<int>(ci); });
        if (!assigned) {
            celIndex = static_cast<int>(ci);
            break;
        }
    }
    core::MultiplaneCelPlane plane;
    plane.celIndex = celIndex;
    plane.distanceMm = 500.0;
    plane.widthMm = 400.0;
    setup.planes.push_back(plane);

    rebuildMultiplanePanel();
    markEdited();
}

void ShootingWindow::removeMultiplanePlaneRow() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    core::MultiplaneSetup& setup = cut->multiplane();
    if (setup.planes.empty()) return;

    const int row = m_mpTable->currentRow();
    const int target =
        (row >= 0 && row < static_cast<int>(setup.planes.size())) ? row : static_cast<int>(setup.planes.size()) - 1;
    setup.planes.erase(setup.planes.begin() + target);

    rebuildMultiplanePanel();
    markEdited();
}

core::MultiplaneBacklight* ShootingWindow::selectedBacklight() const {
    core::Cut* cut = currentCut();
    if (!cut) return nullptr;
    std::vector<core::MultiplaneBacklight>& backlights = cut->multiplane().backlights;
    if (m_backlightIndex < 0 || m_backlightIndex >= static_cast<int>(backlights.size())) return nullptr;
    return &backlights[static_cast<size_t>(m_backlightIndex)];
}

// 灯一覧(QListWidget)をカットの内容で作り直す(構造変更)。選択インデックス(m_backlightIndex)を
// クランプして維持し、末尾で選択灯のパラメータ群(rebuildBacklightParamsPanel)も作り直す
void ShootingWindow::rebuildBacklightList() {
    const bool wasUpdating = m_updating;
    m_updating = true;

    m_blList->clear();
    core::Cut* cut = currentCut();
    const int count = cut ? static_cast<int>(cut->multiplane().backlights.size()) : 0;
    for (int i = 0; i < count; ++i) {
        const core::MultiplaneBacklight& bl = cut->multiplane().backlights[static_cast<size_t>(i)];
        const QString label = bl.name.empty() ? tr("(無名灯)") : QString::fromStdString(bl.name);
        auto* item = new QListWidgetItem(label, m_blList);
        item->setFlags((item->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        item->setCheckState(bl.enabled ? Qt::Checked : Qt::Unchecked);
    }
    m_backlightIndex = count > 0 ? std::clamp(m_backlightIndex, 0, count - 1) : -1;
    if (m_backlightIndex >= 0) m_blList->setCurrentRow(m_backlightIndex);

    m_blAddButton->setEnabled(cut != nullptr);
    m_blRemoveButton->setEnabled(count > 0);
    m_blRenameButton->setEnabled(count > 0);

    m_updating = wasUpdating;
    rebuildBacklightParamsPanel();
}

// 選択中の灯のパラメータ群(強度/色/透過率/にじみ/マスク)をスピン等へ反映する(構造は変えない軽量更新)
void ShootingWindow::rebuildBacklightParamsPanel() {
    const bool wasUpdating = m_updating;
    m_updating = true;

    core::Cut* cut = currentCut();
    const core::MultiplaneBacklight* bl = selectedBacklight();
    const bool hasSelection = bl != nullptr;

    m_blIntensitySpin->setEnabled(hasSelection);
    m_blIntensityKeyAddButton->setEnabled(hasSelection);
    m_blColorRSpin->setEnabled(hasSelection);
    m_blColorGSpin->setEnabled(hasSelection);
    m_blColorBSpin->setEnabled(hasSelection);
    m_blColorSwatch->setEnabled(hasSelection);
    m_blTransmittanceSpin->setEnabled(hasSelection);
    m_blBloomRadiusSpin->setEnabled(hasSelection);
    m_blBloomStrengthSpin->setEnabled(hasSelection);
    m_blMaskEditButton->setEnabled(hasSelection);
    m_blMaskClearButton->setEnabled(hasSelection);
    m_blMaskCelCombo->setEnabled(hasSelection);

    const core::MultiplaneBacklight fallback{};  // 灯が0個/未選択の間はパラメータ群を既定値で仮表示(無効化済み)
    const core::MultiplaneBacklight& shown = bl ? *bl : fallback;
    m_blIntensitySpin->setValue(shown.intensity);
    m_blColorRSpin->setValue(shown.colorR);
    m_blColorGSpin->setValue(shown.colorG);
    m_blColorBSpin->setValue(shown.colorB);
    m_blTransmittanceSpin->setValue(shown.paintTransmittance);
    m_blBloomRadiusSpin->setValue(shown.bloomRadiusPx);
    m_blBloomStrengthSpin->setValue(shown.bloomStrength);
    updateBacklightColorSwatch();

    QStringList celNames;
    if (cut) {
        for (size_t ci = 0; ci < cut->celCount(); ++ci) celNames.append(QString::fromStdString(cut->cel(ci).name()));
    }
    m_blMaskCelCombo->clear();
    m_blMaskCelCombo->addItem(tr("なし"));
    m_blMaskCelCombo->addItems(celNames);
    const int maskCel = shown.maskCelIndex;
    m_blMaskCelCombo->setCurrentIndex(maskCel >= 0 && maskCel < celNames.size() ? maskCel + 1 : 0);
    refreshBacklightMaskLayerCombo();

    m_updating = wasUpdating;
    refreshMultiplaneKeyedFields();
}

// 灯リストの選択行が変わった。データ変更ではないのでmarkEditedはしない(rebuildBacklightList()中の
// プログラム的な選択更新はm_updatingで抑止される)
void ShootingWindow::onBacklightSelectionChanged(int row) {
    if (m_updating) return;
    m_backlightIndex = row;
    closeBacklightMaskDialogIfOpen();  // 選択が変わったのでBitmap*束縛を作り直す(安全策)
    rebuildBacklightParamsPanel();
}

// 灯リストのチェック(enabled)がユーザー操作でトグルされた
void ShootingWindow::onBacklightItemChanged(QListWidgetItem* item) {
    if (m_updating || !item) return;
    core::Cut* cut = currentCut();
    if (!cut) return;
    const int row = m_blList->row(item);
    std::vector<core::MultiplaneBacklight>& backlights = cut->multiplane().backlights;
    if (row < 0 || row >= static_cast<int>(backlights.size())) return;
    backlights[static_cast<size_t>(row)].enabled = (item->checkState() == Qt::Checked);
    scheduleTimelineRebuild();  // 見出し文字色等が変わりうる
    markEdited();
}

void ShootingWindow::onBacklightAddClicked() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    core::MultiplaneBacklight bl;
    const int n = static_cast<int>(cut->multiplane().backlights.size()) + 1;
    bl.name = tr("T光 %1").arg(n).toStdString();
    cut->multiplane().backlights.push_back(std::move(bl));
    m_backlightIndex = static_cast<int>(cut->multiplane().backlights.size()) - 1;  // 新しい灯を選択

    closeBacklightMaskDialogIfOpen();  // vectorの再配置でBitmap*束縛が無効化しうるため念のため閉じる
    rebuildBacklightList();
    scheduleTimelineRebuild();  // 「撮影台」セクションに強度行が1つ増える
    markEdited();
}

void ShootingWindow::onBacklightRemoveClicked() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    std::vector<core::MultiplaneBacklight>& backlights = cut->multiplane().backlights;
    if (backlights.empty()) return;

    const int row = m_blList->currentRow();
    const int target =
        (row >= 0 && row < static_cast<int>(backlights.size())) ? row : static_cast<int>(backlights.size()) - 1;
    backlights.erase(backlights.begin() + target);
    m_backlightIndex = target;  // rebuildBacklightList()内でクランプされる

    closeBacklightMaskDialogIfOpen();
    rebuildBacklightList();
    scheduleTimelineRebuild();  // 「撮影台」セクションから強度行が1つ減る
    markEdited();
}

void ShootingWindow::onBacklightRenameClicked() {
    core::MultiplaneBacklight* bl = selectedBacklight();
    if (!bl) return;
    bool ok = false;
    const QString current = QString::fromStdString(bl->name);
    const QString newName =
        QInputDialog::getText(this, tr("灯の名前"), tr("名前:"), QLineEdit::Normal, current, &ok);
    if (!ok) return;
    bl->name = newName.toStdString();

    rebuildBacklightList();       // 一覧の表示文字列を更新する
    scheduleTimelineRebuild();    // 「撮影台」セクションの行ラベルも更新する
    markEdited();
}

void ShootingWindow::onBacklightChanged() {
    if (m_updating) return;
    core::MultiplaneBacklight* backlight = selectedBacklight();
    if (!backlight) return;
    backlight->intensity = m_blIntensitySpin->value();
    backlight->colorR = m_blColorRSpin->value();
    backlight->colorG = m_blColorGSpin->value();
    backlight->colorB = m_blColorBSpin->value();
    backlight->paintTransmittance = m_blTransmittanceSpin->value();
    backlight->bloomRadiusPx = m_blBloomRadiusSpin->value();
    backlight->bloomStrength = m_blBloomStrengthSpin->value();
    markEdited();
}

// --- 透過光(T光)の色見本・光源マスク・コマキー ---

// 光源色スピン(0〜1)の現在値をスウォッチ(見本ボタン)の背景色へ反映する
void ShootingWindow::updateBacklightColorSwatch() {
    if (!m_blColorSwatch) return;
    const int r = static_cast<int>(std::lround(std::clamp(m_blColorRSpin->value(), 0.0, 1.0) * 255.0));
    const int g = static_cast<int>(std::lround(std::clamp(m_blColorGSpin->value(), 0.0, 1.0) * 255.0));
    const int b = static_cast<int>(std::lround(std::clamp(m_blColorBSpin->value(), 0.0, 1.0) * 255.0));
    m_blColorSwatch->setStyleSheet(QStringLiteral("background-color: rgb(%1,%2,%3);").arg(r).arg(g).arg(b));
}

// 色見本ボタン: QColorDialogで選んだ色をスピン(0〜1)へ反映する(数値だけだと見にくい対策)
void ShootingWindow::onBacklightColorSwatchClicked() {
    if (!selectedBacklight()) return;
    const QColor initial(static_cast<int>(std::lround(m_blColorRSpin->value() * 255.0)),
                         static_cast<int>(std::lround(m_blColorGSpin->value() * 255.0)),
                         static_cast<int>(std::lround(m_blColorBSpin->value() * 255.0)));
    const QColor picked = QColorDialog::getColor(initial, this, tr("光源色を選ぶ"));
    if (!picked.isValid()) return;

    // スピン3つの変更で onBacklightChanged が3回走らないよう、まとめて設定してから1回だけ反映する
    m_updating = true;
    m_blColorRSpin->setValue(picked.red() / 255.0);
    m_blColorGSpin->setValue(picked.green() / 255.0);
    m_blColorBSpin->setValue(picked.blue() / 255.0);
    m_updating = false;
    updateBacklightColorSwatch();
    onBacklightChanged();
}

void ShootingWindow::ensureBacklightMaskAllocated(core::MultiplaneBacklight& backlight) const {
    if (!backlight.mask.isEmpty()) return;
    core::Bitmap mask(m_canvasWidth, m_canvasHeight);
    mask.fill({0, 0, 0, 0});  // 全面透明。ペンで塗った所(alpha>0)だけ光が通るようになる
    backlight.mask = std::move(mask);
}

void ShootingWindow::closeBacklightMaskDialogIfOpen() {
    if (!m_backlightMaskDialog) return;
    m_backlightMaskDialog->close();  // WA_DeleteOnCloseで破棄。finishedハンドラでポインタもクリアされる
}

// T光の光源マスクをペンで塗るモードレスダイアログ(エフェクトのマスク編集と同じ流儀)。
// 注意: ペンマスクは「塗った所だけ光る」(空=全面)。塗り始めた時点で未塗り部は遮光になる
void ShootingWindow::openBacklightMaskEditDialog() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    core::MultiplaneBacklight* backlightPtr = selectedBacklight();
    if (!backlightPtr) return;
    core::MultiplaneBacklight& backlight = *backlightPtr;

    closeBacklightMaskDialogIfOpen();

    ensureBacklightMaskAllocated(backlight);

    const QString backlightName = backlight.name.empty() ? tr("(無名灯)") : QString::fromStdString(backlight.name);
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle(tr("光源マスク編集 - 透過光(T光) - %1").arg(backlightName));
    dialog->resize(960, 540);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QVBoxLayout(dialog);

    auto* toolRow = new QHBoxLayout();
    auto* penButton = new QToolButton(dialog);
    penButton->setText(tr("ペン"));
    penButton->setCheckable(true);
    penButton->setChecked(true);
    auto* eraserButton = new QToolButton(dialog);
    eraserButton->setText(tr("消しゴム"));
    eraserButton->setCheckable(true);
    penButton->setAutoExclusive(true);
    eraserButton->setAutoExclusive(true);
    toolRow->addWidget(penButton);
    toolRow->addWidget(eraserButton);

    toolRow->addWidget(new QLabel(tr("太さ"), dialog));
    constexpr int kMaskRadiusMin = 10;
    constexpr int kMaskRadiusMax = 200;
    constexpr int kMaskRadiusDefault = 40;
    auto* radiusSlider = new QSlider(Qt::Horizontal, dialog);
    radiusSlider->setRange(kMaskRadiusMin, kMaskRadiusMax);
    radiusSlider->setValue(kMaskRadiusDefault);
    radiusSlider->setFixedWidth(160);
    toolRow->addWidget(radiusSlider);
    auto* radiusValueLabel = new QLabel(QString::number(kMaskRadiusDefault), dialog);
    radiusValueLabel->setFixedWidth(32);
    toolRow->addWidget(radiusValueLabel);

    auto* clearButton = new QPushButton(tr("全消去"), dialog);
    clearButton->setToolTip(tr("マスクを消して全面に光が通る状態へ戻す"));
    toolRow->addWidget(clearButton);
    toolRow->addStretch(1);
    layout->addLayout(toolRow);

    auto* canvas = new GLCanvas(dialog);
    canvas->setCanvasSize(m_canvasWidth, m_canvasHeight);
    canvas->setTool(GLCanvas::Tool::Pen);
    canvas->setPenColor(kMaskPenColor);
    canvas->setPenRadius(static_cast<float>(kMaskRadiusDefault));
    canvas->setEraserRadius(static_cast<float>(kMaskRadiusDefault));
    canvas->setBitmap(&backlight.mask);
    layout->addWidget(canvas, 1);

    // 下敷き: 透過光を一時的に無効化した現在コマの合成画像を薄く表示し、光らせたい場所の目安にする
    {
        const bool originalEnabled = backlight.enabled;
        backlight.enabled = false;
        core::RenderOptions options;
        options.multiplaneSampleCap = 4;
        const core::Bitmap under =
            core::renderCutFrame(*cut, static_cast<size_t>(m_koma), m_canvasWidth, m_canvasHeight, options);
        backlight.enabled = originalEnabled;
        const QImage underImage =
            QImage(under.data(), under.width(), under.height(), QImage::Format_RGBA8888).copy();
        canvas->setUnderlayImage(underImage);
        canvas->setUnderlayOpacity(0.5f);
    }

    connect(penButton, &QToolButton::toggled, canvas, [canvas, radiusSlider](bool checked) {
        if (!checked) return;
        canvas->setTool(GLCanvas::Tool::Pen);
        canvas->setPenRadius(static_cast<float>(radiusSlider->value()));
    });
    connect(eraserButton, &QToolButton::toggled, canvas, [canvas, radiusSlider](bool checked) {
        if (!checked) return;
        canvas->setTool(GLCanvas::Tool::Eraser);
        canvas->setEraserRadius(static_cast<float>(radiusSlider->value()));
    });
    connect(radiusSlider, &QSlider::valueChanged, dialog, [canvas, radiusValueLabel, penButton](int value) {
        radiusValueLabel->setText(QString::number(value));
        if (penButton->isChecked()) {
            canvas->setPenRadius(static_cast<float>(value));
        } else {
            canvas->setEraserRadius(static_cast<float>(value));
        }
    });

    connect(clearButton, &QPushButton::clicked, this, [this, canvas] {
        core::MultiplaneBacklight* bl = selectedBacklight();
        if (!bl) return;
        bl->mask = core::Bitmap();          // 空に戻す(=全面に光が通る)
        ensureBacklightMaskAllocated(*bl);  // 引き続きこのダイアログで塗れるよう確保し直す
        canvas->setBitmap(&bl->mask);
        canvas->clearTextureCache();
        markEdited();
    });

    canvas->setStrokeCommandSink([this](std::unique_ptr<core::Command>) { markEdited(); });

    m_backlightMaskDialog = dialog;
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        if (m_backlightMaskDialog == dialog) m_backlightMaskDialog = nullptr;
    });

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

// 「マスクセル」コンボ: 0=なし、1以降=セルindex+1
void ShootingWindow::onBacklightMaskCelChanged(int comboIndex) {
    if (m_updating) return;
    core::MultiplaneBacklight* backlight = selectedBacklight();
    if (!backlight) return;
    backlight->maskCelIndex = comboIndex <= 0 ? -1 : comboIndex - 1;
    backlight->maskLayerIndex = -1;  // セルを変えたらレイヤー指定はリセット(セル全体)
    refreshBacklightMaskLayerCombo();
    markEdited();
}

// 「マスクレイヤー」コンボ: 0=セル全体、1以降=レイヤーindex+1
void ShootingWindow::onBacklightMaskLayerChanged(int comboIndex) {
    if (m_updating) return;
    core::MultiplaneBacklight* backlight = selectedBacklight();
    if (!backlight) return;
    backlight->maskLayerIndex = comboIndex <= 0 ? -1 : comboIndex - 1;
    markEdited();
}

// 「マスクレイヤー」コンボの項目を、選択中の灯のマスクセルのレイヤー一覧で作り直す。
// 表示専用なので読み取りだけ行い、cutは変更しない(未選択なら「なし」相当として扱う)
void ShootingWindow::refreshBacklightMaskLayerCombo() {
    if (!m_blMaskLayerCombo) return;
    const bool wasUpdating = m_updating;
    m_updating = true;
    m_blMaskLayerCombo->clear();
    m_blMaskLayerCombo->addItem(tr("セル全体"));

    core::Cut* cut = currentCut();
    const core::MultiplaneBacklight* bl = selectedBacklight();
    const int maskCel = bl ? bl->maskCelIndex : -1;
    const bool hasCel = cut && bl && maskCel >= 0 && static_cast<size_t>(maskCel) < cut->celCount();
    if (hasCel) {
        const core::Cel& cel = cut->cel(static_cast<size_t>(maskCel));
        for (size_t li = 0; li < cel.layerCount(); ++li) {
            m_blMaskLayerCombo->addItem(QString::fromStdString(cel.layer(li).name()));
        }
        const int layerIndex = bl->maskLayerIndex;
        m_blMaskLayerCombo->setCurrentIndex(
            layerIndex >= 0 && layerIndex < static_cast<int>(cel.layerCount()) ? layerIndex + 1 : 0);
    }
    m_blMaskLayerCombo->setEnabled(hasCel);
    m_updating = wasUpdating;
}

// --- クラシック撮影のコマキー(点滅・滑らかなカメラ変化) ---

void ShootingWindow::onIntensityKeyAddClicked() {
    core::MultiplaneBacklight* backlight = selectedBacklight();
    if (!backlight) return;
    backlight->intensityKeys[static_cast<size_t>(m_koma)] = m_blIntensitySpin->value();
    refreshMultiplaneKeyedFields();
    markEdited();
}

void ShootingWindow::onIntensityKeyRemoveClicked() {
    core::MultiplaneBacklight* backlight = selectedBacklight();
    if (!backlight) return;
    backlight->intensityKeys.erase(static_cast<size_t>(m_koma));
    refreshMultiplaneKeyedFields();
    markEdited();
}

void ShootingWindow::onFocalKeyAddClicked() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    cut->multiplane().focalKeys[static_cast<size_t>(m_koma)] = m_mpFocalSpin->value();
    refreshMultiplaneKeyedFields();
    markEdited();
}

void ShootingWindow::onFocalKeyRemoveClicked() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    cut->multiplane().focalKeys.erase(static_cast<size_t>(m_koma));
    refreshMultiplaneKeyedFields();
    markEdited();
}

void ShootingWindow::onFocusKeyAddClicked() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    cut->multiplane().focusKeys[static_cast<size_t>(m_koma)] = m_mpFocusSpin->value();
    refreshMultiplaneKeyedFields();
    markEdited();
}

void ShootingWindow::onFocusKeyRemoveClicked() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    cut->multiplane().focusKeys.erase(static_cast<size_t>(m_koma));
    refreshMultiplaneKeyedFields();
    markEdited();
}

void ShootingWindow::onFStopKeyAddClicked() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    cut->multiplane().fstopKeys[static_cast<size_t>(m_koma)] = m_mpFStopSpin->value();
    refreshMultiplaneKeyedFields();
    markEdited();
}

void ShootingWindow::onFStopKeyRemoveClicked() {
    core::Cut* cut = currentCut();
    if (!cut) return;
    cut->multiplane().fstopKeys.erase(static_cast<size_t>(m_koma));
    refreshMultiplaneKeyedFields();
    markEdited();
}

// キー持ちの強度(選択中の灯)/焦点距離/フォーカス距離/絞りスピンを現在コマの補間値へ同期し、
// キー削除ボタンの有効状態(現在コマにキーがあるときだけ)を更新する。キーが無いフィールドは基本値のまま
void ShootingWindow::refreshMultiplaneKeyedFields() {
    core::Cut* cut = currentCut();
    const bool wasUpdating = m_updating;
    m_updating = true;
    if (cut) {
        const core::MultiplaneSetup& mp = cut->multiplane();
        const size_t frame = static_cast<size_t>(m_koma);
        if (!mp.focalKeys.empty()) {
            m_mpFocalSpin->setValue(core::MultiplaneSetup::valueAt(mp.focalKeys, frame, mp.camera.focalLengthMm));
        }
        if (!mp.focusKeys.empty()) {
            m_mpFocusSpin->setValue(
                core::MultiplaneSetup::valueAt(mp.focusKeys, frame, mp.camera.focusDistanceMm));
        }
        if (!mp.fstopKeys.empty()) {
            m_mpFStopSpin->setValue(
                core::MultiplaneSetup::valueAt(mp.fstopKeys, frame, mp.camera.apertureFStop));
        }
        m_mpFocalKeyRemoveButton->setEnabled(mp.focalKeys.count(frame) > 0);
        m_mpFocusKeyRemoveButton->setEnabled(mp.focusKeys.count(frame) > 0);
        m_mpFStopKeyRemoveButton->setEnabled(mp.fstopKeys.count(frame) > 0);
        m_mpFocalKeyAddButton->setEnabled(true);
        m_mpFocusKeyAddButton->setEnabled(true);
        m_mpFStopKeyAddButton->setEnabled(true);

        const core::MultiplaneBacklight* bl = selectedBacklight();
        if (bl) {
            if (!bl->intensityKeys.empty()) {
                m_blIntensitySpin->setValue(core::MultiplaneSetup::valueAt(bl->intensityKeys, frame, bl->intensity));
            }
            m_blIntensityKeyRemoveButton->setEnabled(bl->intensityKeys.count(frame) > 0);
            m_blIntensityKeyAddButton->setEnabled(true);
        } else {
            m_blIntensityKeyRemoveButton->setEnabled(false);
            m_blIntensityKeyAddButton->setEnabled(false);
        }
    } else {
        m_mpFocalKeyRemoveButton->setEnabled(false);
        m_mpFocusKeyRemoveButton->setEnabled(false);
        m_mpFStopKeyRemoveButton->setEnabled(false);
        m_mpFocalKeyAddButton->setEnabled(false);
        m_mpFocusKeyAddButton->setEnabled(false);
        m_mpFStopKeyAddButton->setEnabled(false);
        m_blIntensityKeyRemoveButton->setEnabled(false);
        m_blIntensityKeyAddButton->setEnabled(false);
    }
    m_updating = wasUpdating;
}
