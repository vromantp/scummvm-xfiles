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
#include "common/textconsole.h"

#include "xfiles/detection.h"
#include "xfiles/script/script.h"
#include "xfiles/script/script_internal.h"
#include "xfiles/script/script_trace.h"

namespace XFiles {

static const char *const kModeNames[] = { "preload", "play_once", "play_loop", "stop", "unload" };

// -- Media --------------------------------------------------------------------

void Script::playAsset(uint32 id, byte mode, const VCAction *sound3D) {
	VCAsset a;
	if (!_data.getAsset(id, a)) {
		warning("Script: #%u is not an asset", id);
		return;
	}
	if (mode == kModePlayOnce || mode == kModePlayLoop)
		cover('S', id);
	// Action_DoAsset 0x47a4e0 drops a play (once or loop) of an asset that still has a sound object (0x491380),
	// until a stop or the next main-loop pass after its end removes it. Only where sound ends are known
	if (_media.soundEnds && (mode == kModePlayOnce || mode == kModePlayLoop) && isPlaying(id))
		return;
	// Without known sound ends nothing drops the play, so the stopped sound ends before it restarts
	if (!_media.soundEnds && (mode == kModePlayOnce || mode == kModePlayLoop) && Common::find(_media.pendingStops.begin(), _media.pendingStops.end(), id) != _media.pendingStops.end()) {
		_media.pendingStops.erase(Common::find(_media.pendingStops.begin(), _media.pendingStops.end(), id));
		endSound(id, "stopped");
		endClip(id, "stopped");
	}
	bool video = isVideoAsset(a);
	Common::String file = a.files.empty() ? Common::String() : formatFile(a.files[0]);

	// XFiles.exe makes a sound object for every asset it plays (Action_DoAsset
	// 0x47a4e0) and treats one whose asset is an .xmv or .nmv (types 0x17 and
	// 0x16, 0x444d00) the same whatever its mode or range: its movie starts in
	// 0x488240, which raises the hold on views and on other sound objects'
	// play events (0x44f860), so these clips play one after another, and its
	// end or stop deactivates the sound and its asset object (0x43a970).
	// The .nmv clips (ranges of a view's navigation movie) are queued like
	// the .xmv ones: the media layer plays the file entry's time range. A still
	// asset (flag 0x40) is queued the same way; the engine shows its frame for
	// 3000 ms with the view live (asset-play.md "A still asset plays for 3 s")
	if (video && (mode == kModePlayOnce || mode == kModePlayLoop)) {
		queueVideo(id, 0, mode == kModePlayLoop);
	} else if (mode == kModePlayOnce || mode == kModePlayLoop) {
		Common::String where = sound3D ? Common::String::format(" at (%d, %d)", sound3D->x, sound3D->y) : Common::String();
		log(Common::String::format("      %s #%u ", kModeNames[mode], id) + file + where + "  \"" + nameOrNone(_data, a.name) + "\"");
		// A sound (.amv, .mus, .wav) ends on its own; its end or a stop
		// deactivates its asset object too (0x43a970): see endSound()
		// A sound already playing shares its asset object: no second activation
		bool xs = !a.files.empty() && a.files[0].file.hasPrefix("XS:");
		bool fresh = xs && !_media.sounds.contains(id);
		if (xs)
			_media.sounds[id] = true;
		if (!_dlg.clipsActivated) {
			if (fresh)
				activateAsset(id);
			else if (!xs)
				fire(id, kEventObjectActivation);
		}
		// The sound object's event is held while a video plays (0x433000,
		// raised by 0x44f860), so the sound starts after the videos, in
		// order; its asset object is active at once (media2.log: 30399
		// 'asset.start' 42996, 'movie.start' 49706, after the video's end)
		HeldSound held = { id, mode, sound3D != nullptr, sound3D ? sound3D->x : 0, sound3D ? sound3D->y : 0 };
		if (!_media.videoQueue.empty())
			_media.heldSounds.push_back(held);
		else if (_hooks)
			_hooks->playAsset(held.id, held.mode, held.positional, held.x, held.y);
	} else if (mode == kModeStop) {
		log(Common::String::format("      stop #%u ", id) + file);
		for (uint i = _media.heldSounds.size(); i-- > 0; ) {
			if (_media.heldSounds[i].id == id)
				_media.heldSounds.remove_at(i);
		}
		// A sound object stays (isPlaying) until a later pass; its deactivation follows the event (flushStops).
		// Without one, the stop runs a shown view asset to its end, with no deactivation (0x504c90)
		bool hasObject = isPlaying(id);
		if (_hooks) {
			_hooks->playAsset(id, mode, false, 0, 0);
			if (!hasObject)
				_hooks->stopViewAsset(id);
		}
		if ((_media.sounds.contains(id) || _media.clips.contains(id)) && Common::find(_media.pendingStops.begin(), _media.pendingStops.end(), id) == _media.pendingStops.end())
			_media.pendingStops.push_back(id);
		// A video stopped while it plays ends; one still waiting never starts
		for (uint i = _media.videoQueue.size(); i-- > 0; ) {
			if (_media.videoQueue[i] != id) {
				continue;
			} else if (i == 0) {
				endVideo("stopped");
			} else {
				// Nothing deactivates it, so its use of the asset object ends without an event
				if (_media.videoPlays[i].activated && !_media.videoPlays[i].viewLoop)
					deactivateAsset(id, false);
				_media.videoQueue.remove_at(i);
				_media.videoPlays.remove_at(i);
			}
		}
	}
}

void Script::releaseHeldSounds() {
	Common::Array<HeldSound> held;
	held.swap(_media.heldSounds);
	for (uint i = 0; i < held.size(); i++) {
		if (_hooks)
			_hooks->playAsset(held[i].id, held[i].mode, held[i].positional, held[i].x, held[i].y);
	}
}

void Script::endSound(uint32 id, const char *how) {
	if (!_media.sounds.contains(id))
		return;
	_media.sounds.erase(id);
	log(Common::String::format("  << sound #%u %s", id, how));
	deactivateAsset(id);
	_aiSoundEndPending = true;
}

bool Script::isPlaying(uint32 id) const {
	return _media.sounds.contains(id) || _media.clips.contains(id) || Common::find(_media.videoQueue.begin(), _media.videoQueue.end(), id) != _media.videoQueue.end();
}

void Script::playAssetAt(uint32 asset, int x, int y) {
	// 0x491de0: starts the asset as a Do Asset action would, unless it is already playing (0x491380).
	if (isPlaying(asset))
		return;
	// An XG clip is drawn once with its top-left at (x, y) over the screen, without a hold; its end fires
	// the asset's Object Deactivation (ing-search run, B-071). Other assets play as a Do Asset action does
	VCAsset a;
	if (_data.getAsset(asset, a) && !a.files.empty() && a.files[0].file.hasPrefix("XG:"))
		startClip(a, x, y);
	else
		playAsset(asset, kModePlayOnce, nullptr);
}

void Script::startClip(const VCAsset &a, int x, int y) {
	log(Common::String::format("      %s #%u ", kModeNames[kModePlayOnce], a.id) + formatFile(a.files[0]) + "  \"" + nameOrNone(_data, a.name) + "\"");
	_media.clips[a.id] = true;
	activateAsset(a.id);
	if (_hooks)
		_hooks->playClip(a.id, x, y);
}

void Script::endClip(uint32 id, const char *how) {
	if (!_media.clips.contains(id))
		return;
	_media.clips.erase(id);
	log(Common::String::format("  << clip #%u %s", id, how));
	deactivateAsset(id);
	_aiSoundEndPending = true;
}

void Script::stopEnvSounds() {
	// 0x4cc9c0 queues every playing asset without a picture for deactivation:
	// its Object Deactivation fires as at a stop (0x43a970). _media.sounds holds
	// the audio-only assets (XS); clips with a picture keep playing. The
	// players stop first, so a sound a trigger starts is not cut
	if (_hooks)
		_hooks->stopAllSounds();
	_media.heldSounds.clear();
	Common::Array<uint32> ids;
	for (Common::HashMap<uint32, bool>::const_iterator it = _media.sounds.begin(); it != _media.sounds.end(); ++it)
		ids.push_back(it->_key);
	Common::sort(ids.begin(), ids.end());
	for (uint i = 0; i < ids.size(); i++)
		endSound(ids[i], "stopped");
}

void Script::regPreferences(const VCAction &a) {
	// 0x4ccda0 (mode 5) writes the registry's values into the 24 preference
	// variables. The registry is the player's settings or setPreference();
	// a preference neither knows keeps its .gam value, which is what the exe
	// stores on its first start (no registry key: 0x4ccda0 mode 0)
	if (a.args.size() > kPrefArtificialIntuition && a.args[kPrefArtificialIntuition].kind == kOperandVariable)
		_prefArtificialIntuition = a.args[kPrefArtificialIntuition].value;
	for (uint index = 0; index < a.args.size() && index < kPrefCount; index++) {
		if (a.args[index].kind != kOperandVariable)
			continue;
		_prefVariables[index] = a.args[index].value;
		// What a Preferences screen hook stored (PrefHooks) or setPreference() set is the registry's value
		int32 value = 0;
		if (_preferences.contains(index))
			_variables[a.args[index].value] = _preferences[index];
		else if (_hooks && _hooks->queryPreference(index, value))
			_variables[a.args[index].value] = value;
	}
}

int32 Script::getPreferenceValue(uint index) const {
	if (_preferences.contains(index))
		return _preferences[index];
	return _prefVariables[index] ? getVariable(_prefVariables[index]) : 0;
}

void Script::loadPreferences() {
	// 0x4310f0: walk the title's triggers and run the first C++ Function
	// action named RegPreferences, whatever its event or condition. The
	// reference log shows it as "cpp from=0043137e"; compare.py leaves it out
	VCTitle title;
	if (!_data.getTitle(title))
		return;
	Common::Array<uint32> triggers;
	_data.getMembers(title.triggers, triggers);
	for (uint i = 0; i < triggers.size(); i++) {
		VCTrigger t;
		if (!_data.getTrigger(triggers[i], t))
			continue;
		for (uint j = 0; j < t.actions.size(); j++) {
			VCAction a;
			if (!_data.getAction(t.actions[j], a) || a.type != kActionFunction || a.function != "RegPreferences")
				continue;
			regPreferences(a);
			if (_hooks) {
				Common::Array<int32> args;
				for (uint k = 0; k < a.args.size(); k++)
					args.push_back(getValue(a.args[k], EventFields(), LocalVariables()));
				_hooks->callFunction(HookCall(a, args, -1, EventFields()));
			}
			return;
		}
	}
}

void Script::queueVideo(uint32 id, uint32 limit, bool loop, bool viewLoop) {
	// A piece of the view's loop is the view's shown asset, already active: it fires neither Activation nor
	// Deactivation (0x4ea320, B-162)
	VideoPlay play = {limit, _dlg.nextVideoEnd, loop, _fireSerial, false, _dlg.clipsActivated || viewLoop, viewLoop, 0};
	_media.videoQueue.push_back(id);
	_media.videoPlays.push_back(play);
	if (_media.videoQueue.size() == 1) {
		startVideo();
	} else if (!play.activated) {
		// The exe activates a clip's asset object when it is queued, not when it starts
		_media.videoPlays.back().activated = true;
		activateAsset(id);
	}
}

void Script::startVideo() {
	uint32 id = _media.videoQueue[0];
	VCAsset a;
	_data.getAsset(id, a);
	Common::String limit = _media.videoPlays[0].limit ? Common::String::format(" for %u ms", _media.videoPlays[0].limit) : Common::String();
	if (_media.videoPlays[0].loop)
		limit += " looped";
	log(Common::String::format("  >> video #%u ", id) + a.files[0].file + limit + "  \"" + nameOrNone(_data, a.name) + "\"");
	if (!_media.videoPlays[0].activated) {
		_media.videoPlays[0].activated = true;
		activateAsset(id);
	}
}

void Script::endVideo(const char *how) {
	if (_media.videoQueue.empty()) {
		log("  (nothing playing)");
		return;
	}
	uint32 id = _media.videoQueue.remove_at(0);
	bool viewVideo = _media.videoPlays[0].serial == _viewSerial || _media.videoPlays[0].viewWait;
	bool viewLoop = _media.videoPlays[0].viewLoop;
	_media.videoPlays.remove_at(0);
	log(Common::String::format("  << video #%u %s", id, how));
	bool next = !_media.videoQueue.empty();
	bool nextVideo = next && (_media.videoPlays[0].serial == _viewSerial || _media.videoPlays[0].viewWait);
	bool nextWait = next && _media.videoPlays[0].viewWait;
	// A held view comes at the stop of the last video it waits for, before
	// that video's Object Deactivation (its asset object is deactivated
	// after the view's activation); a video the view queues starts at once
	// if the queue was empty, otherwise after the one already queued. A video queued before the view was entered
	// that has not started yet comes first (its show event is older than the view's activation; box-click-kinds:
	// 33401 and 20361 of a topic, then a view entered when the clip before them ended)
	if (_pos.held && !nextWait && (!nextVideo || !viewVideo))
		activateView();
	else if (_pos.assetHeld)
		activateViewAsset();
	_media.videoNextPending = next;
	if (!viewLoop) {
		deactivateAsset(id);
		_aiSoundEndPending = true;
	}
	_media.videoNextPending = false;
	if (next)
		startVideo();
	else if (_media.videoQueue.empty()) {
		releaseHeldSounds();
		showView(); // Back to the view, with whatever the video's triggers changed
		// The box comes back: its idea icons get their objects again, before an idea topic's deactivation
		if (_dlg.box && !_dlg.boxShown) {
			_dlg.boxShown = true;
			activateIdeaIcons();
		}
		endIdeaTopic();
	}
}

void Script::endIdeaTopic() {
	// The clips of an idea's topic (PlayItemTopic) have ended and the box is shown again (0x436230): the topic's
	// object is deactivated, firing its Object Deactivation triggers (evidence1-to-skinnergone: 41642, which counts
	// iAllQuestionsAskedSkinner up).
	if (!_dlg.ideaTopic || !_media.videoQueue.empty() || !_media.clips.empty())
		return;
	uint32 topic = _dlg.ideaTopic;
	_dlg.ideaTopic = 0;
	fire(topic, kEventObjectDeactivation);
}

} // End of namespace XFiles
