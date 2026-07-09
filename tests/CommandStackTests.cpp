#include <catch2/catch_test_macros.hpp>

#include "core/CommandStack.h"

namespace {

class AddCommand : public core::Command {
public:
    AddCommand(int& target, int amount) : m_target(target), m_amount(amount) {}
    void execute() override { m_target += m_amount; }
    void undo() override { m_target -= m_amount; }

private:
    int& m_target;
    int m_amount;
};

}  // namespace

TEST_CASE("CommandStack executes, undoes and redoes commands", "[core][command]") {
    int value = 0;
    core::CommandStack stack;

    stack.push(std::make_unique<AddCommand>(value, 5));
    REQUIRE(value == 5);
    REQUIRE(stack.canUndo());
    REQUIRE_FALSE(stack.canRedo());

    stack.push(std::make_unique<AddCommand>(value, 3));
    REQUIRE(value == 8);

    stack.undo();
    REQUIRE(value == 5);
    REQUIRE(stack.canRedo());

    stack.undo();
    REQUIRE(value == 0);
    REQUIRE_FALSE(stack.canUndo());

    stack.redo();
    REQUIRE(value == 5);

    stack.redo();
    REQUIRE(value == 8);
    REQUIRE_FALSE(stack.canRedo());
}

TEST_CASE("Pushing a new command after undo clears redo history", "[core][command]") {
    int value = 0;
    core::CommandStack stack;

    stack.push(std::make_unique<AddCommand>(value, 1));
    stack.push(std::make_unique<AddCommand>(value, 2));
    stack.undo();
    REQUIRE(stack.canRedo());

    stack.push(std::make_unique<AddCommand>(value, 10));
    REQUIRE_FALSE(stack.canRedo());
    REQUIRE(value == 11);
}
