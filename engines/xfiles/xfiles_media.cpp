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

#include "audio/mixer.h"
#include "common/config-manager.h"
#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "graphics/pixelformat.h"

#include "xfiles/xfiles.h"
#include "xfiles/media/movie.h"
#include "xfiles/script/script.h"
#include "xfiles/script/vcdata.h"

namespace XFiles {

static const uint32 kFadeBlackHold = 2000;

void XFilesEngine::fadeToWhite() {
	// Nothing redraws the screen after the fade in the exe until the next video frame (59824 is sound only, the
	// death clip 23412 follows its end): the white stays (B-287)
	fadeToColor(255, 255, 255, 0);
	_whiteHold = true;
}

void XFilesEngine::fadeToBlack() {
	fadeToColor(0, 0, 0, kFadeBlackHold);
}

void XFilesEngine::fadeToColor(byte cr, byte cg, byte cb, uint32 holdMs) {
	// 0x4ccb50: 0x5103b0 fades the whole screen (interface bar too) to colour 0x12 (black; 500 ms, then 1000 ms in
	// 8 steps: read as start delay and length, unverified), then 2000 ms black, with the script held. The exe also
	// flags the object from 0x5196b0 meanwhile (0x4a0480, not identified): left out. Nothing undoes the black but
	// the next frame. A step list and a fast-forward have no time: the order only
	static const uint32 kStartDelay = 500;
	static const uint32 kFadeLength = 1000;
	static const uint32 kFadeSteps = 8;
	debugC(1, kDebugScript, "-- fade to colour %02x%02x%02x", cr, cg, cb);
	_whiteHold = false;
	if (!_steps.empty() || !_fastForward.empty())
		return;
	Graphics::Surface shot;
	shot.copyFrom(_screen);
	uint32 start = g_system->getMillis();
	uint32 total = kStartDelay + kFadeLength + holdMs;
	uint32 lastStep = 0;
	while (!shouldQuit()) {
		uint32 elapsed = g_system->getMillis() - start;
		if (elapsed >= total)
			break;
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
		}
		// The sounds go on under the fade (AlienBlast: 59824 plays while the screen fades, exe.log sound.start
		// before the hook's cpp line); they only feed their audio from here (B-284)
		_sound.update(getPreference(kPref3DSound));
		uint32 step = 0; // 0: the picture as it was, kFadeSteps: black
		if (elapsed >= kStartDelay + kFadeLength)
			step = kFadeSteps;
		else if (elapsed > kStartDelay)
			step = (elapsed - kStartDelay) * kFadeSteps / kFadeLength;
		if (step != lastStep || elapsed == 0) {
			lastStep = step;
			uint32 scale = kFadeSteps - step;
			const Graphics::PixelFormat &fmt = _screen.format;
			for (int y = 0; y < shot.h; y++) {
				for (int x = 0; x < shot.w; x++) {
					byte r, g, b;
					fmt.colorToRGB(shot.getPixel(x, y), r, g, b);
					_screen.setPixel(x, y, fmt.RGBToColor((r * scale + cr * step) / kFadeSteps, (g * scale + cg * step) / kFadeSteps, (b * scale + cb * step) / kFadeSteps));
				}
			}
		}
		present();
	}
	shot.free();
}

// -- Sound, video and timers --------------------------------------------------

bool XFilesEngine::readAssetFile(uint32 id, Common::Array<byte> &data) {
	// A file asset on the hard disk or a disc (an e-mail's text, XT:<id>.xtx)
	VCAsset asset;
	if (!_data->getAsset(id, asset) || asset.files.empty())
		return false;
	Common::SeekableReadStream *file = _discFiles.openFile(asset.files[0].disc, asset.files[0].file);
	if (!file)
		return false;
	data.resize(file->size());
	bool ok = data.empty() || file->read(&data[0], data.size()) == data.size();
	delete file;
	return ok;
}

int XFilesEngine::measureText(const Common::U32String &text, int size, bool bold, int face) const {
	return _interface ? _interface->measureItemText(text, size, bold, face) : -1;
}

void XFilesEngine::playAsset(uint32 id, byte mode, bool positional, int16 x, int16 y) {
	_sound.play(id, mode, positional, x, y, getPreference(kPref3DSound));
}

void XFilesEngine::stopViewAsset(uint32 id) {
	// 0x504c90: play once, then go to the segment end (else the movie end); the layer holds that frame
	for (uint i = 0; i < _layers.size(); i++) {
		if (_layers[i]->getAssetId() == id) {
			_layers[i]->stop();
			_layers[i]->showStillAtEnd();
		}
	}
}

void XFilesEngine::playPhoneTone(uint32 id) {
	_sound.playTone(id);
}

void XFilesEngine::playShutter(uint32 id) {
	// A bare movie of the camera's asset, stopped by the next shutter (no sound object)
	_sound.playTone(id);
}

void XFilesEngine::playGunShot(uint32 id, uint32 remark) {
	// A bare movie of the gun's asset, restarted by the next click (0x4cb63c stop, 0x4cb649 start); a remark follows its end
	_sound.playTone(id, remark);
}

uint32 XFilesEngine::getPhotoTime() const {
	if (_video && _video->isOpen())
		return _video->getTime();
	return !_layers.empty() ? _layers[0]->getTime() : 0;
}

void XFilesEngine::pauseTimedText(bool pause) {
	// 0x5078f0: 0 pauses the head credits and the subliminal object (0x46c970: stop the timers, hide), else resumes (0x49fae0)
	if (pause) {
		_headCredits.pause();
		_subliminal.pause();
	} else {
		_headCredits.resume(gameMillis());
		_subliminal.resume(gameMillis());
	}
}

bool XFilesEngine::getViewClipTime(uint32 &ms) const {
	// 0x51cf10: the view's clip, the first layer that is not a still; an ended one: no swap
	for (uint i = 0; i < _layers.size(); i++) {
		VCAsset asset;
		if (!_layers[i]->isOpen() || !_data->getAsset(_layers[i]->getAssetId(), asset) || (asset.flags & kAssetStill))
			continue;
		if (_layers[i]->hasEnded())
			return false;
		ms = _layers[i]->getTime();
		return true;
	}
	// No clip layer: the special views always have one in the exe, so this is a clip that did not open (no media)
	ms = 0;
	return true;
}

bool XFilesEngine::getVideoTime(uint32 id, uint32 &ms) const {
	if (!_video || !_video->isOpen() || _video->getAssetId() != id || _video->hasEnded())
		return false;
	ms = _video->getTime();
	return true;
}

void XFilesEngine::restoreSound(uint32 id, bool loop, uint32 startMs) {
	_sound.restore(id, loop, startMs);
}

void XFilesEngine::playClip(uint32 id, int16 x, int16 y) {
	_sound.playClip(id, x, y);
}

void XFilesEngine::stopAllSounds() {
	_sound.stopAll();
}

void XFilesEngine::pauseTimers() {
	// 0x48e640(1) on the timer list: each running timer is killed, and a
	// one-shot one keeps what was left of its delay (0x52f030; none left:
	// the delay stays). The PDA's own timers start after this and run
	uint32 now = gameMillis();
	const Common::HashMap<uint, Script::Timer> &timers = _script->getTimers();
	for (Common::HashMap<uint, Script::Timer>::const_iterator it = timers.begin(); it != timers.end(); ++it) {
		if (!_timerStates.contains(it->_key) || _timerStates[it->_key].serial != it->_value.serial)
			continue;
		TimerState &state = _timerStates[it->_key];
		uint32 elapsed = now - state.started;
		if (!state.paused && !it->_value.periodic && elapsed < state.ms)
			state.ms -= elapsed;
		state.paused = true;
	}
	_timersPaused = true;
}

void XFilesEngine::drawCaptions(const Movie &movie, const Common::Point &pos) {
	if (!getPreference(kPrefClosedCaptioning))
		return;
	// Text tracks sit below the picture (track matrix y 240), as wide as the video
	_captions.drawCaption(_screen, movie.getCaption(), pos.x, pos.y + movie.getHeight(), movie.getWidth());
}

bool XFilesEngine::isStillVideo(uint32 id) const {
	VCAsset asset;
	return _data->getAsset(id, asset) && (asset.flags & kAssetStill);
}

void XFilesEngine::updateQueuedStill() {
	// A still asset (flag 0x40) is the sound object showing the player's frame
	// until a one-shot 3000 ms timer hides it (SoundObj_OnShow 0x44f260); the
	// view and its clicks go on meanwhile, skip does nothing
	static const uint32 kStillMillis = 3000;
	uint32 id = _script->getVideoQueue()[0];
	if (!_video->isOpen() || _video->getAssetId() != id) {
		if (!openMovie(*_video, id) || !_video->showStill()) {
			_video->close();
			_script->finishVideo();
			return;
		}
		_videoStarted = gameMillis();
	}
	if (gameMillis() - _videoStarted >= kStillMillis) {
		_video->close();
		_script->finishVideo();
	}
}

void XFilesEngine::playQueuedVideo(const Input &input) {
	uint32 id = _script->getVideoQueue()[0];
	if (!_video->isOpen() || _video->getAssetId() != id) {
		closeView();
		if (!openMovie(*_video, id)) {
			_script->finishVideo();
			return;
		}
		if (uint32 end = _script->getVideoEnd())
			_video->setEnd(end);
		_video->play(_script->isVideoLooped(), _script->getVideoStart() ? (int32)_script->getVideoStart() : -1);
		_videoStarted = gameMillis();
		// The clip's .HOT file: its entries open and close by the clip's time, as for a view clip (B-031)
		VCAsset asset;
		if (_data->getAsset(id, asset) && _hot.load(_discFiles, asset, _script->getPreferenceValue(kPrefActionSceneDifficulty)))
			_hotMovie = _video;
	}
	// A piece of the view's loop between conversation clips plays for a
	// fixed time (Script::queueViewLoop); a looping clip plays until a Stop
	// action stops it
	uint32 limit = _script->getVideoLimit();
	if ((input.skip && !_script->isVideoLooped()) || (limit && gameMillis() - _videoStarted >= limit))
		_video->stop();
	_video->update();

	clearScreen();
	Common::Point pos = getBandPosition(*_video);
	blit(_video->getFrame(), pos.x, pos.y);
	// A playing movie is drawn untinted under SetLBoxTint (B-309); a tool change still latches the tint
	relatchBandTint();
	drawCaptions(*_video, pos);

	if (_video->hasEnded()) {
		releaseVideoHot();
		_video->close();
		_script->finishVideo();
	}
}

void XFilesEngine::releaseVideoHot() {
	if (_hotMovie == _video) {
		_hot.clear();
		_hotMovie = nullptr;
	}
}

void XFilesEngine::updateTimers() {
	// Timers run in real time; Script only knows they were started
	uint32 now = gameMillis();
	// 0x4a4fa0, after each pass of the event loop: once no layout is shown,
	// the timers the PDA paused start again (0x48e640(0))
	if (_timersPaused && _script->getLayouts().empty()) {
		for (Common::HashMap<uint, TimerState>::iterator it = _timerStates.begin(); it != _timerStates.end(); ++it) {
			if (it->_value.paused) {
				it->_value.started = now;
				it->_value.paused = false;
			}
		}
		_timersPaused = false;
	}
	const Common::HashMap<uint, Script::Timer> &timers = _script->getTimers();
	Common::Array<Common::Pair<uint, uint32>> expired; // number, serial
	Common::Array<uint> stopped;
	for (Common::HashMap<uint, Script::Timer>::const_iterator it = timers.begin(); it != timers.end(); ++it) {
		if (!_timerStates.contains(it->_key) || _timerStates[it->_key].serial != it->_value.serial) {
			TimerState state = { it->_value.serial, now, it->_value.ms, false };
			_timerStates[it->_key] = state;
		} else if (!_timerStates[it->_key].paused && now - _timerStates[it->_key].started >= _timerStates[it->_key].ms) {
			expired.push_back(Common::Pair<uint, uint32>(it->_key, it->_value.serial));
		}
	}
	for (Common::HashMap<uint, TimerState>::const_iterator it = _timerStates.begin(); it != _timerStates.end(); ++it) {
		if (!timers.contains(it->_key))
			stopped.push_back(it->_key);
	}
	for (uint i = 0; i < stopped.size(); i++)
		_timerStates.erase(stopped[i]);

	// The phone's buttons go up 100 ms after they were pressed (0x4c8870)
	Common::Array<uint32> pressed;
	_script->takePressedItems(pressed);
	for (uint i = 0; i < pressed.size(); i++) {
		PressedItem item = { pressed[i], now };
		_pressedItems.push_back(item);
	}
	for (uint i = 0; i < _pressedItems.size(); ) {
		if (now - _pressedItems[i].since >= kButtonUpDelay) {
			uint32 id = _pressedItems[i].id;
			_pressedItems.remove_at(i);
			_script->releaseItem(id);
		} else {
			i++;
		}
	}

	for (uint i = 0; i < _delayedPlays.size(); ) {
		if ((int32)(now - _delayedPlays[i].due) >= 0) {
			uint32 asset = _delayedPlays[i].asset;
			_delayedPlays.remove_at(i);
			debugC(1, kDebugScript, "-- delayed play of #%u", asset);
			_script->playReaction(asset);
		} else {
			i++;
		}
	}

	// The reveal's timers (deferInventoryIcon): the icon joins, the bar hides
	for (uint i = 0; i < _pendingIcons.size();) {
		if ((int32)(now - _pendingIcons[i].due) >= 0) {
			uint32 id = _pendingIcons[i].asset;
			_pendingIcons.remove_at(i);
			// (an `icon:` step may have added it already)
			const Common::Array<uint32> &bar = _script->getInventory();
			if (Common::find(bar.begin(), bar.end(), id) == bar.end()) {
				debugC(1, kDebugScript, "-- icon #%u joins the bar", id);
				_script->addInventoryIcon(id);
			}
		} else {
			i++;
		}
	}
	if (_barRevealed && (int32)(now - _barRevealEnd) >= 0)
		_barRevealed = false;

	for (uint i = 0; i < expired.size(); i++) {
		uint n = expired[i].first;
		// An earlier expiry may have stopped or restarted this timer (a new serial): the restarted one is not due
		if (!timers.contains(n) || timers.getVal(n).serial != expired[i].second)
			continue;
		debugC(1, kDebugScript, "-- timer %u expired", n);
		_timerStates.erase(n);
		if ((int)n == _waitTimer)
			_waitTimer = -1;
		// Icons are left alone: the data disables them when a choice times out
		_script->expireTimer(n);
	}
}

} // End of namespace XFiles
