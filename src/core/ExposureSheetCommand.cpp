#include "ExposureSheetCommand.h"

#include <utility>

#include "Cel.h"

namespace core {

ExposureSheetCommand::ExposureSheetCommand(std::vector<ExposureChange> changes,
                                           std::vector<ActionChange> actionChanges)
    : m_changes(std::move(changes)), m_actionChanges(std::move(actionChanges)) {}

void ExposureSheetCommand::execute() {
    for (const ExposureChange& change : m_changes) {
        if (change.cel) change.cel->setExposure(change.frame, change.after);
    }
    for (const ActionChange& change : m_actionChanges) {
        if (change.cel) change.cel->setActionEntry(change.frame, change.after);
    }
}

void ExposureSheetCommand::undo() {
    for (const ExposureChange& change : m_changes) {
        if (change.cel) change.cel->setExposure(change.frame, change.before);
    }
    for (const ActionChange& change : m_actionChanges) {
        if (change.cel) change.cel->setActionEntry(change.frame, change.before);
    }
}

}  // namespace core
