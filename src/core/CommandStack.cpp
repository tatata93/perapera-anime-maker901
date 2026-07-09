#include "CommandStack.h"

namespace core {

void CommandStack::push(std::unique_ptr<Command> command) {
    command->execute();
    m_undoStack.push_back(std::move(command));
    m_redoStack.clear();
}

void CommandStack::undo() {
    if (!canUndo()) return;
    auto command = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    command->undo();
    m_redoStack.push_back(std::move(command));
}

void CommandStack::redo() {
    if (!canRedo()) return;
    auto command = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    command->execute();
    m_undoStack.push_back(std::move(command));
}

}  // namespace core
