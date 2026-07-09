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

    void undo();
    void redo();

private:
    std::vector<std::unique_ptr<Command>> m_undoStack;
    std::vector<std::unique_ptr<Command>> m_redoStack;
};

}  // namespace core
