/*
 * Copyright � 2020 uwadmin12
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

#include "object/area.h"
#include "party.h"
#include "../common/log.h"


using namespace std;

namespace reone {

namespace game {

// helper functions

AttackAction* getAttackAction(shared_ptr<Creature> &combatant) {
    return dynamic_cast<AttackAction*>(combatant->actionQueue().currentAction());
}

bool isActiveTargetInRange(shared_ptr<Creature> &combatant) {
    auto* action = getAttackAction(combatant);
    return action && action->isInRange();
}

void duel(shared_ptr<Creature> &attacker, shared_ptr<Creature> &target) {
    target->face(*attacker);
    attacker->face(*target);
    attacker->playAnimation("g8a1");
    target->playAnimation("g8g1");
}

void bash(shared_ptr<Creature> &attacker, shared_ptr<Creature> &target) {
    attacker->face(*target);
    attacker->playAnimation("g8a2");
}

void flinch(shared_ptr<Creature> &target) {
    target->playAnimation("g1y1");
}

// END helper functions

Combat::Combat(Area *area, Party *party) : _area(area), _party(party) {
    if (!area) {
        throw invalid_argument("Area must not be null");
    }
}

void Combat::update() {
    updateTimers(SDL_GetTicks());

    std::shared_ptr<Creature> pc = _party->leader();
    if (!pc) {
        debug("no pc yet ...");
        return;
    }

    // TODO: scan all party members??
    scanHostility(pc);
    activityScanner();

    if (activated()) {
        // One AIMaster per frame, rotated by activityScanner
        if (isAITimerDone(_activeCombatants.front())) {
            AIMaster(_activeCombatants.front());
            setAITimeout(_activeCombatants.front());
        }

        for (auto &cbt : _activeCombatants) combatStateMachine(cbt);
        animationSync();

        // rotate _activeCombatants
        _activeCombatants.push_back(_activeCombatants.front());
        _activeCombatants.pop_front();
    }

}

bool Combat::scanHostility(const std::shared_ptr<Creature>& actor) {
    // TODO: need a better mechanism to query creatures in range
    bool stillActive = false;
    for (auto &creature : _area->objectsByType()[ObjectType::Creature]) {
        if (glm::length(creature->position() - actor->position()) > MAX_DETECT_RANGE) {
            continue;
        }

        // TODO: add line-of-sight requirement

        const auto &target = static_pointer_cast<Creature>(creature);
        if (getIsEnemy(actor, target)) {
            stillActive = true;
            if (registerCombatant(target)) { // will fail if already registered
                debug(boost::format("combat: registered '%s', faction '%d'")
                      % target->tag() % static_cast<int>(target->getFaction()));
            }
        }
    }

    return stillActive;
}

void Combat::activityScanner() {
    if (_activeCombatants.empty()) {
        return;
    }

    auto &actor = _activeCombatants.front();

    bool stillActive = scanHostility(actor);

    // deactivate actor if !active
    if (!stillActive) { //&& getAttackAction(actor) == nullptr ???
        if (_deactivationTimer.completed.count(actor->id()) == 1) {
            _deactivationTimer.completed.erase(actor->id());

            // deactivation timeout complete
            actor->setCombatState(CombatState::Idle);

            _activeCombatants.pop_front();
            _activeCombatantIds.erase(actor->id());

            debug(boost::format("combat: deactivated '%s', combat_mode[%d]") % actor->tag() % activated());
        }
        else if (!_deactivationTimer.isRegistered(actor->id())) {
            _deactivationTimer.setTimeout(actor->id(), DEACTIVATION_TIMEOUT);

            debug(boost::format("combat: registered deactivation timer'%s'") % actor->tag());
        }
    }
    else { 
        if (_deactivationTimer.isRegistered(actor->id())) {
            _deactivationTimer.cancel(actor->id());

            debug(boost::format("combat: cancelled deactivation timer'%s'") % actor->tag());
        }
    }
}

void Combat::AIMaster(shared_ptr<Creature> &combatant) {
    if (combatant->id() == _party->leader()->id()) return;

    ActionQueue &cbt_queue = combatant->actionQueue();

    //if (cbt_queue.currentAction()) return;

    auto hostile = findNearestHostile(combatant, MAX_DETECT_RANGE);
    if (hostile) {
        cbt_queue.add(make_unique<AttackAction>(hostile));
        debug(boost::format("AIMaster: '%s' Queued to attack '%s'") % combatant->tag()
            % hostile->tag());
    }
}

void Combat::updateTimers(uint32_t currentTicks) {
    _stateTimer.update(currentTicks);
    _effectDelayTimer.update(currentTicks);
    _deactivationTimer.update(currentTicks);
    _AITimer.update(currentTicks);

    for (const auto &id : _stateTimer.completed) {
        if (--(_pendingStates[id]) == 0)
            _pendingStates.erase(id);
    }
    _stateTimer.completed.clear();

    for (auto &pr : _effectDelayTimer.completed) {
        if (!pr->first) continue;

        pr->first->applyEffect(std::move(pr->second));
        pr->second = nullptr; // dangling?
        _effectDelayIndex.erase(pr);
    }
    _effectDelayTimer.completed.clear();

    for (auto &id : _AITimer.completed) {
        _pendingAITimer.erase(id);
    }
    _AITimer.completed.clear();
}

void Combat::setStateTimeout(const std::shared_ptr<Creature> &creature, uint32_t delayTicks) {
    if (!creature) return;
    _stateTimer.setTimeout(creature->id(), delayTicks);

    // in case of repetition
    if (_pendingStates.count(creature->id()) == 0) _pendingStates[creature->id()] = 0;
    ++(_pendingStates[creature->id()]);
}

bool Combat::isStateTimerDone(const std::shared_ptr<Creature> &creature) {
    if (!creature) return false;
    return _pendingStates.count(creature->id()) == 0;
}

void Combat::setDelayEffectTimeout(std::unique_ptr<Effect> &&eff,
    std::shared_ptr<Creature> &target, uint32_t delayTicks) {
    auto index = _effectDelayIndex.insert(
        _effectDelayIndex.end(), std::make_pair(target, std::move(eff)));
    _effectDelayTimer.setTimeout(index, delayTicks);
}

void Combat::setAITimeout(const std::shared_ptr<Creature> &creature) {
    if (!creature) return;
    _AITimer.setTimeout(creature->id(), AI_MASTER_INTERVAL);
    
    _pendingAITimer.insert(creature->id());
}

bool Combat::isAITimerDone(const std::shared_ptr<Creature> &creature) {
    if (!creature) return false;
    return _pendingAITimer.count(creature->id()) == 0;
}

void Combat::onEnterAttackState(shared_ptr<Creature> &combatant) {
    if (!combatant) return;

    setStateTimeout(combatant, 1500);
    debug(boost::format("'%s' enters Attack state, actionQueueLen[%d], attackAction[%d]") 
        % combatant->tag() % combatant->actionQueue().size()
        % (getAttackAction(combatant) != nullptr)); // TODO: disable redundant info

    auto *action = getAttackAction(combatant);
    auto &target = action->target();

    if (target && (target->getCombatState() == CombatState::Idle
        || target->getCombatState() == CombatState::Cooldown)) {
        _duelQueue.push_back(make_pair(combatant, target));
        
        // synchronization
        combatStateMachine(target);
    } else {
        _bashQueue.push_back(make_pair(combatant, target));
    }

    setDelayEffectTimeout(
        std::make_unique<DamageEffect>(combatant), target, 500 // dummy delay
    );

    action->complete();
}

void Combat::onEnterDefenseState(shared_ptr<Creature> &combatant) {
    setStateTimeout(combatant, 1500);
    debug(boost::format("'%s' enters Defense state, set_timer") % combatant->tag());
}

void Combat::onEnterCooldownState(shared_ptr<Creature> &combatant) {
    setStateTimeout(combatant, 1500);
    debug(boost::format("'%s' enters Cooldown state, set_timer") % combatant->tag());
}

void Combat::combatStateMachine(shared_ptr<Creature> &combatant) {
    switch (combatant->getCombatState()) {
    case CombatState::Idle:
        for (auto &pr : _duelQueue) { // if combatant is caught in a duel
            if (pr.second && pr.second->id() == combatant->id()) {
                combatant->setCombatState(CombatState::Defense);
                onEnterDefenseState(combatant);
                return;
            }
        }

        if (isActiveTargetInRange(combatant)) {
            combatant->setCombatState(CombatState::Attack);
            onEnterAttackState(combatant);
        }
        return;

    case CombatState::Attack:
        if (isStateTimerDone(combatant)) {
            combatant->setCombatState(CombatState::Cooldown);
            onEnterCooldownState(combatant);
        }
        return;

    case CombatState::Cooldown:
        for (auto &pr : _duelQueue) { // if combatant is caught in a duel
            if (pr.second && pr.second->id() == combatant->id()) {
                combatant->setCombatState(CombatState::Defense);
                onEnterDefenseState(combatant);
                return;
            }
        }

        if (isStateTimerDone(combatant)) {
            combatant->setCombatState(CombatState::Idle);
            debug(boost::format("'%s' enters Idle state") % combatant->tag());
        }
        return;

    case CombatState::Defense:
        if (isStateTimerDone(combatant)) {
            combatant->setCombatState(CombatState::Idle);
            debug(boost::format("'%s' enters Idle state") % combatant->tag());

            // synchronization
            combatStateMachine(combatant);
        }
        return;

    default:
        return;
    }
}

void Combat::animationSync() {
    while (!_duelQueue.empty()) {
        auto &pr = _duelQueue.front();
        duel(pr.first, pr.second);
        _duelQueue.pop_front();
    }

    while (!_bashQueue.empty()) {
        auto &pr = _bashQueue.front();
        duel(pr.first, pr.second);
        _bashQueue.pop_front();
    }
}

shared_ptr<Creature> Combat::findNearestHostile(shared_ptr<Creature> &combatant, float detectionRange) {
    shared_ptr<Creature> closest_target = nullptr;
    float min_dist = detectionRange;

    for (auto &creature : _activeCombatants) {
        if (creature->id() == combatant->id()) continue;

        if (!getIsEnemy(static_pointer_cast<Creature>(creature), combatant))
            continue;

        float distance = glm::length(creature->position() - combatant->position()); // TODO: fine tune the distance
        if (distance < min_dist) {
            min_dist = distance;
            closest_target = static_pointer_cast<Creature>(creature);
        }
    }

    return move(closest_target);
}

bool Combat::registerCombatant(const std::shared_ptr<Creature> &combatant) {
    auto res = _activeCombatantIds.insert(combatant->id());
    if (res.second) { // combatant not already in _activeCombatantIds
        _activeCombatants.push_back(combatant);
    }
    return res.second;
}

} // namespace game

} // namespace reone