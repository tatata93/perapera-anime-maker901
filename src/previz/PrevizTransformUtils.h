#pragma once

#include <QMatrix3x3>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

#include "core/Previz.h"

namespace previz {

inline QMatrix4x4 matrixFromTransform(const core::PrevizTransform& transform) {
    QMatrix4x4 matrix;
    matrix.translate(transform.position.x, transform.position.y, transform.position.z);
    matrix.rotate(transform.rotationDeg.y, 0, 1, 0);
    matrix.rotate(transform.rotationDeg.x, 1, 0, 0);
    matrix.rotate(transform.rotationDeg.z, 0, 0, 1);
    matrix.scale(transform.scale.x, transform.scale.y, transform.scale.z);
    return matrix;
}

inline core::PrevizTransform transformFromMatrix(const QMatrix4x4& matrix) {
    const QVector3D position = matrix.column(3).toVector3D();
    QVector3D axisX = matrix.column(0).toVector3D();
    QVector3D axisY = matrix.column(1).toVector3D();
    QVector3D axisZ = matrix.column(2).toVector3D();
    const float scaleX = std::max(0.000001f, axisX.length());
    const float scaleY = std::max(0.000001f, axisY.length());
    const float scaleZ = std::max(0.000001f, axisZ.length());
    axisX /= scaleX;
    axisY /= scaleY;
    axisZ /= scaleZ;

    QMatrix3x3 rotationMatrix;
    rotationMatrix(0, 0) = axisX.x();
    rotationMatrix(1, 0) = axisX.y();
    rotationMatrix(2, 0) = axisX.z();
    rotationMatrix(0, 1) = axisY.x();
    rotationMatrix(1, 1) = axisY.y();
    rotationMatrix(2, 1) = axisY.z();
    rotationMatrix(0, 2) = axisZ.x();
    rotationMatrix(1, 2) = axisZ.y();
    rotationMatrix(2, 2) = axisZ.z();
    const QVector3D rotation =
        QQuaternion::fromRotationMatrix(rotationMatrix).toEulerAngles();

    return {{position.x(), position.y(), position.z()},
            {rotation.x(), rotation.y(), rotation.z()},
            {scaleX, scaleY, scaleZ}};
}

inline QMatrix4x4 worldMatrix(const core::PrevizScene& scene, size_t modelIndex,
                              size_t frame) {
    if (modelIndex >= scene.models.size()) return {};

    QMatrix4x4 matrix =
        matrixFromTransform(scene.models[modelIndex].transformAt(frame));
    std::vector<bool> visited(scene.models.size(), false);
    visited[modelIndex] = true;
    int parent = scene.models[modelIndex].parentModel;
    while (parent >= 0 && parent < static_cast<int>(scene.models.size())) {
        const size_t parentIndex = static_cast<size_t>(parent);
        if (visited[parentIndex]) break;
        visited[parentIndex] = true;
        matrix = matrixFromTransform(scene.models[parentIndex].transformAt(frame)) *
                 matrix;
        parent = scene.models[parentIndex].parentModel;
    }
    return matrix;
}

inline core::PrevizTransform worldTransform(const core::PrevizScene& scene,
                                            size_t modelIndex, size_t frame) {
    return transformFromMatrix(worldMatrix(scene, modelIndex, frame));
}

inline core::PrevizTransform localTransformForWorld(
    const core::PrevizScene& scene, size_t modelIndex, size_t frame,
    const QMatrix4x4& desiredWorld) {
    if (modelIndex >= scene.models.size()) return {};
    const int parent = scene.models[modelIndex].parentModel;
    if (parent < 0 || parent >= static_cast<int>(scene.models.size())) {
        return transformFromMatrix(desiredWorld);
    }
    bool invertible = false;
    const QMatrix4x4 parentInverse =
        worldMatrix(scene, static_cast<size_t>(parent), frame).inverted(&invertible);
    return transformFromMatrix(invertible ? parentInverse * desiredWorld
                                          : desiredWorld);
}

inline void writeTransform(core::PrevizModel& model, size_t frame,
                           const core::PrevizTransform& transform) {
    if (model.transformKeys.empty()) {
        model.transform = transform;
    } else {
        model.transformKeys[frame] = transform;
    }
}

inline std::set<size_t> hierarchyKeyFrames(const core::PrevizScene& scene,
                                           size_t modelIndex) {
    std::set<size_t> frames{0};
    if (modelIndex >= scene.models.size()) return frames;
    std::vector<bool> visited(scene.models.size(), false);
    int current = static_cast<int>(modelIndex);
    while (current >= 0 && current < static_cast<int>(scene.models.size())) {
        const size_t index = static_cast<size_t>(current);
        if (visited[index]) break;
        visited[index] = true;
        for (const auto& [frame, transform] : scene.models[index].transformKeys) {
            static_cast<void>(transform);
            frames.insert(frame);
        }
        current = scene.models[index].parentModel;
    }
    return frames;
}

inline bool reparentPreservingWorld(core::PrevizScene& scene, size_t modelIndex,
                                    int newParent, size_t currentFrame) {
    if (!scene.canSetParent(modelIndex, newParent)) return false;
    if (scene.models[modelIndex].parentModel == newParent) return true;

    std::set<size_t> frames = hierarchyKeyFrames(scene, modelIndex);
    if (newParent >= 0) {
        const std::set<size_t> parentFrames =
            hierarchyKeyFrames(scene, static_cast<size_t>(newParent));
        frames.insert(parentFrames.begin(), parentFrames.end());
    }
    if (frames.size() > 1) frames.insert(currentFrame);

    std::vector<std::pair<size_t, QMatrix4x4>> worldFrames;
    worldFrames.reserve(frames.size());
    for (const size_t frame : frames) {
        worldFrames.emplace_back(frame, worldMatrix(scene, modelIndex, frame));
    }

    const bool hadAnimation = !scene.models[modelIndex].transformKeys.empty();
    scene.models[modelIndex].parentModel = newParent;
    scene.models[modelIndex].transformKeys.clear();
    scene.models[modelIndex].transform =
        localTransformForWorld(scene, modelIndex, worldFrames.front().first,
                               worldFrames.front().second);

    if (hadAnimation || worldFrames.size() > 1) {
        for (const auto& [frame, world] : worldFrames) {
            scene.models[modelIndex].transformKeys[frame] =
                localTransformForWorld(scene, modelIndex, frame, world);
        }
    }
    return true;
}

}  // namespace previz
