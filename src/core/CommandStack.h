#pragma once

#include <memory>
#include <vector>

#include "Command.h"

namespace core {

// Undo/Redoスタック。pushしたコマンドは即座にexecute()され、undo履歴に積まれる。
class CommandStack {
public:
    void push(std::unique_ptr<Command> command);

    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }

    // 実行したコマンドを返す(何もしなかった場合はnullptr)。
    // 呼び出し側は部分再描画のためにコマンドの変更範囲を参照できる
    Command* undo();
    Command* redo();

    // 履歴を全破棄する(コマンドが参照する対象が無効になる構造変更時に呼ぶ)
    void clear();

private:
    std::vector<std::unique_ptr<Command>> m_undoStack;
    std::vector<std::unique_ptr<Command>> m_redoStack;
};

}  // namespace core
