/*
* Copyright (c) 2020 Vsevolod Kremianskii
* Copyright (c) 2020 uwadmin12
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

#include <memory>

namespace reone {

namespace game {

constexpr static int kMaxFactionCount = 25;

class Creature;

bool getIsEnemy(const std::shared_ptr<Creature> &target, const std::shared_ptr<Creature> &source);

} // namespace game

} // namespace reone
