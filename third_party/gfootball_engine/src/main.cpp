// Copyright 2019 Google LLC & Bastiaan Konings
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// written by bastiaan konings schuiling 2008 - 2015
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#ifdef WIN32
#include <windows.h>
#endif

#include <string>

#include "ai/ai_keyboard.hpp"
#include "base/log.hpp"
#include "base/math/bluntmath.hpp"
#include "base/utils.hpp"
#include "file.h"
#include "main.hpp"
#include "scene/objectfactory.hpp"
#include "scene/scene2d/scene2d.hpp"
#include "scene/scene3d/scene3d.hpp"
#include "utils/objectloader.hpp"
#include "utils/orbitcamera.hpp"
#include "wrap_SDL_ttf.h"
#include "game_env.hpp"

using std::string;

#if defined(WIN32) && defined(__MINGW32__)
#undef main
#endif

using namespace blunted;

thread_local GameEnv* game;

void DoValidation() {
  auto game = GetGame();
  if (game) {
    game->tracker->verify();
  }
}

GameEnv* GetGame() { return game; }

GameContext& GetContext() {
  DO_VALIDATION;
  return *game->context;
}

void SetGame(GameEnv* c) { game = c; }

boost::shared_ptr<Scene2D> GetScene2D() {
  DO_VALIDATION;
  return game->context->scene2D;
}

boost::shared_ptr<Scene3D> GetScene3D() {
  DO_VALIDATION;
  return game->context->scene3D;
}

GraphicsSystem* GetGraphicsSystem() {
  DO_VALIDATION;
  return game->context->graphicsSystem.get();
}

boost::shared_ptr<GameTask> GetGameTask() {
  DO_VALIDATION;
  return game->context->gameTask;
}

boost::shared_ptr<MenuTask> GetMenuTask() {
  DO_VALIDATION;
  return game->context->menuTask;
}

Properties* GetConfiguration() {
  DO_VALIDATION;
  return game->context->config;
}

ScenarioConfig& GetScenarioConfig() {
  DO_VALIDATION;
  return *(game->context->scenario_config);
}

GameConfig& GetGameConfig() {
  DO_VALIDATION;
  return game->context->game_config;
}

const std::vector<IHIDevice*>& GetControllers() {
  DO_VALIDATION;
  return game->context->controllers;
}

void randomize(unsigned int seed) {
  DO_VALIDATION;
  srand(seed);
  rand(); // mingw32? buggy compiler? first value seems bogus
  randomseed(seed); // for the boost random
}

void run_game(Properties* input_config) {
  DO_VALIDATION;
  game->context->config = input_config;
  Initialize(*game->context->config);
  randomize(0);

  // initialize systems
  game->context->graphicsSystem.reset(new GraphicsSystem());
  game->context->graphicsSystem->Initialize(*game->context->config);

  // init scenes

  game->context->scene2D.reset(new Scene2D(*game->context->config));
  game->context->graphicsSystem->Create2DScene(game->context->scene2D);
  game->context->scene2D->Init();
  game->context->scene3D.reset(new Scene3D());
  game->context->graphicsSystem->Create3DScene(game->context->scene3D);
  game->context->scene3D->Init();

  for (int x = 0; x < 2 * MAX_PLAYERS; x++) {
    DO_VALIDATION;
    game->context->controllers.push_back(new AIControlledKeyboard());
  }
  // sequences

  game->context->gameTask = boost::shared_ptr<GameTask>(new GameTask());
  std::string fontfilename = game->context->config->Get(
      "font_filename", "media/fonts/alegreya/AlegreyaSansSC-ExtraBold.ttf");
  game->context->font = GetFile(fontfilename);
  game->context->defaultFont =
      TTF_OpenFontIndexRW(SDL_RWFromConstMem(game->context->font.data(),
                                             game->context->font.size()),
                          0, 32, 0);
  if (!game->context->defaultFont)
    Log(e_FatalError, "football", "main",
        "Could not load font " + fontfilename);
  game->context->defaultOutlineFont =
      TTF_OpenFontIndexRW(SDL_RWFromConstMem(game->context->font.data(),
                                             game->context->font.size()),
                          0, 32, 0);
  TTF_SetFontOutline(game->context->defaultOutlineFont, 2);
  game->context->menuTask = boost::shared_ptr<MenuTask>(
      new MenuTask(5.0f / 4.0f, 0, game->context->defaultFont,
                   game->context->defaultOutlineFont, game->context->config));
}
  // fire!

void quit_game() {
  DO_VALIDATION;
  game->context->gameTask.reset();
  game->context->menuTask.reset();

  game->context->scene2D.reset();
  game->context->scene3D.reset();

  for (unsigned int i = 0; i < game->context->controllers.size(); i++) {
    DO_VALIDATION;
    delete game->context->controllers[i];
  }
  game->context->controllers.clear();

  TTF_CloseFont(game->context->defaultFont);
  TTF_CloseFont(
      game->context->defaultOutlineFont);

  delete game->context->config;

  Exit();
}

void Tracker::setSession(int id) {
  session = id;
  if (!sessions.count(session)) {
    sessions[session] = -1;
  }
}

void Tracker::disable() { session = -1; }

void Tracker::reset() {
  session = -1;
  failure_pos = -1;
  sessions.clear();
  std::cout << "Tracker: validating [" << start << "," << end
            << "], step: " << step << std::endl;
}

bool Tracker::isFailure() { return failure_pos > -1; }

void Tracker::verify_internal(int pos) {
  if (!game->context->gameTask->GetMatch()) return;

  string stack;
  if (!stack_trace.count(pos)) {
    if (traces.size() > 100000) {
      Log(blunted::e_FatalError, "Too many traces", "", "");
    }
    EnvState reader("");
    GetGame()->ProcessState(&reader);
    stack_trace[pos] = stack;
    traces[pos] = reader.GetState();
  } else if (failure_pos == -1 || failure_pos > pos) {
    bool error = false;
    if (stack != stack_trace[pos]) {
      if (step == 1) {
        Log(blunted::e_FatalError, "Stack trace mismatch", stack,
            stack_trace[pos]);
      } else {
        error = true;
      }
    }
    if (step == 1) {
      EnvState reader("", traces[pos]);
      GetGame()->ProcessState(&reader);
    } else {
      EnvState reader("");
      GetGame()->ProcessState(&reader);
      if (reader.GetState() != traces[pos]) {
        error = true;
      }
    }
    if (error) {
      std::cout << "Error found at position: " << pos << std::endl;
      failure_pos = pos;
      start = std::max(pos - 2 * step, 0);
      end = pos;
      step = std::max(1, (end - start) / 1000);
    }
  }
}

void GameContext::ProcessState(EnvState* state) {
  scenario_config->ProcessState(state);
  state->process(step);
}
