#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "previz/PrevizTransformUtils.h"

TEST_CASE("Previz child follows its group rotation",
          "[previz][group][transform]") {
    core::PrevizScene scene;
    core::PrevizModel group;
    group.name = "Turret";
    group.filePath = ":group";
    group.transform.position = {1.0f, 0.0f, 0.0f};
    group.transform.rotationDeg.y = 90.0f;
    scene.models.push_back(group);

    core::PrevizModel barrel;
    barrel.name = "Barrel";
    barrel.filePath = ":cylinder";
    barrel.parentModel = 0;
    barrel.transform.position = {0.0f, 0.0f, -2.0f};
    scene.models.push_back(barrel);

    const QVector3D worldOrigin =
        previz::worldMatrix(scene, 1, 0).map(QVector3D());
    REQUIRE(worldOrigin.x() == Catch::Approx(-1.0f).margin(0.0001f));
    REQUIRE(worldOrigin.y() == Catch::Approx(0.0f).margin(0.0001f));
    REQUIRE(worldOrigin.z() == Catch::Approx(0.0f).margin(0.0001f));
}

TEST_CASE("Previz reparent keeps the model at its world position",
          "[previz][group][transform]") {
    core::PrevizScene scene;
    core::PrevizModel group;
    group.name = "Group";
    group.filePath = ":group";
    group.transform.position = {10.0f, 0.0f, 0.0f};
    scene.models.push_back(group);

    core::PrevizModel model;
    model.name = "Model";
    model.filePath = ":box";
    model.transform.position = {3.0f, 2.0f, -4.0f};
    scene.models.push_back(model);

    const QVector3D before =
        previz::worldMatrix(scene, 1, 0).map(QVector3D());
    REQUIRE(previz::reparentPreservingWorld(scene, 1, 0, 0));
    const QVector3D after =
        previz::worldMatrix(scene, 1, 0).map(QVector3D());

    REQUIRE(after.x() == Catch::Approx(before.x()).margin(0.0001f));
    REQUIRE(after.y() == Catch::Approx(before.y()).margin(0.0001f));
    REQUIRE(after.z() == Catch::Approx(before.z()).margin(0.0001f));
    REQUIRE(scene.models[1].transform.position.x ==
            Catch::Approx(-7.0f).margin(0.0001f));
}
