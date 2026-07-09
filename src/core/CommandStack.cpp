#include "CommandStack.h"

namespace core {

void CommandStack::push(std::unique_ptr<Command> command) {
    command->execute();
    m_undoStack.push_back(std::move(command));
    m_redoStack.clear();
}

Command* CommandStack::undo() {
    if (!canUndo()) return nullptr;
    auto command = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    command->undo();
    m_redoStack.push_back(std::move(command));
    return m_redoStack.back().get();
}

void CommandStack::clear() {
    m_undoStack.clear();
    m_redoStack.clear();
}

Command* CommandStack::redo() {
    if (!canRedo()) return nullptr;
    auto command = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    command->execute();
    m_undoStack.push_back(std::move(command));
    return m_undoStack.back().get();
}

}  // namespace core
