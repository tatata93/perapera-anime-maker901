#include <catch2/catch_test_macros.hpp>

#include "core/Cel.h"
#include "core/CommandStack.h"
#include "core/ExposureSheetCommand.h"

TEST_CASE("ExposureSheetCommand edits a range as one undo step", "[core][sheet][command]") {
    core::Cel celA("A");
    core::Cel celB("B");
    celA.resizeExposure(4);
    celB.resizeExposure(4);
    celA.setExposure(1, 0);

    std::vector<core::ExposureChange> changes{
        {&celA, 1, 0, 2},
        {&celA, 2, -1, 2},
        {&celB, 2, -1, 1},
    };

    core::CommandStack stack;
    stack.push(std::make_unique<core::ExposureSheetCommand>(changes));
    REQUIRE(celA.exposure(1) == 2);
    REQUIRE(celA.exposure(2) == 2);
    REQUIRE(celB.exposure(2) == 1);

    stack.undo();
    REQUIRE(celA.exposure(1) == 0);
    REQUIRE(celA.exposure(2) == -1);
    REQUIRE(celB.exposure(2) == -1);

    stack.redo();
    REQUIRE(celA.exposure(1) == 2);
    REQUIRE(celA.exposure(2) == 2);
    REQUIRE(celB.exposure(2) == 1);
}

TEST_CASE("ExposureSheetCommand keeps ACTION and CELL in one undo step", "[core][sheet][command]") {
    core::Cel cel("A");
    cel.resizeExposure(4);

    std::vector<core::ExposureChange> exposureChanges{{&cel, 2, -1, 1}};
    std::vector<core::ActionChange> actionChanges{{&cel, 2, std::string(), "○"}};

    core::CommandStack stack;
    stack.push(std::make_unique<core::ExposureSheetCommand>(exposureChanges, actionChanges));
    REQUIRE(cel.exposure(2) == 1);
    REQUIRE(cel.actionEntry(2) == "○");

    stack.undo();
    REQUIRE(cel.exposure(2) == -1);
    REQUIRE(cel.actionEntry(2).empty());

    stack.redo();
    REQUIRE(cel.exposure(2) == 1);
    REQUIRE(cel.actionEntry(2) == "○");
}
