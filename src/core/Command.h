#pragma once

namespace core {

// 単一の可逆操作を表すコマンド。UI/Core双方の操作はこれを介してUndo/Redoスタックに積む。
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
};

}  // namespace core
