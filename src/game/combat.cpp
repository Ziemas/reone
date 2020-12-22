/*
 * Copyright (c) 2020 The reone project contributors
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

#include "combat.h"

#include <algorithm>
#include <climits>

#include "glm/common.hpp"

#include "../common/log.h"
#include "../common/random.h"

#include "game.h"
#include "rp/factionutil.h"

using namespace std;

namespace reone {

namespace game {

constexpr float kRoundDuration = 3.0f;

static AttackAction *getAttackAction(const shared_ptr<Creature> &combatant) {
    return dynamic_cast<AttackAction *>(combatant->actionQueue().currentAction());
}

void Combat::Round::advance(float dt) {
    time = glm::min(time + dt, kRoundDuration);
}

Combat::Combat(Game *game) : _game(game) {
    if (!game) {
        throw invalid_argument("game must not be null");
    }
}

void Combat::update(float dt) {
    _heartbeatTimer.update(dt);

    if (_heartbeatTimer.hasTimedOut()) {
        _heartbeatTimer.reset(kHeartbeatInterval);
        updateCombatants();
        updateAI();
    }
    updateRounds(dt);
    updateActivation();
}

void Combat::updateCombatants() {
    for (auto &object : _game->module()->area()->getObjectsByType(ObjectType::Creature)) {
        auto creature = static_pointer_cast<Creature>(object);
        if (!creature->isDead()) {
            updateCombatant(creature);
        }
    }
    removeStaleCombatants();
}

void Combat::updateCombatant(const shared_ptr<Creature> &creature) {
    vector<shared_ptr<Creature>> enemies(getEnemies(*creature));

    auto maybeCombatant = _combatantById.find(creature->id());
    if (maybeCombatant != _combatantById.end()) {
        maybeCombatant->second->enemies = move(enemies);
        return;
    }

    if (!enemies.empty()) {
        addCombatant(creature, move(enemies));
    }
}

vector<shared_ptr<Creature>> Combat::getEnemies(const Creature &combatant, float range) const {
    vector<shared_ptr<Creature>> result;
    shared_ptr<Area> area(_game->module()->area());

    for (auto &object : area->getObjectsByType(ObjectType::Creature)) {
        if (object.get() == &combatant ||
            object->isDead() ||
            object->distanceTo(combatant) > range) continue;

        shared_ptr<Creature> creature(static_pointer_cast<Creature>(object));
        if (!getIsEnemy(combatant, *creature)) continue;

        glm::vec3 adjustedCombatantPos(combatant.position());
        adjustedCombatantPos.z += 1.8f; // TODO: height based on appearance

        glm::vec3 adjustedCreaturePos(creature->position());
        adjustedCreaturePos.z += creature->model()->aabb().center().z;

        glm::vec3 combatantToCreature(adjustedCreaturePos - adjustedCombatantPos);

        RaycastProperties castProps;
        castProps.flags = kRaycastRooms | kRaycastObjects | kRaycastAABB;
        castProps.objectTypes = { ObjectType::Door };
        castProps.origin = adjustedCombatantPos;
        castProps.direction = glm::normalize(combatantToCreature);
        castProps.maxDistance = glm::length(combatantToCreature);

        RaycastResult castResult;

        const CollisionDetector &detector = area->collisionDetector();
        if (detector.raycast(castProps, castResult)) continue;

        // TODO: check field of view

        result.push_back(move(creature));
    }

    return move(result);
}

void Combat::addCombatant(const shared_ptr<Creature> &creature, EnemiesList enemies) {
    debug(boost::format("Combat: add combatant '%s' with %d enemies") % creature->tag() % enemies.size(), 2);

    creature->setInCombat(true);

    auto combatant = make_shared<Combatant>();
    combatant->creature = creature;
    combatant->enemies = move(enemies);

    _combatantById.insert(make_pair(creature->id(), move(combatant)));
}

void Combat::removeStaleCombatants() {
    for (auto it = _combatantById.begin(); it != _combatantById.end(); ) {
        shared_ptr<Combatant> combatantPtr(it->second);
        bool isStale = combatantPtr->enemies.empty() || combatantPtr->creature->isDead();
        if (isStale) {
            combatantPtr->creature->setInCombat(false);
            it = _combatantById.erase(it);
        } else {
            ++it;
        }
    }
}

void Combat::updateAI() {
    for (auto &pair : _combatantById) {
        updateCombatantAI(*pair.second);
    }
}

void Combat::updateCombatantAI(Combatant &combatant) {
    shared_ptr<Creature> creature(combatant.creature);

    shared_ptr<Creature> enemy(getNearestEnemy(combatant));
    if (!enemy) return;

    AttackAction *action = getAttackAction(creature);
    if (action && action->target() == enemy) return;

    ActionQueue &actions = creature->actionQueue();
    actions.clear();
    actions.add(make_unique<AttackAction>(enemy, creature->getAttackRange()));

    debug(boost::format("Combat: attack action added: '%s' -> '%s'") % creature->tag() % enemy->tag(), 2);
}

shared_ptr<Creature> Combat::getNearestEnemy(const Combatant &combatant) const {
    shared_ptr<Creature> result;
    float minDist = FLT_MAX;

    for (auto &enemy : combatant.enemies) {
        float dist = enemy->distanceTo(*combatant.creature);
        if (dist >= minDist) continue;

        result = enemy;
        minDist = dist;
    }

    return move(result);
}

void Combat::updateRounds(float dt) {
    for (auto &pair : _combatantById) {
        shared_ptr<Combatant> attacker(pair.second);
        if (attacker->creature->isDead()) continue;

        // Do not start a combat round, if attacker is a moving party leader

        if (attacker->creature->id() == _game->party().leader()->id() && _game->module()->player().isMovementRequested()) continue;

        // Check if attacker already participates in a combat round

        auto maybeAttackerRound = _roundByAttackerId.find(attacker->creature->id());
        if (maybeAttackerRound != _roundByAttackerId.end()) continue;

        // Check if attacker is close enough to attack its target

        AttackAction *action = getAttackAction(attacker->creature);
        if (!action) continue;

        shared_ptr<SpatialObject> target(action->target());
        if (!target || target->distanceTo(*attacker->creature) > action->range()) continue;

        // Check if target is valid combatant

        auto maybeTargetCombatant = _combatantById.find(target->id());
        if (maybeTargetCombatant == _combatantById.end()) continue;

        attacker->target = target;

        // Create a combat round if not a duel

        auto maybeTargetRound = _roundByAttackerId.find(target->id());
        bool isDuel = maybeTargetRound != _roundByAttackerId.end() && maybeTargetRound->second->target == attacker;
        if (!isDuel) {
            addRound(attacker, maybeTargetCombatant->second);
        }
    }
    for (auto it = _roundByAttackerId.begin(); it != _roundByAttackerId.end(); ) {
        shared_ptr<Round> round(it->second);
        updateRound(*round, dt);

        if (round->state == RoundState::Finished) {
            round->attacker->target.reset();
            it = _roundByAttackerId.erase(it);
        } else {
            ++it;
        }
    }
}

void Combat::addRound(const shared_ptr<Combatant> &attacker, const shared_ptr<Combatant> &target) {
    debug(boost::format("Combat: add round: '%s' -> '%s'") % attacker->creature->tag() % target->creature->tag(), 2);

    auto round = make_shared<Round>();
    round->attacker = attacker;
    round->target = target;

    _roundByAttackerId.insert(make_pair(attacker->creature->id(), round));
}

void Combat::updateRound(Round &round, float dt) {
    round.advance(dt);

    shared_ptr<Creature> attacker(round.attacker->creature);
    shared_ptr<Creature> target(round.target->creature);
    bool isDuel = round.target->target == attacker;

    if (attacker->isDead() || target->isDead()) {
        finishRound(round);
        return;
    }
    switch (round.state) {
        case RoundState::Started:
            attacker->face(*target);
            attacker->setMovementType(Creature::MovementType::None);
            attacker->setMovementRestricted(true);
            if (isDuel) {
                attacker->playAnimation(CombatAnimation::DuelAttack);
                target->face(*attacker);
                target->setMovementType(Creature::MovementType::None);
                target->setMovementRestricted(true);
                target->playAnimation(CombatAnimation::Dodge);
            } else {
                attacker->playAnimation(CombatAnimation::BashAttack);
            }
            round.state = RoundState::FirstTurn;
            debug(boost::format("Combat: first round turn started: '%s' -> '%s'") % attacker->tag() % target->tag(), 2);
            break;

        case RoundState::FirstTurn:
            if (round.time >= 0.5f * kRoundDuration) {
                executeAttack(attacker, target);

                if (isDuel) {
                    target->face(*attacker);
                    target->playAnimation(CombatAnimation::DuelAttack);
                    attacker->face(*target);
                    attacker->playAnimation(CombatAnimation::Dodge);
                }
                round.state = RoundState::SecondTurn;
                debug(boost::format("Combat: second round turn started: '%s' -> '%s'") % attacker->tag() % target->tag(), 2);
            }
            break;

        case RoundState::SecondTurn:
            if (round.time == kRoundDuration) {
                if (isDuel) {
                    executeAttack(target, attacker);
                }
                finishRound(round);
            }
            break;

        default:
            break;
    }
}

void Combat::finishRound(Round &round) {
    shared_ptr<Creature> attacker(round.attacker->creature);
    shared_ptr<Creature> target(round.target->creature);
    attacker->setMovementRestricted(false);
    target->setMovementRestricted(false);
    round.state = RoundState::Finished;
    debug(boost::format("Combat: round finished: '%s' -> '%s'") % attacker->tag() % target->tag(), 2);
}

void Combat::executeAttack(const std::shared_ptr<Creature> &attacker, const std::shared_ptr<SpatialObject> &target) {
    // TODO: add armor bonus and dexterity modifier
    int defense = 10;

    int attack = random(1, 20);
    if (attack == 1) {
        debug(boost::format("Combat: attack missed: '%s' -> '%s'") % attacker->tag() % target->tag(), 2);
        return;
    }

    if (attack == 20 || attack >= defense) {
        debug(boost::format("Combat: attack hit: '%s' -> '%s'") % attacker->tag() % target->tag(), 2);
        auto effects = _damageResolver.getDamageEffects(attacker);
        for (auto &effect : effects) {
            target->applyEffect(effect);
        }
    }
}

void Combat::updateActivation() {
    shared_ptr<Creature> partyLeader(_game->party().leader());
    bool active = partyLeader && _combatantById.count(partyLeader->id()) != 0;
    if (_active == active) return;

    if (active) {
        onEnterCombatMode();
    } else {
        onExitCombatMode();
    }
    _active = active;
}

bool Combat::isActive() const {
    return _active;
}

void Combat::onEnterCombatMode() {
    _game->module()->area()->setThirdPartyCameraStyle(CameraStyleType::Combat);
}

void Combat::onExitCombatMode() {
    _game->module()->area()->setThirdPartyCameraStyle(CameraStyleType::Default);
}

void Combat::cutsceneAttack(
    const shared_ptr<Creature> &attacker,
    const shared_ptr<SpatialObject> &target,
    int animation,
    AttackResult result,
    int damage) {

    // TODO: implement
}

} // namespace game

} // namespace reone
