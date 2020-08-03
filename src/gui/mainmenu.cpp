/*
 * Copyright � 2020 Vsevolod Kremianskii
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

#include "mainmenu.h"

using namespace reone::render;
using namespace reone::resources;

namespace reone {

namespace gui {

MainMenu::MainMenu(const GraphicsOptions &opts) : GUI(opts) {
}

void MainMenu::load(GameVersion version) {
    GUI::load(getResRef(version), version == GameVersion::KotOR ? BackgroundType::Menu : BackgroundType::None);

    hideControl("BTN_WARP");
    hideControl("LBL_NEWCONTENT");
    hideControl("LBL_BW");
    hideControl("LBL_LUCAS");
}

std::string MainMenu::getResRef(GameVersion version) const {
    std::string resRef("mainmenu");
    switch (version) {
        case GameVersion::KotOR:
            resRef += "16x12";
            break;
        case GameVersion::TheSithLords:
            resRef += "8x6_p";
            break;
    }

    return resRef;
}

void MainMenu::onClick(const std::string &control) {
    if (control == "BTN_NEWGAME") {
        if (_onNewGame) _onNewGame();
    } else if (control == "BTN_EXIT") {
        if (_onExit) _onExit();
    }
}

void MainMenu::setOnNewGame(const std::function<void()> &fn) {
    _onNewGame = fn;
}

void MainMenu::setOnExit(const std::function<void()> &fn) {
    _onExit = fn;
}

} // namespace gui

} // namespace reone
