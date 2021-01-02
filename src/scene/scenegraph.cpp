/*
 * Copyright (c) 2020-2021 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "scenegraph.h"

#include <algorithm>
#include <stack>

#include "../render/mesh/quad.h"

#include "node/cameranode.h"
#include "node/lightnode.h"
#include "node/modelnodescenenode.h"
#include "node/modelscenenode.h"

using namespace std;

using namespace reone::render;

namespace reone {

namespace scene {

static const float kMaxLightDistance = 16.0f;

SceneGraph::SceneGraph(const GraphicsOptions &opts) : _opts(opts) {
}

void SceneGraph::clear() {
    _roots.clear();
}

void SceneGraph::addRoot(const shared_ptr<SceneNode> &node) {
    _roots.push_back(node);
}

void SceneGraph::removeRoot(const shared_ptr<SceneNode> &node) {
    auto maybeRoot = find_if(_roots.begin(), _roots.end(), [&node](auto &n) { return n.get() == node.get(); });
    if (maybeRoot != _roots.end()) {
        _roots.erase(maybeRoot);
    }
}

void SceneGraph::build() {
}

void SceneGraph::prepareFrame() {
    if (!_activeCamera) return;

    refreshMeshesAndLights();
    refreshShadowLights();

    for (auto &root : _roots) {
        ModelSceneNode *modelNode = dynamic_cast<ModelSceneNode *>(root.get());
        if (modelNode) {
            modelNode->updateLighting();
        }
    }
    unordered_map<ModelNodeSceneNode *, float> cameraDistances;
    glm::vec3 cameraPosition(_activeCamera->absoluteTransform()[3]);

    for (auto &mesh : _transparentMeshes) {
        cameraDistances.insert(make_pair(mesh, mesh->distanceTo(cameraPosition)));
    }
    sort(_transparentMeshes.begin(), _transparentMeshes.end(), [&cameraDistances](auto &left, auto &right) {
        int leftTransparency = left->modelNode()->mesh()->transparency();
        int rightTransparency = right->modelNode()->mesh()->transparency();

        if (leftTransparency < rightTransparency) return true;
        if (leftTransparency > rightTransparency) return false;

        float leftDistance = cameraDistances.find(left)->second;
        float rightDistance = cameraDistances.find(right)->second;

        return leftDistance > rightDistance;
    });
}

void SceneGraph::refreshMeshesAndLights() {
    _opaqueMeshes.clear();
    _transparentMeshes.clear();
    _shadowMeshes.clear();
    _lights.clear();

    for (auto &root : _roots) {
        stack<SceneNode *> nodes;
        nodes.push(root.get());

        while (!nodes.empty()) {
            SceneNode *node = nodes.top();
            nodes.pop();

            ModelSceneNode *model = dynamic_cast<ModelSceneNode *>(node);
            if (model) {
                if (!model->isVisible() || !model->isOnScreen()) continue;

            } else {
                ModelNodeSceneNode *modelNode = dynamic_cast<ModelNodeSceneNode *>(node);
                if (modelNode) {
                    if (modelNode->shouldRender()) {
                        if (modelNode->isTransparent()) {
                            _transparentMeshes.push_back(modelNode);
                        } else {
                            _opaqueMeshes.push_back(modelNode);
                        }
                    }
                    if (modelNode->shouldCastShadows()) {
                        _shadowMeshes.push_back(modelNode);
                    }
                } else {
                    LightSceneNode *light = dynamic_cast<LightSceneNode *>(node);
                    if (light) {
                        _lights.push_back(light);
                    }
                }
            }
            for (auto &child : node->children()) {
                nodes.push(child.get());
            }
        }
    }
}

void SceneGraph::refreshShadowLights() {
    _shadowLights.clear();

    if (!_refNode) return;

    glm::vec3 refNodePos(_refNode->absoluteTransform()[3]);
    vector<LightSceneNode *> lights;
    getLightsAt(refNodePos, lights);

    for (auto &light : lights) {
        if (!light->shadow()) continue;
        if (_shadowLights.size() >= kMaxShadowLightCount) break;

        glm::vec3 lightPos(light->absoluteTransform()[3]);
        glm::vec3 lightToRefNode(lightPos - refNodePos);
        glm::vec3 lightDir(glm::normalize(lightToRefNode));

        float dist = glm::min(glm::length(lightToRefNode), kMaxLightDistance);
        lightPos = refNodePos + dist * lightDir;

        glm::mat4 view(glm::lookAt(lightPos, refNodePos, glm::vec3(0.0f, 0.0f, 1.0f)));

        ShadowLight shadowLight;
        shadowLight.position = glm::vec4(lightPos, 1.0f);
        shadowLight.view = view;
        shadowLight.projection = getLightProjection();
        _shadowLights.push_back(move(shadowLight));
    }
}

void SceneGraph::render() const {
    if (!_activeCamera) return;

    GlobalUniforms globals;
    globals.projection = _activeCamera->projection();
    globals.view = _activeCamera->view();
    globals.cameraPosition = _activeCamera->absoluteTransform()[3];

    int lightCount = static_cast<int>(_shadowLights.size());
    globals.shadows.shadowLightCount = lightCount;
    for (int i = 0; i < lightCount; ++i) {
        ShadowLight &light = globals.shadows.shadowLights[i];
        light = _shadowLights[i];
    }

    Shaders::instance().setGlobalUniforms(globals);

    renderNoGlobalUniforms(false);
}

void SceneGraph::renderNoGlobalUniforms(bool shadowPass) const {
    if (shadowPass) {
        for (auto &mesh : _shadowMeshes) {
            mesh->renderSingle(true);
        }
        return;
    }
    for (auto &root : _roots) {
        root->render();
    }
    for (auto &mesh : _opaqueMeshes) {
        mesh->renderSingle(false);
    }
    for (auto &mesh : _transparentMeshes) {
        mesh->renderSingle(false);
    }
}

const vector<ShadowLight> &SceneGraph::shadowLights() const {
    return _shadowLights;
}

void SceneGraph::getLightsAt(const glm::vec3 &position, vector<LightSceneNode *> &lights) const {
    unordered_map<LightSceneNode *, float> distances;
    lights.clear();

    for (auto &light : _lights) {
        float distance = light->distanceTo(position);
        float radius = light->radius();
        if (distance > radius) continue;

        distances.insert(make_pair(light, distance));
        lights.push_back(light);
    }

    sort(lights.begin(), lights.end(), [&distances](LightSceneNode *left, LightSceneNode *right) {
        int leftPriority = left->priority();
        int rightPriority = right->priority();

        if (leftPriority < rightPriority) return true;
        if (leftPriority > rightPriority) return false;

        float leftDistance = distances.find(left)->second;
        float rightDistance = distances.find(right)->second;

        return leftDistance < rightDistance;
    });

    if (lights.size() > kMaxLightCount) {
        lights.erase(lights.begin() + kMaxLightCount, lights.end());
    }
}

glm::mat4 SceneGraph::getLightProjection() const {
    return glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 1000.0f);
}

const glm::vec3 &SceneGraph::ambientLightColor() const {
    return _ambientLightColor;
}

void SceneGraph::setActiveCamera(const shared_ptr<CameraSceneNode> &camera) {
    _activeCamera = camera;
}

void SceneGraph::setReferenceNode(const shared_ptr<SceneNode> &node) {
    _refNode = node;
}

void SceneGraph::setAmbientLightColor(const glm::vec3 &color) {
    _ambientLightColor = color;
}

} // namespace scene

} // namespace reone
