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

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "../actionqueue.h"

#include "types.h"

namespace reone {

namespace game {

class Object {
public:
    virtual ~Object() = default;

    virtual void update(float dt);

    void runUserDefinedEvent(int eventNumber);

    uint32_t id() const;
    ObjectType type() const;
    const std::string &tag() const;
    const std::string &title() const;
    virtual std::string conversation() const;
    ActionQueue &actionQueue();

protected:
    uint32_t _id { 0 };
    ObjectType _type { ObjectType::None };
    std::string _tag;
    std::string _title;
    ActionQueue _actionQueue;
    std::string _onUserDefined;

    Object(uint32_t id, ObjectType type);

private:
    Object(const Object &) = delete;
    Object &operator=(const Object &) = delete;
};

} // namespace game

} // namespace reone
