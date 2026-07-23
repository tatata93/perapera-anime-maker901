#pragma once

#include <cstddef>
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

// 複数セル・複数コマの割付変更を1回のUndo/Redoとして扱う。
class ExposureSheetCommand final : public Command {
public:
    explicit ExposureSheetCommand(std::vector<ExposureChange> changes);

    void execute() override;
    void undo() override;

    const std::vector<ExposureChange>& changes() const { return m_changes; }

private:
    std::vector<ExposureChange> m_changes;
};

}  // namespace core
