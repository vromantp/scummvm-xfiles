/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "common/algorithm.h"
#include "common/config-manager.h"
#include "common/events.h"
#include "common/formats/winexe.h"
#include "common/list.h"
#include "common/system.h"
#include "common/tokenizer.h"
#include "common/translation.h"
#include "engines/util.h"
#include "graphics/pixelformat.h"
#include "gui/message.h"

#include "xfiles/xfiles.h"
#include "xfiles/debug/console.h"
#include "xfiles/media/movie.h"
#include "xfiles/media/pictures.h"
#include "xfiles/parse.h"
#include "xfiles/script/script.h"
#include "xfiles/script/vcdata.h"
#include "xfiles/ui/view_band.h"

namespace XFiles {

XFilesEngine *g_engine;

XFilesEngine::XFilesEngine(OSystem *syst, const ADGameDescription *gameDesc) : Engine(syst),
	_gameDescription(gameDesc),
	_randomSource("xfiles"),
	_data(nullptr),
	_script(nullptr),
	_menuX(nullptr),
	_menuEntries(nullptr),
	_menuClick(nullptr),
	_menuLoop(nullptr),
	_menuLoopSound(nullptr),
	_menuTop(nullptr),
	_nightVisionMask(nullptr),
	_binocularMask(nullptr),
	_binocularScaled(nullptr),
	_binocularScaledBeam(0),
	_flashlightMask(nullptr),
	_flashlightScaled(nullptr),
	_flashlightScaledKey(0),
	_bandTintOn(false),
	_bandTintTool(0),
	_viewfinder(nullptr),
	_counterFont(nullptr),
	_counterFontOwned(false),
	_menuReturn(nullptr),
	_menuIntroDone(false),
	_menuFading(false),
	_menuIntroPlay(false),
	_menuHelpEnd(5000),
	_menuHelpPlaying(false),
	_menuTopHot(false),
	_menuOpen(false),
	_menuLayout(false),
	_menuReopen(false),
	_paused(false),
	_pauseDepth(0),
	_pauseStart(0),
	_pausedTotal(0),
	_video(nullptr),
	_videoStarted(0),
	_viewId(0),
	_hotMovie(nullptr),
	_shots(0),
	_shotDue(0),
	_shotId(0),
	_shotView(0),
	_shotBox(false),
	_nextStep(0),
	_waitVideo(0),
	_delayUntil(0),
	_waitTimer(-1),
	_dialogBoxView(0),
	_textsDll(nullptr),
	_texts(nullptr),
	_interface(nullptr),
	_cursors(nullptr),
	_lastClickTime(0),
	_dragIdea(0),
	_viewHover(0),
	_dragMoved(false),
	_timersPaused(false),
	_movieCursor(false),
	_whiteHold(false),
	_barRevealed(false),
	_barRevealEnd(0),
	_sound(_discFiles) {
	g_engine = this;
	addHooks();
	_loadedSave = false;
	_checkpointNew = false;
	_gameEnded = false;
	_stepAnswer = -1;
	_pendingClipTime = -1;
	memset(_uberVariables, 0, sizeof(_uberVariables));
	_lastClickHit.kind = Interface::kHitNone;
	_lastClickHit.id = 0;
	_hover = _lastClickHit;
}

XFilesEngine::~XFilesEngine() {
	_sound.clear();
	closeMenu();
	closeView();
	delete _interface;
	delete _cursors;
	delete _video;
	delete _viewfinder;
	delete _nightVisionMask;
	delete _binocularMask;
	delete _flashlightMask;
	if (_flashlightScaled) {
		_flashlightScaled->free();
		delete _flashlightScaled;
	}
	if (_binocularScaled) {
		_binocularScaled->free();
		delete _binocularScaled;
	}
	for (uint i = 0; i < _photoPictures.size(); i++) {
		if (_photoPictures[i].surface) {
			_photoPictures[i].surface->free();
			delete _photoPictures[i].surface;
		}
	}
	if (_counterFontOwned)
		delete _counterFont;
	delete _texts;
	delete _textsDll;
	delete _script;
	delete _data;
	_screen.free();
	g_engine = nullptr;
}

bool XFilesEngine::loadDatabases(bool fromSave) {
	// The databases are on disc 0: in the game directory or in cd1/MININST
	Common::SeekableReadStream *hdb = _discFiles.openFile(0, "XFiles.hdb");
	if (!hdb)
		return false;
	Common::SeekableReadStream *gam = nullptr;
	// xfiles_save names an original .X save: it takes the place of XFiles.gam
	// (it is a copy of it) and the game continues from it
	Common::Path savePath = fromSave ? ConfMan.getPath("xfiles_save") : Common::Path();
	Common::SeekableReadStream *saveStream = nullptr;
	if (!savePath.empty()) {
		saveStream = Common::FSNode(savePath).createReadStream();
		if (!saveStream) {
			warning("Cannot open the save %s", savePath.toString().c_str());
			delete hdb;
			return false;
		}
	} else {
		gam = _discFiles.openFile(0, "XFiles.gam");
		if (!gam) {
			delete hdb;
			return false;
		}
	}
	_data = new VCData();
	bool loaded = _data->load(*hdb, saveStream ? *saveStream : *gam);
	delete saveStream;
	delete gam;
	delete hdb;
	if (!loaded)
		return false;
	_loadedSave = !savePath.empty() && _data->getSave(_save);
	if (!savePath.empty() && !_loadedSave) {
		warning("%s is not a save", savePath.toString().c_str());
		return false;
	}
	debugC(1, kDebugDatabase, "Loaded %u classes from XFiles.hdb, %u from XFiles.gam",
		_data->getHdb().getClasses().size(), _data->getGam().getClasses().size());
	return true;
}

Common::Error XFilesEngine::run() {
	_discFiles.init(Common::FSNode(ConfMan.getPath("path")));
	if (!loadDatabases(true))
		return Common::Error(Common::kReadingFailed, "XFiles.hdb / XFiles.gam");

	// A scripted run's registry is fixed (B-207): the setting xfiles_prefs, not the player's settings. It is read
	// here because Auto Set Depth picks the screen format
	if (!ConfMan.get("xfiles_steps").empty())
		_preferences.setScripted(ConfMan.get("xfiles_prefs"));
	_preferences.setTouchDefaults(g_system->hasFeature(OSystem::kFeatureTouchscreen));
	// The exe draws at the screen depth: 32 bits in its Quality configuration, 16 with Auto Set Depth L. ScummVM has
	// no desktop depth to keep, so H, N and A take the best depth the backend offers (B-217). The list overload of
	// initGraphics takes the backend's first format that the list holds, so the order here would not count: the
	// preferred format the backend lists is picked here. The OpenGL backend lists ARGB8888 but no XRGB8888
	Common::List<Graphics::PixelFormat> wanted;
	if (!_preferences.wantsReducedDepth()) {
		wanted.push_back(Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0));
		wanted.push_back(Graphics::PixelFormat::createFormatARGB32());
	}
	wanted.push_back(Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
	const Common::List<Graphics::PixelFormat> supported = g_system->getSupportedFormats();
	Graphics::PixelFormat format = wanted.back();
	for (const auto &candidate : wanted) {
		if (Common::find(supported.begin(), supported.end(), candidate) != supported.end()) {
			format = candidate;
			break;
		}
	}
	applyWidescreen(&format);
	_screen.create(kScreenWidth, kScreenHeight, g_system->getScreenFormat());
	setDebugger(new Console());

	_cursors = new Cursors(*_data, _discFiles);
	_cursors->init(_screen.format);
	_captions.loadFonts();
	_interface = new Interface(*_data, _discFiles);
	_interface->setFormat(_screen.format);
	if (!_dialogBox.load(*_data, _discFiles, _screen.format))
		warning("Cannot load the dialog box pictures from x.pff");
	// The language DLL with the topic texts (the exe loads it with LoadLibrary)
	_textsDll = _discFiles.openFile(0, "XFilesT.dll");
	if (!_textsDll)
		_textsDll = _discFiles.openFile(1, "XFilesT.dll");
	if (_textsDll)
		_texts = Common::WinResources::createFromEXE(_textsDll);
	if (!_texts)
		warning("Cannot load XFilesT.dll; topics show their authoring names");
	_interface->init(_texts);
	_headCredits.load(_discFiles);
	_subliminal.load(_discFiles);

	_shotsDir = ConfMan.getPath("xfiles_shots");
	_steps = Common::StringTokenizer(ConfMan.get("xfiles_steps")).split();
	if (!_steps.empty()) {
		_mouse = Common::Point(320, 240); // tools/ref/run.ps1 Start-Game: move 320 240
		// xfiles_mouse=X,Y: another start point (the triage engine check uses -1,-1, under no item)
		Common::String start = ConfMan.get("xfiles_mouse");
		int32 x = 0, y = 0;
		const char *comma = start.empty() ? nullptr : parseInt(start.c_str(), x);
		if (comma && *comma == ',' && parseInt(comma + 1, y))
			_mouse = Common::Point(x, y);
	}
	syncSoundSettings();
	_fastForward = Common::StringTokenizer(ConfMan.get("xfiles_fastforward")).split();

	_video = new Movie();
	// -x / --save-slot: load that save before the intro, and skip the intro and the menu
	int saveSlot = ConfMan.getInt("save_slot");
	if (saveSlot >= 0) {
		Common::Error status = loadGameState(saveSlot);
		if (status.getCode() != Common::kNoError)
			warning("Cannot load save slot %d: %s", saveSlot, status.getDesc().c_str());
	}
	// A step list starts the game at once, without the intro and the menu. The exe has shown the menu's fade-in by
	// then, so the X corner opens the still menu (g_menuIntroDone)
	bool play;
	if (_pendingLoad.valid || !_steps.empty() || !_fastForward.empty() || _loadedSave) {
		_menuIntroDone = true;
		play = true;
	} else {
		play = playIntro() && runMenu();
	}
	// EndGame without a Try again takes the game down to the start-up menu (B-176); a scripted run has nobody there
	while (play && !shouldQuit()) {
		_gameEnded = false;
		runGame();
		play = _gameEnded && _steps.empty() && !shouldQuit() && runMenu();
	}
	return Common::kNoError;
}

// -- Screen and input ---------------------------------------------------------

XFilesEngine::Input XFilesEngine::pollInput() {
	Input input;
	input.click = false;
	input.skip = false;
	input.escape = false;
	input.rightClick = false;
	input.release = false;
	input.wheel = 0;
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
		// The widescreen canvas has black sides: canvas x to screen x (clamped to the 640 columns)
		Common::Point canvasMouse = event.mouse;
		event.mouse = toScreen(canvasMouse);
		if (event.type == Common::EVENT_LBUTTONDOWN && _steps.empty()) {
			input.click = true;
			input.pos = event.mouse;
		} else if (event.type == Common::EVENT_LBUTTONUP && _steps.empty()) {
			input.release = true;
			input.releasePos = event.mouse;
			if (_script)
				_script->release();
		} else if (event.type == Common::EVENT_MOUSEMOVE && _steps.empty()) {
			// A step list (xfiles_steps) keeps the mouse where a reference
			// run puts it, whatever the real mouse does
			_mouse = event.mouse;
			_canvasMouse = canvasMouse;
			_canvasMouseScreen = _mouse;
			if (_dragIdea && (ABS(_mouse.x - _dragStart.x) > 3 || ABS(_mouse.y - _dragStart.y) > 3))
				_dragMoved = true;
		} else if ((event.type == Common::EVENT_WHEELUP || event.type == Common::EVENT_WHEELDOWN) && _steps.empty()) {
			input.wheel += event.type == Common::EVENT_WHEELUP ? -1 : 1;
			input.wheelPos = event.mouse;
		} else if (event.type == Common::EVENT_RBUTTONDOWN) {
			// The exe ends the playing video on a right-click as on a natural end
			input.skip = true;
			input.rightClick = _steps.empty();
			input.rightPos = event.mouse;
		} else if (event.type == Common::EVENT_CUSTOM_ENGINE_ACTION_START && event.customType == kActionEscape) {
			input.skip = true;
			input.escape = true;
		} else if (event.type == Common::EVENT_KEYDOWN && _script && _steps.empty()) {
			uint key = getKeyValue(event.kbd);
			if (key)
				_script->keyPress(key);
		}
	}
	return input;
}

uint XFilesEngine::getKeyValue(const Common::KeyState &kbd) {
	// The exe posts a Key Press for the key-down of Backspace, Tab, Return and Delete (virtual-key codes), and
	// otherwise for the character of WM_CHAR (docs/subsystems/computers.md, "Text entry")
	switch (kbd.keycode) {
	case Common::KEYCODE_BACKSPACE:
		return 0x08;
	case Common::KEYCODE_TAB:
		return 0x09;
	case Common::KEYCODE_RETURN:
	case Common::KEYCODE_KP_ENTER:
		return 0x0d;
	case Common::KEYCODE_DELETE:
		return 0x2e;
	default:
		break;
	}
	if (kbd.ascii >= 0x20 && kbd.ascii < 0x7f)
		return kbd.ascii;
	// The numpad without an ascii value
	if (kbd.keycode >= Common::KEYCODE_KP0 && kbd.keycode <= Common::KEYCODE_KP9)
		return '0' + (kbd.keycode - Common::KEYCODE_KP0);
	if (kbd.keycode == Common::KEYCODE_KP_MINUS)
		return '-';
	return 0;
}

Common::Point XFilesEngine::toScreen(const Common::Point &canvas) const {
	if (_bandZoomed)
		return _wide.toScreen(canvas);
	// The black sides act as the screen edge
	return Common::Point(CLIP<int16>(canvas.x - _wideDx, 0, kScreenWidth - 1), CLIP<int16>(canvas.y, 0, kScreenHeight - 1));
}

Common::Point XFilesEngine::toCanvas(const Common::Point &screen) const {
	if (_bandZoomed)
		return WideLayout::toCanvas(screen);
	return Common::Point(screen.x + _wideDx, screen.y);
}

void XFilesEngine::captureBandPass() {
	// Zoomed only with no shown layout and no menu; the caller checked the frame is the 600x240 band
	_bandCaptured = false;
	_wide.end();
	if (!_wideDx || _menuOpen || _menuLayout || _interface->hasLayouts())
		return;
	_bandPass.free();
	_bandPass.copyFrom(_screen.getSubArea(Common::Rect(kBandLeft, kBandTop, kBandRight, kBandBottom)));
	_bandCaptured = true;
	_wide.begin(_screen); // the UI pass records each element it draws from here on
}

void XFilesEngine::composeWide(Graphics::Surface &canvas, bool zoomed) {
	// The framed screen at the frame offset; zoomed, the band pass scaled 1.25x (bilinear: it is video) on black, and
	// the UI pass's elements pasted at the places WideLayout gives them (B-354)
	canvas.create(kWideWidth, kScreenHeight, _screen.format);
	canvas.fillRect(Common::Rect(canvas.w, canvas.h), 0);
	const int bpp = _screen.format.bytesPerPixel;
	if (!zoomed) {
		for (int y = 0; y < _screen.h; y++)
			memcpy(canvas.getBasePtr(_wideDx, y), _screen.getBasePtr(0, y), _screen.w * bpp);
		return;
	}
	Graphics::Surface *scaled = _bandPass.scale(kZoomWidth, kZoomHeight, true);
	for (int y = 0; y < kZoomHeight; y++)
		memcpy(canvas.getBasePtr(kZoomLeft, kZoomTop + y), scaled->getBasePtr(0, y), kZoomWidth * bpp);
	scaled->free();
	delete scaled;
	_wide.compose(canvas);
}

void XFilesEngine::present() {
	if (_wideDx && _bandCaptured) {
		_wide.mark(WideLayout::kOther, _screen);
		_wide.end();
		Graphics::Surface canvas;
		composeWide(canvas, true);
		g_system->copyRectToScreen(canvas.getPixels(), canvas.pitch, 0, 0, canvas.w, canvas.h);
		canvas.free();
		_bandZoomed = true;
	} else {
		if (_bandZoomed) {
			g_system->fillScreen(0); // the zoomed band reached into the black sides
			_bandZoomed = false;
		}
		g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, _wideDx, 0, _screen.w, _screen.h);
	}
	_bandCaptured = false;
	g_system->updateScreen();
	g_system->delayMillis(10);
}

void XFilesEngine::blit(const Graphics::Surface &src, int x, int y) {
	if (!src.getPixels())
		return;
	Common::Rect r(x, y, x + src.w, y + src.h);
	r.clip(Common::Rect(_screen.w, _screen.h));
	if (r.isEmpty())
		return;
	_screen.copyRectToSurface(src, r.left, r.top, Common::Rect(r.left - x, r.top - y, r.right - x, r.bottom - y));
}

Common::Point XFilesEngine::getBandPosition(const Movie &movie) const {
	return Common::Point(kBandLeft + (kBandWidth - (int)movie.getWidth()) / 2, kBandTop + (kBandHeight - (int)movie.getHeight()) / 2);
}

uint32 XFilesEngine::hotKindAt(const Common::Point &pos) const {
	// Kind 29 objects test the point moved by the band origin (HitTestObject 0x519e40, 0x51ad80), before every
	// view object, and the first live one takes the click and the cursor even when nothing happens (B-031)
	if (!_hotMovie || _hotMovie->hasEnded())
		return 0;
	Common::Point band = getBandPosition(*_hotMovie);
	const HotEntry *e = _hot.find(_hotMovie->getTime(), Common::Point(pos.x - band.x, pos.y - band.y));
	return e ? e->kind : 0;
}

bool XFilesEngine::openMovie(Movie &movie, uint32 assetId) {
	VCAsset asset;
	if (!_data->getAsset(assetId, asset)) {
		warning("#%u is not a media asset", assetId);
		return false;
	}
	if (!movie.open(_discFiles, asset))
		return false;
	if (_assetRanges.contains(assetId)) {
		movie.setStart(_assetRanges[assetId].first);
		movie.setEnd(_assetRanges[assetId].second);
	}
	return true;
}

bool XFilesEngine::getTimerLeft(uint32 id, uint32 &leftMs) {
	const Common::HashMap<uint, Script::Timer> &timers = _script->getTimers();
	if (!timers.contains(id))
		return false;
	const Script::Timer &timer = timers[id];
	if (!_timerStates.contains(id) || _timerStates[id].serial != timer.serial) {
		leftMs = timer.ms; // started, not yet counted by updateTimers
		return true;
	}
	const TimerState &state = _timerStates[id];
	uint32 elapsed = state.paused ? 0 : gameMillis() - state.started;
	leftMs = elapsed < state.ms ? state.ms - elapsed : 0;
	return true;
}

void XFilesEngine::setAssetRange(uint32 asset, uint32 startMs, uint32 endMs) {
	_assetRanges[asset] = Common::Pair<uint32, uint32>(startMs, endMs);
}

// -- Pause and the game clock -------------------------------------------------

uint32 XFilesEngine::gameMillis() const {
	return (_paused ? _pauseStart : g_system->getMillis()) - _pausedTotal;
}

void XFilesEngine::pauseGame(bool pause) {
	// Scene_SuspendAll: the timers (Timers_Pause), the scene's objects and
	// the game's timed objects stop, so everything goes on where it was. The menu and
	// the GMM (pauseEngineIntern) can both ask: the game runs again when
	// the last one resumes
	if (pause) {
		if (_pauseDepth++ > 0)
			return;
	} else {
		if (_pauseDepth == 0 || --_pauseDepth > 0)
			return;
	}
	if (pause) {
		_pauseStart = g_system->getMillis();
	} else {
		uint32 paused = g_system->getMillis() - _pauseStart;
		_pausedTotal += paused;
		debugC(1, kDebugScript, "-- game resumed after %u ms", paused);
	}
	_paused = pause;
	if (_video)
		_video->pause(pause);
	for (uint i = 0; i < _layers.size(); i++) {
		if (!_viewPaused) // a clip the layout holds stays stopped
			_layers[i]->pause(pause);
	}
	_sound.pause(pause);
}

void XFilesEngine::pauseEngineIntern(bool pause) {
	Engine::pauseEngineIntern(pause);
	if (pause)
		debugC(1, kDebugScript, "-- game paused by ScummVM");
	// Before a game runs there is nothing to pause
	pauseGame(pause);
}

// -- The game -----------------------------------------------------------------

void XFilesEngine::createScript() {
	_script = new Script(*_data);
	_preferences.setScript(_script);
	_script->setHooks(this);
	_script->setOncePerProcess(&_onceFlags);
	_sound.attach(_data, _script);
	_script->setTexts(_texts);
	_script->setRandomSeed(_randomSource.getRandomNumber(0xFFFFFFFF));
}

void XFilesEngine::startNewGame() {
	// A new game starts from XFiles.gam, which allows the head credits
	// (XFiles.exe Game_Start 0x48cd70)
	_headCredits.setEnabled(true);
	flushPendingIcons();
	// No commit yet: Try again restarts from XFiles.gam
	_checkpointNew = true;
	_checkpoint = PendingLoad();
	_script->start();
}

void XFilesEngine::restoreGame(const VCSave &save) {
	flushPendingIcons();
	_script->restore(save);
	// The view's clip plays from the saved time (0x51d200), the head
	// credits roll again if they were rolling, from the last credit
	// shown (0x4a80a0); the dialog box that was open is created again
	if (save.clip)
		_pendingClipTime = save.clipTime;
	if (save.credits != -1) {
		_headCredits.setEnabled(true);
		_headCredits.roll(gameMillis(), getPreference(kPrefSkipIntro), save.credits);
	}
	VCCharView boxView;
	if (save.dialogBox && _data->getCharView(save.dialogBox, boxView))
		openDialogBox(boxView);
}

void XFilesEngine::runGame() {
	if (_pendingLoad.valid) {
		// A save chosen before the game (the start-up menu, save_slot)
		applyPendingLoad();
		if (!_script)
			return;
	} else {
		createScript();
		if (_loadedSave)
			restoreGame(_save);
		else
			startNewGame();
	}
	// Fast-forward (xfiles_fastforward): the steps run in the script alone,
	// as vc_sim.py runs them, so nothing plays and no time passes; the
	// game goes on from where they end
	const uint cardStarts = _captions.cardStarts();
	for (uint i = 0; i < _fastForward.size(); i++)
		_script->step(_fastForward[i]);
	_fastForward.clear();
	// A location card started by a skipped step is gone: no time passed (one from before them stays)
	if (_captions.cardStarts() != cardStarts)
		_captions.reset();
	// A clip whose end step was skipped is no longer playing in the script
	_sound.dropEndedClips();
	while (!shouldQuit()) {
		// A load queued by loadGameStream (from the global main menu, from the
		// steps) runs here, between two frames
		if (_pendingLoad.valid)
			applyPendingLoad();
		if (!_script)
			return;
		if (_script->isEndGamePending() && handleEndGame()) {
			if (_gameEnded || !_script)
				return;
			continue;
		}
		Input input = pollInput();
		if (_menuReopen)
			reopenMenu();
		// The X corner and Escape open the menu, not during a video
		if (input.escape && _menuLayout)
			escapeMenuLayout();
		else if (input.escape && !_menuOpen && _script->getVideoQueue().empty())
			openGameMenu();
		if (_menuOpen) {
			runMenuFrame(input);
			continue;
		}
		updateTimers();
		_sound.update(getPreference(kPref3DSound));
		_captions.update(gameMillis());
		{
			// The credit on screen goes when a video plays, the dialog box is open or a layout activated
			// (0x42b272); a credit due is held back by any layout but the Letter (0x442b50)
			bool canKeep = _script->getVideoQueue().empty() && !_dialogBox.isOpen() && !_interface->pausesView();
			_headCredits.update(gameMillis(), canKeep && !_interface->showsLayoutOtherThan(57082), canKeep);
		}
		// Screen +0x950 (B-010, 0x433e20): sticky. A queued clip's start recomputes it (0x510c30; a still gives 1); a view, the
		// location card, a head credit, interface items, a tool or the open kit set it (show events)
		{
			const Common::Array<uint32> &queue = _script->getVideoQueue();
			uint32 clip = queue.empty() ? 0 : queue[0];
			int tool = _script->heldToolType();
			bool shown = _captions.hasCard() || _headCredits.isShown() || _interface->showsLayoutOtherThan(57082) ||
						 (tool >= 1 && tool <= 4) || (_interface->isKitOpen() && _script->getKitIcons().size() >= 2);
			if (!clip)
				_compose = true; // the view is shown or resumed
			else if (clip != _composeClip) {
				// 0x488240 -> 0x433e20(screen, 0) at the clip's start; a still sets no sound +0x50 and the view stays
				// unsuspended, so 0x510c30 gives 1 (B-333)
				_compose = shown || isStillVideo(clip);
				debugC(1, kDebugScript, "-- compose %d at %u (clip %u)", _compose, gameMillis(), clip);
			} else if (shown)
				_compose = true; // a show event during the clip
			_composeClip = clip;
		}
		_subliminal.update(gameMillis(), *_script, getUberVariable(0), getUberVariable(1), getUberVariable(2), _compose);
		bool still = !_script->getVideoQueue().empty() && isStillVideo(_script->getVideoQueue()[0]);
		bool movie = !_script->getVideoQueue().empty() && !still;
		_movieCursor = movie;
		if (movie)
			_whiteHold = false;
		if (movie)
			runMovieFrame(input);
		else
			runViewFrame(input, still);
		present();
		updateShots();
		runSteps();
	}
}

void XFilesEngine::runMenuFrame(const Input &input) {
	updateMenu();
	hoverMenu(_mouse);
	if (input.click)
		menuChoice(clickMenu(input.pos));
	if (_menuOpen) {
		_cursors->show(0);
		_cursors->update(getPreference(kPrefTranslucency));
		drawMenu();
	}
	present();
	updateShots();
	runSteps();
}

void XFilesEngine::runMovieFrame(const Input &input) {
	// B-290: a movie does not block the main loop. Inside the video's rect a click goes only to a live .HOT
	// entry (B-031), else it is dropped; outside it the bar's icons and items take it. The X corner stays
	// off during a video (user decision 2026-10-14; the exe opens the menu there)
	if (input.click) {
		Common::Rect rect = movieRect();
		if (rect.isEmpty() || rect.contains(input.pos)) {
			if (_hotMovie == _video && hotKindAt(input.pos))
				handleClick(input.pos);
		} else if (_interface->hitTest(input.pos).kind != Interface::kHitNone) {
			handleClick(input.pos);
		}
	}
	if (!_script->getVideoQueue().empty())
		playQueuedVideo(input);
	// The bar, icon rolls and rollover text run under a movie; the rolls freeze inside the video's rect
	updateInterface(true);
	// The exe never hides the cursor for a movie: the refresh finds the playing video (B-285)
	updateCursor();
	// A playing movie: drawView does not run; the bar (open by the mouse, seen in the original 2026-11-09)
	// and the text layers still go over it
	_interface->drawBar(_screen, &_wide);
	_captions.drawCard(_screen);
	_wide.mark(WideLayout::kCard, _screen);
	_headCredits.draw(_screen);
	_wide.mark(WideLayout::kCredits, _screen);
	_subliminal.draw(_screen);
	_wide.mark(WideLayout::kSubliminal, _screen);
}

void XFilesEngine::runViewFrame(Input input, bool still) {
	// A queued still is shown over the view, which stays live (asset-play.md)
	if (still)
		updateQueuedStill();
	else if (_video->isOpen()) {
		releaseVideoHot();
		_video->close(); // Stopped by the script
	}
	syncView();
	updateInterface();
	updateViewPause();
	updateDialogBox();
	if (_whiteHold) {
		// 59824 plays with the script and the input held in the exe (movie.start wait=1): nothing answers
		// a click until its end starts the death clip (B-288)
		input.click = false;
		input.rightClick = false;
		input.release = false;
	}
	if (input.click)
		handleClick(input.pos);
	if (input.rightClick)
		handleRightClick(input.rightPos);
	if (input.release)
		handleRelease(input.releasePos);
	if (input.wheel)
		handleWheel(input.wheelPos, input.wheel);
	updateCursor();
	drawView();
	if (_whiteHold)
		_screen.fillRect(Common::Rect(_screen.w, _screen.h), _screen.format.RGBToColor(255, 255, 255));
}

void XFilesEngine::restartGame() {
	// New from the menu
	debugC(1, kDebugScript, "-- new game");
	if (!teardownGame())
		return;
	createScript();
	startNewGame();
}

void XFilesEngine::applyPendingLoad() {
	// Loading is Game_Start (0x48cd70) with a .X: the same teardown, then the
	// restore with the saved values over a fresh XFiles.gam
	debugC(1, kDebugScript, "-- load");
	PendingLoad load = _pendingLoad;
	_pendingLoad = PendingLoad();
	// A load is the commit Try again restarts from (the .X is copied to Temp.gam)
	_checkpoint = load;
	_checkpointNew = false;
	if (!teardownGame())
		return;
	createScript();
	_script->applySaved(load.values);
	restoreGame(load.save);
}

bool XFilesEngine::buildSave(PendingLoad &out) {
	if (!_script)
		return false;
	out.valid = false;
	fillSnapshot(out.save);
	// An icon still waiting for the reveal's timer is saved in the bar; the running game keeps waiting
	for (uint i = 0; i < _pendingIcons.size(); i++) {
		if (Common::find(out.save.inventory.begin(), out.save.inventory.end(), _pendingIcons[i].asset) == out.save.inventory.end())
			out.save.inventory.push_back(_pendingIcons[i].asset);
	}
	_script->getSaved(out.values);
	out.valid = true;
	return true;
}

void XFilesEngine::commitCheckpoint() {
	// GameState_Save commits the working database to Temp.gam
	if (buildSave(_checkpoint))
		_checkpointNew = false;
}

bool XFilesEngine::handleEndGame() {
	// 0x4cc870: the dialog box closes; a non-zero bReplay with Pref_bAutoRewind asks "Try again?"
	bool ask = _script->getEndGameValue() != 0 && getPreference(kPrefAutoRewind);
	bool again = false;
	if (ask) {
		if (!_steps.empty()) {
			// A scripted run has nobody to answer: the answer: step does
			if (_stepAnswer < 0)
				return false;
			again = _stepAnswer != 0;
			_stepAnswer = -1;
		} else {
			_dialogBox.close();
			GUI::MessageDialog question(_("Try again?"), _("Yes"), _("No"));
			again = question.runModal() == GUI::kMessageOK;
		}
	}
	if (_dialogBox.isOpen())
		_dialogBox.close();
	_script->answerEndGame(ask);
	// A load chosen in the GMM while the question was up wins: the frame top applies it
	if (_pendingLoad.valid)
		return true;
	const char *what = !ask ? "no question" : (again ? "try again" : "no");
	debugC(1, kDebugScript, "-- end game: %s", what);
	if (again) {
		// Yes reopens Temp.gam: the last commit, or a new game when there is none
		if (_checkpoint.valid) {
			_pendingLoad = _checkpoint;
			applyPendingLoad();
		} else {
			restartGame();
		}
		return true;
	}
	// No: g_gameLoaded = 0, the start-up menu (Game_Start kind 1)
	if (teardownGame())
		_gameEnded = true;
	return true;
}

bool XFilesEngine::teardownGame() {
	// Game_Start (0x48cd70) destroys every timer, stops the head credits and
	// the subliminal words, removes the sounds and videos and opens the state
	// database again, so the game starts from the unchanged XFiles.gam
	_whiteHold = false;
	VCData *savedData = nullptr;
	if (_loadedSave) {
		// The save is a modified copy of XFiles.gam: read the original before anything is deleted, so that a
		// failure leaves the game as it was
		savedData = _data;
		_data = nullptr;
		if (!loadDatabases(false)) {
			warning("Cannot load XFiles.gam again");
			delete _data;
			_data = savedData;
			quitGame();
			return false;
		}
		_loadedSave = false;
	}
	closeMenu();
	_menuOpen = false;
	_menuLayout = false;
	_menuReopen = false;
	_timerStates.clear();
	_timersPaused = false;
	_pressedItems.clear();
	_sound.clear();
	_video->close();
	_videoStarted = 0;
	closeView();
	_dialogBox.close();
	_dialogBoxView = 0;
	_captions.reset();
	_headCredits.reset();
	_headCredits.setEnabled(false); // only a new game, or a restore with the credits rolling, allows them
	_subliminal.stop();
	_compose = true; // start-up and the menu set the flag (0x4903fb, 0x44e204)
	_composeClip = 0;
	_interface->setSuspended(false);
	_lastClickTime = 0;
	_dragIdea = 0;
	_lastClickHit.kind = Interface::kHitNone;
	_lastClickHit.id = 0;
	_hover = _lastClickHit;
	_pendingClipTime = -1;
	_shotDue = 0;
	_shotView = 0;
	_shotBox = false;
	_waitVideo = 0;
	_waitTimer = -1;
	// A tool reaction due after the click and a bomb clip's range belong to the game that goes
	_delayedPlays.clear();
	_assetRanges.clear();
	// Whatever asked for the pause (the menu) is gone with the game
	_pauseDepth = _paused ? 1 : 0;
	pauseGame(false);

	_sound.attach(nullptr, nullptr);
	delete _script;
	_script = nullptr;
	_preferences.setScript(nullptr);
	delete _interface;
	_interface = nullptr;
	delete _cursors;
	_cursors = nullptr;
	delete savedData;
	_cursors = new Cursors(*_data, _discFiles);
	_cursors->init(_screen.format);
	_interface = new Interface(*_data, _discFiles);
	_interface->setFormat(_screen.format);
	_interface->init(_texts);
	return true;
}

// -- C++ functions ------------------------------------------------------------

bool XFilesEngine::callFunction(const HookCall &call) {
	return _hookTable.run(*this, call) == kHookHandled;
}

void XFilesEngine::addHooks() {
	HookTable<XFilesEngine> &t = _hookTable;
	t.add("SaveGameState", [](XFilesEngine &e, const HookCall &c) {
		e.commitCheckpoint();
		return true;
	});
	t.add("StartVideoText", [](XFilesEngine &e, const HookCall &c) {
		e.startVideoText(c.action, c.args);
		return true;
	});
	// The layouts' buttons unshow the layout, then these create the menu object again (MainMenu_ctor)
	t.add("ReturnMainMenu", [](XFilesEngine &e, const HookCall &c) {
		e.savePreferences();
		e._menuReopen = e._menuLayout;
		return e._menuLayout;
	});
	t.add("ReturnFromHelp", [](XFilesEngine &e, const HookCall &c) {
		e._menuReopen = e._menuLayout;
		return e._menuLayout;
	});
	t.add("RegPreferences", [](XFilesEngine &e, const HookCall &c) {
		e.registerPreferences(c.action);
		return true;
	});
	t.add("RollHdCredits", [](XFilesEngine &e, const HookCall &c) {
		e._headCredits.roll(e.gameMillis(), e.getPreference(kPrefSkipIntro));
		return true;
	});
	t.add("SetStartView", [](XFilesEngine &e, const HookCall &c) {
		e.applyPreferences(false); // Game_BuildObjects 0x477940 calls Prefs_Apply 0x452b20
		return true;
	});
	t.add("RegUberVars", [](XFilesEngine &e, const HookCall &c) {
		e.registerUberVariables(c.action);
		return true;
	});

	// The inventory bar and the evidence kit; RegCurrInvVar, OnInvClick and OnIntInvClick are handled by Script
	// (iCurrInventory)
	t.add("RollOnEvKit", [](XFilesEngine &e, const HookCall &c) {
		if (!c.has(2))
			return false;
		// 0x4efda0: nothing while that item is in hand; open the kit above
		// the item's icon, or close it once the mouse has left it
		Common::Rect from;
		if ((uint32)c.args[0] == e._interface->getHeld())
			return true;
		if (c.args[1] && e._interface->getInventoryRect(c.args[0], &from))
			e._interface->openKit(from);
		else if (!c.args[1] && !e._interface->isMouseInKit())
			e._interface->closeKit();
		return true;
	});
	t.add("EvKitIconClick", [](XFilesEngine &e, const HookCall &c) {
		// 0x4efc90: the kit is taken in hand; the hidden bar closes
		e._interface->closeKit();
		if (e._preferences.variable(kPrefInventory) && e._script->getVariable(e._preferences.variable(kPrefInventory)) == 'H')
			e._interface->closeBar();
		return true;
	});
	t.add("DisplayDBox", [](XFilesEngine &e, const HookCall &c) {
		// The Script hook has opened or closed the box's script side
		if (!c.has(1))
			return false;
		VCCharView charView;
		if (c.args[0] == 0) {
			e._dialogBox.close();
			return true;
		}
		if (!e._data->getCharView(c.args[0], charView))
			return false;
		e.openDialogBox(charView, false);
		return true;
	});
	t.add("AddIFLExcept", [](XFilesEngine &e, const HookCall &c) {
		if (!c.has(1))
			return false;
		e._interface->addModalException(c.args[0]);
		return true;
	});
	t.add("ClearIFLExcept", [](XFilesEngine &e, const HookCall &c) {
		e._interface->clearModalExceptions();
		return true;
	});
	t.add("TextOn", [](XFilesEngine &e, const HookCall &c) {
		// 0x5078f0: for the head credits and the subliminal object, 0 pauses (0x46c970: stop the timers, hide),
		// else resumes (0x49fae0)
		if (!c.has(1))
			return false;
		e.pauseTimedText(!c.args[0]);
		return true;
	});
	t.add("FadeBack", [](XFilesEngine &e, const HookCall &c) {
		// 0x48a9f0 is Scene_SuspendObjects (0x40be40): a suspend flag on the view, icons, interface items and
		// inventory, which take no input meanwhile and are dimmed (Interface::draw, drawView)
		if (!c.has(1))
			return false;
		e._interface->setSuspended(c.args[0] != 0);
		return true;
	});
	t.add("FadeToBlack", [](XFilesEngine &e, const HookCall &c) {
		e.fadeToBlack();
		return true;
	});
}

/** FadeToBlack holds the black screen for 2000 ms (0x5521c0). */

void XFilesEngine::registerPreferences(const VCAction &action) {
	_preferences.registerVariables(action);
}

bool XFilesEngine::queryPreference(uint index, int32 &value) const {
	return _preferences.query(index, value);
}

void XFilesEngine::savePreferences() {
	_preferences.save();
}

void XFilesEngine::applyPreferences(bool resume) {
	// Prefs_Apply also sets the captions, the inventory bar and the translucency; this port reads those from the
	// preference variables where they are used.
	// Prefs_ApplyAIIcon (0x452780): Artificial Intuition on makes the AI icon if there is none; off removes it
	// and every action icon there is (their deactivation clears their enable flags, 0x4c0610). At SetStartView
	// no action icon exists yet: Game_BuildObjects (0x477940) makes them after Prefs_Apply, whatever the
	// preference
	bool intuition = _script->getPreferenceValue(kPrefArtificialIntuition) != 0;
	if (_interface->showAIIcon(intuition)) {
		_script->aiIconShown();
		_script->recheckAIIcon();
	}
	if (!intuition && resume)
		_script->removeActionIcons();
	// Smooth Text (SPI_SETFONTSMOOTHING, and the captions' text mode): the engine's own text is anti-aliased or
	// monochrome; the fonts reload when it changes
	bool smooth = getPreference(kPrefSmoothText);
	_captions.setSmooth(smooth);
	_dialogBox.setSmooth(smooth);
	_interface->setSmooth(smooth);
	_headCredits.setSmooth(smooth);
	_subliminal.setSmooth(smooth);
	if (_counterFontOwned)
		delete _counterFont;
	_counterFont = nullptr;
	_counterFontOwned = false;
	syncSoundSettings();
	bool subliminals = getPreference(kPrefSubliminalMessages);
	if (subliminals && !_subliminal.isRunning())
		_subliminal.start(gameMillis());
	else if (!subliminals && _subliminal.isRunning())
		_subliminal.stop();
}

int XFilesEngine::wantedWideDx() const {
	// B-352: a scripted run keeps the 4:3 canvas (fixtures and goldens); xfiles_widescreen_scripted=true lets a
	// headless check shoot the wide canvas
	const bool scriptedOk = ConfMan.hasKey("xfiles_widescreen_scripted") && ConfMan.getBool("xfiles_widescreen_scripted");
	if ((ConfMan.get("xfiles_steps").empty() || scriptedOk) && ConfMan.hasKey("xfiles_widescreen") && ConfMan.getBool("xfiles_widescreen"))
		return (kWideWidth - kScreenWidth) / 2;
	return 0;
}

void XFilesEngine::applyWidescreen(const Graphics::PixelFormat *format) {
	const int dx = wantedWideDx();
	if (format) {
		// First call, from run: the canvas is created with the chosen pixel format
		_wideDx = dx;
		initGraphics(_wideDx ? kWideWidth : kScreenWidth, kScreenHeight, format);
		if (_wideDx)
			g_system->fillScreen(0);
		return;
	}
	// B-356: the option changed while the game runs; the canvas is resized with the format it already has
	if (dx == _wideDx)
		return;
	const Graphics::PixelFormat keep = _screen.format;
	_wideDx = dx;
	// The stale wide state: no zoomed band, no recorded rects (they are canvas coordinates)
	_bandCaptured = false;
	_bandZoomed = false;
	_bandPass.free();
	_wide.reset();
	_canvasMouse = Common::Point();
	_canvasMouseScreen = Common::Point(-1, -1);
	initGraphics(_wideDx ? kWideWidth : kScreenWidth, kScreenHeight, &keep);
	g_system->fillScreen(0);
	// The mouse stays on the same screen point; the frame is redrawn from the current screen at once
	g_system->warpMouse(_mouse.x + _wideDx, _mouse.y);
	present();
}

void XFilesEngine::applyGameSettings() {
	// ScummVM's menu closed (kSupportsChangingOptionsDuringRuntime): its captions and volumes into the game
	// (syncSoundSettings follows it)
	_preferences.syncFromScummVM();
	applyWidescreen();
}

void XFilesEngine::syncSoundSettings() {
	Engine::syncSoundSettings();
	// 0x469f50 sets every wave-out device to the game volume (0 to 100)
	int32 game = _preferences.gameVolume();
	static const Audio::Mixer::SoundType types[] = {
		Audio::Mixer::kPlainSoundType, Audio::Mixer::kMusicSoundType, Audio::Mixer::kSFXSoundType,
		Audio::Mixer::kSpeechSoundType
	};
	for (uint i = 0; i < ARRAYSIZE(types); i++)
		_mixer->setVolumeForSoundType(types[i], _mixer->getVolumeForSoundType(types[i]) * game / 100);
}

void XFilesEngine::registerUberVariables(const VCAction &action) {
	// RegUberVars(iUberLoss, iUberParanoia, iUberXTrack), 0x48ba50: the
	// subliminal words read them (0x468cf0)
	for (uint i = 0; i < action.args.size() && i < ARRAYSIZE(_uberVariables); i++)
		_uberVariables[i] = action.args[i].kind == kOperandVariable ? action.args[i].value : 0;
	if (_script)
		_script->registerUberParanoia(_uberVariables[1]);
}

int32 XFilesEngine::getUberVariable(uint index) const {
	return _uberVariables[index] ? _script->getVariable(_uberVariables[index]) : 0;
}

bool XFilesEngine::getPreference(uint index) const {
	return _preferences.isSet(index);
}

void XFilesEngine::startVideoText(const VCAction &action, const Common::Array<int32> &args) {
	// StartVideoText(asset, iDayNight, iCurrTime, bWithTime): the asset's
	// name is the location
	VCAsset asset;
	Common::U32String location;
	if (args.size() < 4 || !_data->getAsset(args[0], asset) || !_data->getName(asset.name, location)) {
		warning("StartVideoText: bad arguments in #%u", action.id);
		return;
	}
	_captions.startCard(Captions::formatCard(location, args[1], args[2], args[3] == 1), gameMillis());
}

} // End of namespace XFiles
