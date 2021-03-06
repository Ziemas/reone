/*
 * Copyright (c) 2020 Vsevolod Kremianskii
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

#include "mdlfile.h"

#include <boost/algorithm/string.hpp>

#include "glm/ext.hpp"

#include "../../common/log.h"
#include "../../resource/resources.h"

#include "../models.h"
#include "../textures.h"

using namespace std;

using namespace reone::resource;

namespace fs = boost::filesystem;

namespace reone {

namespace render {

static const int kSignatureSize = 4;
static const char kSignature[] = "\0\0\0\0";
static const int kMdlDataOffset = 12;

enum {
    kNodeHasHeader = 1,
    kNodeHasLight = 2,
    kNodeHasEmitter = 4,
    kNodeHasReference = 16,
    kNodeHasMesh = 32,
    kNodeHasSkin = 64,
    kNodeHasAnim = 128,
    kNodeHasDangly = 256,
    kNodeHasAABB = 512,
    kNodeHasSaber = 2048
};

enum class ControllerType {
    Position = 8,
    Orientation = 20,
    Color = 76,
    Radius = 88,
    SelfIllumColor = 100,
    Alpha = 132,
    Multiplier = 140
};

MdlFile::MdlFile(GameVersion version) : BinaryFile(kSignatureSize, kSignature), _version(version) {
}

void MdlFile::load(const shared_ptr<istream> &mdl, const shared_ptr<istream> &mdx) {
    _mdx = mdx;
    _mdxReader = make_unique<StreamReader>(mdx);

    BinaryFile::load(mdl);
}

void MdlFile::doLoad() {
    if (!_mdx) openMDX();

    uint32_t mdlDataSize = readUint32();
    uint32_t mdxSize = readUint32();

    ignore(8);

    _name = readCString(32);
    boost::to_lower(_name);

    uint32_t rootNodeOffset = readUint32();
    uint32_t nodeCount = readUint32();

    ignore(28);

    uint8_t type = readByte();

    ignore(3);

    uint8_t classification = readByte();

    ignore(2);

    uint8_t fogged = readByte();

    ignore(4);

    uint32_t animOffOffset, animCount;
    readArrayDefinition(animOffOffset, animCount);
    vector<uint32_t> animOffsets(readArray<uint32_t>(kMdlDataOffset + animOffOffset, animCount));

    ignore(28);

    float radius = readFloat();
    float scale = readFloat();

    string superModelName(readCString(32));
    boost::to_lower(superModelName);

    ignore(16);

    uint32_t nameOffOffset, nameCount;
    readArrayDefinition(nameOffOffset, nameCount);
    vector<uint32_t> nameOffsets(readArray<uint32_t>(kMdlDataOffset + nameOffOffset, nameCount));
    readNodeNames(nameOffsets);

    unique_ptr<ModelNode> rootNode(readNode(kMdlDataOffset + rootNodeOffset, nullptr));
    vector<unique_ptr<Animation>> anims(readAnimations(animOffsets));
    shared_ptr<Model> superModel;

    if (!superModelName.empty() && superModelName != "null") {
        superModel = Models::instance().get(superModelName);
    }

    _model = make_unique<Model>(_name, move(rootNode), anims, superModel);
    _model->setClassification(getClassification(classification));
    _model->setAnimationScale(scale);
}

void MdlFile::openMDX() {
    fs::path mdxPath(_path);
    mdxPath.replace_extension(".mdx");

    if (!fs::exists(mdxPath)) {
        throw runtime_error("MDL: MDX file not found: " + mdxPath.string());
    }
    _mdx.reset(new fs::ifstream(mdxPath, ios::binary));
    _mdxReader = make_unique<StreamReader>(_mdx);
}

void MdlFile::readArrayDefinition(uint32_t &offset, uint32_t &count) {
    offset = readUint32();
    count = readUint32();
    ignore(4);
}

void MdlFile::readNodeNames(const vector<uint32_t> &offsets) {
    map<string, int> nameHits;

    for (uint32_t offset : offsets) {
        string name(readCStringAt(kMdlDataOffset + offset));
        boost::to_lower(name);

        int hitCount = nameHits[name]++;
        if (hitCount > 0) {
            warn(boost::format("MDL: duplicate node name: %s, model %s") % name % _name);
            name = str(boost::format("%s_dup%d") % name % hitCount);
        }
        _nodeNames.push_back(move(name));
    }
}

unique_ptr<ModelNode> MdlFile::readNode(uint32_t offset, ModelNode *parent) {
    size_t pos = tell();
    seek(offset);

    uint16_t flags = readUint16();
    if (flags & 0xf408) {
        throw runtime_error("MDL: unsupported node flags: " + to_string(flags));
    }

    ignore(2);

    uint16_t nodeNumber = readUint16();
    string name(_nodeNames[nodeNumber]);

    ignore(10);

    vector<float> positionValues(readArray<float>(3));
    glm::vec3 position(glm::make_vec3(&positionValues[0]));

    vector<float> orientationValues(readArray<float>(4));
    glm::quat orientation(orientationValues[0], orientationValues[1], orientationValues[2], orientationValues[3]);

    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, position);
    transform *= glm::mat4_cast(orientation);

    glm::mat4 absTransform(parent ? parent->_absTransform : glm::mat4(1.0f));
    absTransform *= transform;

    uint32_t childOffOffset, childCount;
    readArrayDefinition(childOffOffset, childCount);
    vector<uint32_t> childOffsets(readArray<uint32_t>(kMdlDataOffset + childOffOffset, childCount));

    uint32_t controllerKeyOffset, controllerKeyCount;
    readArrayDefinition(controllerKeyOffset, controllerKeyCount);

    uint32_t controllerDataOffset, controllerDataCount;
    readArrayDefinition(controllerDataOffset, controllerDataCount);
    vector<float> controllerData(readArray<float>(kMdlDataOffset + controllerDataOffset, controllerDataCount));

    unique_ptr<ModelNode> node(new ModelNode(_nodeIndex++, parent));
    node->_flags = flags;
    node->_nodeNumber = nodeNumber;
    node->_name = name;
    node->_position = position;
    node->_orientation = orientation;
    node->_localTransform = transform;
    node->_absTransform = absTransform;
    node->_absTransformInv = glm::inverse(absTransform);

    readControllers(controllerKeyCount, controllerKeyOffset, controllerData, *node);

    if (flags & kNodeHasLight) {
        readLight(*node);
    }
    if (flags & kNodeHasEmitter) {
        ignore(216);
    }
    if (flags & kNodeHasReference) {
        ignore(68);
    }
    if (flags & kNodeHasMesh) {
        node->_mesh = readMesh();
    }
    if (flags & kNodeHasSkin) {
        readSkin(*node);
    }

    for (auto offset : childOffsets) {
        unique_ptr<ModelNode> child(readNode(kMdlDataOffset + offset, node.get()));
        node->_children.push_back(move(child));
    }

    seek(pos);

    return move(node);
}

void MdlFile::readControllers(uint32_t keyCount, uint32_t keyOffset, const vector<float> &data, ModelNode &node) {
    size_t pos = tell();
    seek(kMdlDataOffset + keyOffset);

    for (uint32_t i = 0; i < keyCount; ++i) {
        ControllerType type = static_cast<ControllerType>(readUint32());

        ignore(2);

        uint16_t rowCount = readUint16();
        uint16_t timeIndex = readUint16();
        uint16_t dataIndex = readUint16();
        uint8_t columnCount = readByte();

        ignore(3);

        switch (type) {
            case ControllerType::Position:
                readPositionController(rowCount, columnCount, timeIndex, dataIndex, data, node);
                break;
            case ControllerType::Orientation:
                readOrientationController(rowCount, columnCount, timeIndex, dataIndex, data, node);
                break;
            case ControllerType::Color:
                // TODO: multiple rows
                readColorController(dataIndex, data, node);
                break;
            case ControllerType::SelfIllumColor:
                if (node._flags & kNodeHasMesh) {
                    // TODO: multiple rows
                    node._selfIllumEnabled = true;
                    readSelfIllumColorController(dataIndex, data, node);
                }
                break;
            case ControllerType::Alpha:
                // TODO: multiple rows
                readAlphaController(dataIndex, data, node);
                break;
            case ControllerType::Radius:
                // TODO: multiple rows
                readRadiusController(dataIndex, data, node);
                break;
            case ControllerType::Multiplier:
                // TODO: multiple rows
                readMultiplierController(dataIndex, data, node);
                break;
            default:
                debug(boost::format("MDL: unsupported controller type: \"%s\" %d") % _name % static_cast<int>(type), 3);
                break;
        }
    }

    seek(pos);
}

void MdlFile::readPositionController(uint16_t rowCount, uint8_t columnCount, uint16_t timeIndex, uint16_t dataIndex, const vector<float> &data, ModelNode &node) {
    bool bezier = columnCount & 16;
    node._positionFrames.reserve(rowCount);

    switch (columnCount) {
        case 3:
        case 19:
            for (int i = 0; i < rowCount; ++i) {
                int rowTimeIdx = timeIndex + i;
                int rowDataIdx = dataIndex + i * (bezier ? 9 : 3);

                ModelNode::PositionKeyframe frame;
                frame.time = data[rowTimeIdx];
                frame.position = glm::make_vec3(&data[rowDataIdx]);

                node._positionFrames.push_back(move(frame));
            }
            break;
        default:
            break;
    }
}

void MdlFile::readOrientationController(uint16_t rowCount, uint8_t columnCount, uint16_t timeIndex, uint16_t dataIndex, const vector<float> &data, ModelNode &node) {
    node._orientationFrames.reserve(rowCount);

    switch (columnCount) {
        case 2:
            for (int i = 0; i < rowCount; ++i) {
                int rowTimeIdx = timeIndex + i;
                int rowDataIdx = dataIndex + i;

                uint32_t temp = *reinterpret_cast<const uint32_t *>(&data[rowDataIdx]);
                float x = 1.0f - static_cast<float>(temp & 0x7ff) / 1023.0f;
                float y = 1.0f - static_cast<float>((temp >> 11) & 0x7ff) / 1023.0f;
                float z = 1.0f - static_cast<float>(temp >> 22) / 511.0f;
                float dot = x * x + y * y + z * z;
                float w;

                if (dot >= 1.0f) {
                    float len = glm::sqrt(dot);
                    x /= len;
                    y /= len;
                    z /= len;
                    w = 0.0f;
                } else {
                    w = -glm::sqrt(1.0f - dot);
                }

                ModelNode::OrientationKeyframe frame;
                frame.time = data[rowTimeIdx];
                frame.orientation = glm::quat(w, x, y, z);

                node._orientationFrames.push_back(move(frame));
            }
            break;

        case 4:
            for (int i = 0; i < rowCount; ++i) {
                int rowTimeIdx = timeIndex + i;
                int rowDataIdx = dataIndex + i * 4;

                ModelNode::OrientationKeyframe frame;
                frame.time = data[rowTimeIdx];
                frame.orientation.x = data[rowDataIdx + 0];
                frame.orientation.y = data[rowDataIdx + 1];
                frame.orientation.z = data[rowDataIdx + 2];
                frame.orientation.w = data[rowDataIdx + 3];

                node._orientationFrames.push_back(move(frame));
            }
            break;
        default:
            break;
    }
}

void MdlFile::readColorController(uint16_t dataIndex, const vector<float> &data, ModelNode &node) {
    node._color.r = data[dataIndex + 0];
    node._color.g = data[dataIndex + 1];
    node._color.b = data[dataIndex + 2];
}

void MdlFile::readSelfIllumColorController(uint16_t dataIndex, const vector<float> &data, ModelNode &node) {
    node._selfIllumColor.r = data[dataIndex + 0];
    node._selfIllumColor.g = data[dataIndex + 1];
    node._selfIllumColor.b = data[dataIndex + 2];
}

void MdlFile::readAlphaController(uint16_t dataIndex, const vector<float> &data, ModelNode &node) {
    node._alpha = data[dataIndex];
}

void MdlFile::readRadiusController(uint16_t dataIndex, const vector<float> &data, ModelNode &node) {
    node._radius = data[dataIndex];
}

void MdlFile::readMultiplierController(uint16_t dataIndex, const vector<float> &data, ModelNode &node) {
    node._multiplier = data[dataIndex];
}

void MdlFile::readLight(ModelNode &node) {
    node._light = make_shared<ModelNode::Light>();

    ignore(64);

    node._light->priority = readInt32();
    node._light->ambientOnly = static_cast<bool>(readInt32());

    ignore(4);

    node._light->affectDynamic = static_cast<bool>(readInt32());

    ignore(12);
}

unique_ptr<ModelMesh> MdlFile::readMesh() {
    ignore(8);

    uint32_t faceOffset, faceCount;
    readArrayDefinition(faceOffset, faceCount);

    ignore(64);

    uint32_t transparency = readUint32();

    string diffuse(readCString(32));
    boost::to_lower(diffuse);

    string lightmap(readCString(32));
    boost::to_lower(lightmap);

    ignore(36);

    uint32_t indexOffOffset, indexCount;
    readArrayDefinition(indexOffOffset, indexCount);

    ignore(52);

    uint32_t mdxVertexSize = readUint32();

    ignore(4);

    uint32_t mdxVerticesOffset = readUint32();
    uint32_t mdxNormalsOffset = readUint32();

    ignore(4);

    uint32_t mdxTextureOffset = readUint32();
    uint32_t mdxLightmapOffset = readUint32();

    ignore(24);

    uint16_t vertexCount = readUint16();
    uint16_t textureCount = readUint16();

    ignore(5);

    uint8_t render = readByte();

    ignore(10);

    if (_version == GameVersion::TheSithLords) ignore(8);

    uint32_t mdxDataOffset = readUint32();

    ignore(4);

    size_t endPos = tell();

    if (faceCount == 0) {
        warn(boost::format("MDL: invalid face count: %d, model %s") % to_string(faceCount) % _name);
        return nullptr;
    }
    if (mdxVertexSize == 0) {
        warn(boost::format("MDL: invalid MDX vertex size: %d, model %s") % to_string(mdxVertexSize) % _name);
        return nullptr;
    }

    seek(kMdlDataOffset + indexOffOffset);
    uint32_t indexOffset = readUint32();
    seek(kMdlDataOffset + indexOffset);
    vector<uint16_t> indices(readArray<uint16_t>(3 * faceCount));

    seek(endPos);

    int valPerVert = mdxVertexSize / sizeof(float);
    int vertValCount = valPerVert * vertexCount;
    _mdxReader->seek(mdxDataOffset);
    vector<float> vertices(_mdxReader->getArray<float>(vertValCount));

    Mesh::VertexOffsets offsets;
    offsets.vertexCoords = mdxVerticesOffset;
    offsets.normals = mdxNormalsOffset != 0xffff ? mdxNormalsOffset : -1;
    offsets.texCoords1 = mdxTextureOffset != 0xffff ? mdxTextureOffset : -1;
    offsets.texCoords2 = mdxLightmapOffset != 0xffff ? mdxLightmapOffset : -1;
    offsets.stride = mdxVertexSize;

    unique_ptr<ModelMesh> mesh(new ModelMesh(render, transparency));
    mesh->_vertices = move(vertices);
    mesh->_indices = move(indices);
    mesh->_offsets = move(offsets);
    mesh->computeAABB();

    if (!diffuse.empty() && diffuse != "null") {
        mesh->_diffuse = Textures::instance().get(diffuse, TextureType::Diffuse);
        if (mesh->_diffuse) {
            const TextureFeatures &features = mesh->_diffuse->features();
            if (!features.envMapTexture.empty()) {
                mesh->_envmap = Textures::instance().get(features.envMapTexture, TextureType::EnvironmentMap);
            }
            if (!features.bumpyShinyTexture.empty()) {
                mesh->_bumpyShiny = Textures::instance().get(features.bumpyShinyTexture, TextureType::EnvironmentMap);
            }
            if (!features.bumpMapTexture.empty()) {
                mesh->_bumpmap = Textures::instance().get(features.bumpMapTexture, TextureType::Bumpmap);
            }
        }
    }
    if (!lightmap.empty()) {
        mesh->_lightmap = Textures::instance().get(lightmap, TextureType::Lightmap);
    }

    return move(mesh);
}

void MdlFile::readSkin(ModelNode &node) {
    ignore(12);

    uint32_t boneWeightsOffset = readUint32();
    uint32_t boneIndicesOffset = readUint32();
    uint32_t bonesOffset = readUint32();
    uint32_t boneCount = readUint32();

    node._mesh->_offsets.boneWeights = boneWeightsOffset;
    node._mesh->_offsets.boneIndices = boneIndicesOffset;

    unordered_map<uint16_t, uint16_t> nodeIdxByBoneIdx;
    seek(kMdlDataOffset + bonesOffset);

    for (uint32_t i = 0; i < boneCount; ++i) {
        uint16_t boneIdx = static_cast<uint16_t>(readFloat());
        if (boneIdx == 0xffff) continue;

        uint16_t nodeIdx = i;
        nodeIdxByBoneIdx.insert(make_pair(boneIdx, nodeIdx));
    }

    node._skin = make_unique<ModelNode::Skin>();
    node._skin->nodeIdxByBoneIdx = move(nodeIdxByBoneIdx);
}

vector<unique_ptr<Animation>> MdlFile::readAnimations(const vector<uint32_t> &offsets) {
    vector<unique_ptr<Animation>> anims;
    anims.reserve(offsets.size());

    for (uint32_t offset : offsets) {
        anims.push_back(readAnimation(offset));
    }

    return move(anims);
}

unique_ptr<Animation> MdlFile::readAnimation(uint32_t offset) {
    seek(kMdlDataOffset + offset);
    ignore(8);

    string name(readCString(32));
    boost::to_lower(name);

    uint32_t rootNodeOffset = readUint32();

    ignore(36);

    float length = readFloat();
    float transitionTime = readFloat();

    ignore(48);

    _nodeIndex = 0;
    unique_ptr<ModelNode> rootNode(readNode(kMdlDataOffset + rootNodeOffset, nullptr));

    return make_unique<Animation>(name, length, transitionTime, move(rootNode));
}

Model::Classification MdlFile::getClassification(int value) const {
    switch (value) {
        case 1:
            return Model::Classification::Effect;
        case 2:
            return Model::Classification::Tile;
        case 4:
            return Model::Classification::Character;
        case 8:
            return Model::Classification::Door;
        case 0x10:
            return Model::Classification::Lightsaber;
        case 0x20:
            return Model::Classification::Placeable;
        case 0x40:
            return Model::Classification::Flyer;
        default:
            return Model::Classification::Other;
    }
}

shared_ptr<Model> MdlFile::model() const {
    return _model;
}

} // namespace render

} // namespace reone
