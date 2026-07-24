#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Command.h"

namespace core {

class Cel;

struct ExposureChange {
    Cel* cel = nullptr;
    size_t frame = 0;
    int before = -1;
    int after = -1;
};

struct ActionChange {
    Cel* cel = nullptr;
    size_t frame = 0;
    std::string before;
    std::string after;
};

// 複数セル・複数コマの割付変更を1回のUndo/Redoとして扱う。
class ExposureSheetCommand final : public Command {
public:
    explicit ExposureSheetCommand(std::vector<ExposureChange> changes,
                                  std::vector<ActionChange> actionChanges = {});

    void execute() override;
    void undo() override;

    const std::vector<ExposureChange>& changes() const { return m_changes; }
    const std::vector<ActionChange>& actionChanges() const { return m_actionChanges; }

private:
    std::vector<ExposureChange> m_changes;
    std::vector<ActionChange> m_actionChanges;
};

}  // namespace core
