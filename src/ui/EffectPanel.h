#pragma once

#include <QDockWidget>
#include <QStringList>
#include <string>
#include <vector>

#include "core/Effect.h"

class QComboBox;
class QDialog;
class QFormLayout;
class QImage;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QWidget;

// 撮影パネル。アクティブカットのエフェクトスタック(ブラー/グロー/パラ/シェイク)を編集する。
// 一覧はチェック(有効/無効)+種類名+対象を表示し、行選択で下のパラメータ編集欄が切り替わる。
// 編集内容はローカルのm_effectsに保持し、変更のたびにeffectsEdited()を発行する。
// MainWindow側はeffects()で取得してCutへ書き戻す(CameraPanelがスピン値をローカルに持ち、
// シグナル経由でMainWindowにデータ変更を委ねる流儀に倣う)
class EffectPanel : public QDockWidget {
    Q_OBJECT

public:
    explicit EffectPanel(QWidget* parent = nullptr);

    // エフェクト一覧とセル名一覧(コア側インデックス順)を反映する(シグナルは発火しない)
    void setEffects(const std::vector<core::Effect>& effects, const QStringList& celNames);
    const std::vector<core::Effect>& effects() const { return m_effects; }

    // 撮影プレビューダイアログに画像を表示する(未生成なら作成、既にあれば更新のみ)
    void showPreview(const QImage& image);
    // ヘッドレステスト確認用: プレビューダイアログ(未表示ならnullptr)
    QDialog* previewDialog() const { return m_previewDialog; }

signals:
    void effectsEdited();     // 追加/削除/並べ替え/有効切替/対象変更/パラメータ変更のいずれか
    void previewRequested();  // 「現在コマをプレビュー」ボタン押下

private:
    void rebuildList();     // m_effectsから一覧を作り直す(選択は呼び出し側で設定する)
    void syncSelectionUI();  // 現在選択行に合わせて対象コンボ・パラメータフォームを作り直す
    void rebuildParamForm(const core::Effect* effect);  // nullptrなら空にする
    void addEffectOfType(core::EffectType type);
    void removeSelected();
    void moveSelected(int delta);
    void onCheckStateChanged(QListWidgetItem* item);
    void onTargetIndexChanged(int index);
    void onParamValueChanged(const std::string& key, double value);
    void emitEdited();

    QListWidget* m_list = nullptr;
    QComboBox* m_targetCombo = nullptr;
    QWidget* m_paramContainer = nullptr;
    QFormLayout* m_paramForm = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_upButton = nullptr;
    QPushButton* m_downButton = nullptr;

    QDialog* m_previewDialog = nullptr;  // 撮影プレビュー(モードレス、未使用時はnullptr)
    QLabel* m_previewImageLabel = nullptr;

    std::vector<core::Effect> m_effects;  // ローカル編集用コピー。MainWindowがeffects()で取得しCutへ反映する
    QStringList m_celNames;
    bool m_updating = false;  // 表示反映中(setEffects/rebuildList/syncSelectionUI)はtrue: シグナルを発火しない
};
