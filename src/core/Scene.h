#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Bitmap.h"
#include "Cut.h"

namespace core {

// 絵コンテの1パネル(コマ)。全工程の前に単体で描くため、カットや作画とは独立して存在する。
// 同じカット番号を複数パネルに書けば「1カット複数コマ」のコンテになる
struct StoryboardPanel {
    // コンテ用紙のコマ全体(罫線・見出し・カット番号・絵の枠・内容欄・セリフ欄・秒欄を
    // 1枚のQImageとして下敷きに敷いた紙全体)への手描きインク。絵の枠内はもちろん、
    // 内容欄への効果音メモやセリフ欄への書き込み、枠をまたぐ矢印なども自由に描ける(空=未描画)
    Bitmap drawing;
    std::string cutLabel;        // カット番号/名(自由記入)
    std::string action;          // 内容(アクション)
    std::string dialogue;        // セリフ
    size_t durationFrames = 24;  // このパネルの尺(コマ、24=1秒)
};

// プロジェクト内の1シーン。カットを順序付きで保持する。
class Scene {
public:
    explicit Scene(std::string name) : m_name(std::move(name)) {}

    const std::string& name() const { return m_name; }
    void setName(std::string name) { m_name = std::move(name); }

    Cut& addCut(std::string name);
    void removeCut(size_t index);

    size_t cutCount() const { return m_cuts.size(); }
    Cut& cut(size_t index) { return *m_cuts.at(index); }
    const Cut& cut(size_t index) const { return *m_cuts.at(index); }

    // 絵コンテ(パネル列)。カット制作前の単体作業として編集される
    std::vector<StoryboardPanel>& storyboard() { return m_storyboard; }
    const std::vector<StoryboardPanel>& storyboard() const { return m_storyboard; }

private:
    std::string m_name;
    std::vector<std::unique_ptr<Cut>> m_cuts;
    std::vector<StoryboardPanel> m_storyboard;
};

}  // namespace core
