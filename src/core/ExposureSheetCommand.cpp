#include "ExposureSheetCommand.h"

#include <utility>

#include "Cel.h"

namespace core {

ExposureSheetCommand::ExposureSheetCommand(std::vector<ExposureChange> changes) : m_changes(std::move(changes)) {}

void ExposureSheetCommand::execute() {
    for (const ExposureChange& change : m_changes) {
        if (change.cel) change.cel->setExposure(change.frame, change.after);
    }
}

void ExposureSheetCommand::undo() {
    for (const ExposureChange& change : m_changes) {
        if (change.cel) change.cel->setExposure(change.frame, change.before);
    }
}

}  // namespace core
