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

#pragma once

#include <cstdint>
#include <functional>
#include <map>

#include <boost/filesystem/path.hpp>

#include "SDL2/SDL_events.h"

#include "../render/window.h"
#include "../resource/types.h"
#include "../scene/pipeline/world.h"
#include "../scene/scenegraph.h"

#include "console.h"
#include "gui/chargen/chargen.h"
#include "gui/container.h"
#include "gui/dialog.h"
#include "gui/hud.h"
#include "gui/ingame/ingame.h"
#include "gui/loadscreen.h"
#include "gui/mainmenu.h"
#include "gui/partyselect.h"
#include "gui/saveload.h"
#include "object/module.h"
#include "object/objectfactory.h"
#include "object/spatial.h"
#include "party.h"
#include "types.h"

namespace reone {

namespace audio {

class SoundHandle;

}

namespace video {

class Video;

}

namespace game {

/**
 * Facade for the game logic. Contains the main game loop. Delegates work to the
 * instances of `Module` and `GUI`.
 *
 * @see game::Module
 * @see gui::GUI
 */
class Game : public render::IEventHandler {
public:
    Game(const boost::filesystem::path &path, const Options &opts);
    virtual ~Game() = default;

    bool handle(const SDL_Event &event) override;

    int run();

    void loadModule(const std::string &name, const std::string &entry = "");
    void openSaveLoad(SaveLoad::Mode mode);
    void openPartySelection(const PartySelection::Context &ctx);
    void openMainMenu();
    void openInGameMenu(InGameMenu::Tab tab);
    void openInGame();
    void openContainer(const std::shared_ptr<SpatialObject> &container);
    void scheduleModuleTransition(const std::string &moduleName, const std::string &entry);
    void startCharacterGeneration();
    void startDialog(const std::shared_ptr<SpatialObject> &owner, const std::string &resRef);
    void quit();

    void playVideo(const std::string &name);

    bool isLoadFromSaveGame() const;

    Camera *getActiveCamera() const;
    int getRunScriptVar() const;

    resource::GameVersion version() const;
    const Options &options() const;
    scene::SceneGraph &sceneGraph();
    ObjectFactory &objectFactory();
    std::shared_ptr<Module> module() const;
    Party &party();
    CharacterGeneration &characterGeneration();
    CameraType cameraType() const;

    void setCursorType(CursorType type);
    void setLoadFromSaveGame(bool load);
    void setRunScriptVar(int var);

    // Globals/locals

    bool getGlobalBoolean(const std::string &name) const;
    int getGlobalNumber(const std::string &name) const;
    std::string getGlobalString(const std::string &name) const;
    std::shared_ptr<Location> getGlobalLocation(const std::string &name) const;
    bool getLocalBoolean(uint32_t objectId, int idx) const;
    int getLocalNumber(uint32_t objectId, int idx) const;

    void setGlobalBoolean(const std::string &name, bool value);
    void setGlobalNumber(const std::string &name, int value);
    void setGlobalString(const std::string &name, const std::string &value);
    void setGlobalLocation(const std::string &name, const std::shared_ptr<Location> &location);
    void setLocalBoolean(uint32_t objectId, int idx, bool value);
    void setLocalNumber(uint32_t objectId, int idx, int value);

    // END Globals/locals

protected:
    Options _options;

    virtual void init();
    virtual void update();

private:
    enum class GameScreen {
        None,
        MainMenu,
        Loading,
        CharacterGeneration,
        InGame,
        InGameMenu,
        Dialog,
        Container,
        PartySelection,
        SaveLoad
    };

    boost::filesystem::path _path;
    render::RenderWindow _window;
    scene::SceneGraph _sceneGraph;
    scene::WorldRenderPipeline _worldPipeline;
    Console _console;
    resource::GameVersion _version { resource::GameVersion::KotOR };
    std::unique_ptr<ObjectFactory> _objectFactory;
    GameScreen _screen { GameScreen::MainMenu };
    Party _party;
    uint32_t _ticks { 0 };
    bool _quit { false };
    std::shared_ptr<video::Video> _video;
    CursorType _cursorType { CursorType::None };
    float _gameSpeed { 1.0f };
    bool _loadFromSaveGame { false };
    CameraType _cameraType { CameraType::ThirdPerson };
    int _runScriptVar { -1 };

    // Modules

    std::string _nextModule;
    std::string _nextEntry;
    std::shared_ptr<Module> _module;
    std::map<std::string, std::shared_ptr<Module>> _loadedModules;

    // END Modules

    // GUI

    std::unique_ptr<MainMenu> _mainMenu;
    std::unique_ptr<LoadingScreen> _loadScreen;
    std::unique_ptr<CharacterGeneration> _charGen;
    std::unique_ptr<HUD> _hud;
    std::unique_ptr<InGameMenu> _inGame;
    std::unique_ptr<DialogGUI> _dialog;
    std::unique_ptr<Container> _container;
    std::unique_ptr<PartySelection> _partySelect;
    std::unique_ptr<SaveLoad> _saveLoad;

    // END GUI

    // Audio

    std::string _musicResRef;
    std::shared_ptr<audio::SoundHandle> _music;
    std::shared_ptr<audio::SoundHandle> _movieAudio;

    // END Audio

    // Globals/locals

    std::map<std::string, std::string> _globalStrings;
    std::map<std::string, bool> _globalBooleans;
    std::map<std::string, int> _globalNumbers;
    std::map<std::string, std::shared_ptr<Location>> _globalLocations;
    std::map<uint32_t, std::map<int, bool>> _localBooleans;
    std::map<uint32_t, std::map<int, int>> _localNumbers;

    // END Globals/locals

    Game(const Game &) = delete;
    Game &operator=(const Game &) = delete;

    bool handleMouseButtonDown(const SDL_MouseButtonEvent &event);
    bool handleKeyDown(const SDL_KeyboardEvent &event);
    void loadNextModule();
    float measureFrameTime();
    void playMusic(const std::string &resRef);
    void runMainLoop();
    void toggleInGameCameraType();
    void updateCamera(float dt);
    void stopMovement();
    void changeScreen(GameScreen screen);
    void updateVideo(float dt);
    void updateMusic();

    std::string getMainMenuMusic() const;
    std::string getCharacterGenerationMusic() const;
    gui::GUI *getScreenGUI() const;

    // Initialization

    void initGameVersion();
    void deinit();

    // END Initialization

    // Loading

    void loadCharacterGeneration();
    void loadContainer();
    void loadDialog();
    void loadHUD();
    void loadInGame();
    void loadLoadingScreen();
    void loadMainMenu();
    void loadPartySelection();
    void loadSaveLoad();

    // END Loading

    // Rendering

    void drawAll();
    void drawWorld();
    void drawGUI();
    void drawCursor();

    // END Rendering

    // Helper methods

    void withLoadingScreen(const std::function<void()> &block);

    // END Helper methods
};

} // namespace game

} // namespace reone
