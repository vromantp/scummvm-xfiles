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
#include "common/debug.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/fonts/ttf.h"
#include "graphics/surface.h"

#include "xfiles/detection.h"
#include "xfiles/movie.h"
#include "xfiles/pictures.h"
#include "xfiles/script.h"
#include "xfiles/script_internal.h"
#include "xfiles/vcdata.h"
#include "xfiles/vcdump.h"
#include "xfiles/view_internal.h"
#include "xfiles/xfiles.h"

namespace XFiles {

/** The camera (docs/subsystems/camera.md): photos it holds, its shutter assets (RegCameraAss), viewfinder width. */
static const int32 kCameraPhotos = 24;
static const uint32 kCameraShutter = 34747;
static const uint32 kCameraShutterFull = 57104;
static const int kCameraWidth = 320;

/** OnIntInvClick (XFiles.exe 0x467fb0): the PDA's Inventory_iPDA value, bDisablePDA and PDAalert.amv. */
static const int32 kInventoryPDA = 14;

static const uint32 kDisablePDAVariable = 5277;
static const uint32 kPDAAlertSound = 75688;

bool Script::binocularsView(bool on) {
	// Tool_SpecialView 0x51bc70: the view change of the binoculars, by iCurrViewSpecial (conversation-reactions.md)
	struct Change {
		int32 value;
		uint32 take;
		uint32 putBack;
	};
	static const Change kChanges[] = {
		{7879, 7878, 7879},
		{7878, 7878, 7879},
		{19688, 70710, 19688},
		{70710, 70710, 19688},
		{19684, 70712, 19684},
		{70712, 70712, 19684},
		{19692, 71914, 19692},
		{71914, 71914, 19692},
	};
	if (!isSpecialView())
		return false;
	int32 value = getVariable(_viewSpecialVariable);
	for (uint i = 0; i < ARRAYSIZE(kChanges); i++) {
		if (kChanges[i].value != value)
			continue;
		// 0x51cf10: disable the shown asset, then enable the new one; the asset list then swaps them
		uint32 from = 0;
		for (uint j = 0; j < _viewObjects.size() && !from; j++) {
			if (_data.getClassOf(_viewObjects[j]) == kClassAssetRef)
				from = _viewObjects[j];
		}
		uint32 to = on ? kChanges[i].take : kChanges[i].putBack;
		if (!from)
			return true;
		// 0x51cf10 does nothing if the view's one-shot clip has ended (or there is no clip): no swap, no reaction
		uint32 ms = 0;
		if (_hooks && !_hooks->getViewClipTime(ms))
			return true;
		log(Common::String::format("  binoculars' view change: #%u -> #%u", from, to));
		enableTarget(from, false); // 0x4083d0
		enableTarget(to, true);
		// The new asset starts at the old one's time (view clip time +0x170); a still gets none
		VCAsset next;
		if (_hooks && ms && _data.getAsset(to, next) && !(next.flags & kAssetStill))
			_hooks->setViewClipStart(ms);
		return true;
	}
	if (value == 23486 && on)
		binocularsVideoSwap();
	return true;
}

void Script::binocularsVideoSwap() {
	// 0x4a96b0 (B-188): the sound object of 23486 is replaced by one of 70706, which plays from 23486's time to its
	// end. Measured order: 70706 starts and activates (71938), then 23486 stops normally, so its Deactivation 33950
	// fires, then the close-up plays; its end runs 71942. Only a take swaps; a putback changes nothing
	static const uint32 kFrom = 23486, kTo = 70706;
	if (_videoQueue.empty() || _videoQueue[0] != kFrom)
		return;
	VCAsset from, to;
	if (!_data.getAsset(kFrom, from) || !_data.getAsset(kTo, to) || from.files.empty() || to.files.empty())
		return;
	uint32 start = 0, ms = 0;
	if (_hooks && _hooks->getVideoTime(kFrom, ms) && ms > from.files[0].start)
		start = to.files[0].start + (ms - from.files[0].start);
	log(Common::String::format("  binoculars' video swap: #%u -> #%u", kFrom, kTo));
	playAsset(kTo, kModePlayOnce, nullptr);
	for (uint i = 0; i < _videoQueue.size(); i++) {
		if (_videoQueue[i] == kTo)
			_videoPlays[i].start = start;
	}
	// The exe stops it through the normal path (sound.stop stopped=0), not a Stop action
	// (the sim has no clock: its next wait step ends 23486 as the exe's log shows)
	if (_hooks) {
		_hooks->playAsset(kFrom, kModeStop, false, 0, 0);
		endVideo("ended");
	}
}

int Script::binocularsMode() const {
	if (!isSpecialView())
		return 0;
	int32 value = getVariable(_viewSpecialVariable);
	return (value == 1228 || value == 1236 || value == 84307) ? 1 : 2;
}

bool Script::binocularsBlock(const Common::Point &pos) const {
	if (heldToolType() != 1 || pos.x < 0 || pos.x >= 640 || pos.y < 90 || pos.y >= 330)
		return false;
	return !_viewSpecialVariable || getVariable(_viewSpecialVariable) != 7878;
}

int Script::heldToolType() const {
	if (!isHolding())
		return 8;
	switch (getVariable(_heldVariable)) {
	case 2:
		return 1;
	case 3:
		return 2;
	case 8:
		return 3;
	case 13:
		return 4;
	case 9:
		return 7;
	default:
		return 0;
	}
}

void Script::toolPause(int32 from, int32 to) {
	// 0x5078f0(1) after the old tool object's deactivation, then (0) in the new one's activation
	if (!_hooks)
		return;
	if (isPausingTool(from))
		_hooks->pauseTimedText(false);
	if (isPausingTool(to))
		_hooks->pauseTimedText(true);
}

void Script::toolReaction(int32 item) {
	// 0x4e2160: S2 for the binoculars and the flashlight, S3 for night vision
	uint slot = 0;
	if (item == 2 || item == 8)
		slot = 2;
	else if (item == 13)
		slot = 3;
	VCView view;
	Common::Array<uint32> charViews;
	if (!slot || !_data.getView(_view, view) || !_data.getMembers(view.characters, charViews))
		return;
	for (uint i = 0; i < charViews.size(); i++) {
		uint32 slots[kReactionSlots];
		if (!isEnabled(charViews[i]) || !getReactionSlots(charViews[i], slots) || !slots[slot])
			continue;
		// A one-shot delay of 2000 ms (0x483f90); without an engine the sim plays it at once
		if (!_hooks || !_hooks->delayPlay(slots[slot], 2000))
			playStock(slots[slot]);
		return;
	}
}

bool Script::runCameraActions(uint32 id) {
	bool ran = false;
	Common::Array<uint32> triggers;
	_data.getMembers(getTriggers(id), triggers);
	for (uint i = 0; i < triggers.size(); i++) {
		VCTrigger t;
		if (!_data.getTrigger(triggers[i], t) || t.event != kEventMouseClick)
			continue;
		for (uint j = 0; j < t.actions.size(); j++) {
			VCAction a;
			if (!_data.getAction(t.actions[j], a) || !a.conditional)
				continue;
			const VCExpression &c = a.condition;
			bool tested = c.lhs.kind == kOperandVariable && c.lhs.value == _heldVariable;
			tested = tested || (c.rhs.kind == kOperandVariable && c.rhs.value == _heldVariable);
			LocalVariables locals;
			if (tested && test(c, EventFields(), locals)) {
				execute(a, kEventMouseClick, EventFields(), locals);
				ran = true;
			}
		}
	}
	return ran;
}

void Script::addPhoto(uint32 asset, int left, int right, uint32 clipTime) {
	VCAsset data;
	if (!asset || !_data.getAsset(asset, data) || data.files.empty())
		return;
	VCPhoto photo;
	photo.asset = asset;
	photo.clipTime = clipTime;
	photo.crop[0] = left;
	photo.crop[1] = 0;
	photo.crop[2] = right;
	photo.crop[3] = kBandHeight;
	photo.disc = _disc;
	_photos.push_back(photo);
}

void Script::takePhoto(uint32 asset) {
	// TakePhoto 0x4e5a60: nothing at 24 photos; else a photo of the asset, crop (0, 0, 300, 240), time 1000
	if (_photosOnCamera >= kCameraPhotos)
		return;
	addPhoto(asset, 0, 300, 1000);
	_photosOnCamera++;
	log(Common::String::format("      camera: photos %d", _photosOnCamera));
}

void Script::cameraClick(const Common::Point &pos) {
	if (!isCameraEnabled())
		return;
	// 0x4f3090: the shutter, as a movie of its own (no sound object, no play line)
	bool full = _photosOnCamera >= kCameraPhotos;
	uint32 shutter = full ? kCameraShutterFull : kCameraShutter;
	log(Common::String::format("      camera: shutter #%u", shutter));
	if (_hooks)
		_hooks->playShutter(shutter);

	// Camera_TakePhoto (0x4f31e0): the video playing in the band, else the view's asset; the count goes up
	// whether or not a photo was made
	if (!full) {
		uint32 asset = 0;
		if (!_videoQueue.empty()) {
			asset = _videoQueue[0];
		} else {
			Common::Array<uint32> assets;
			uint32 background;
			getViewAssets(assets, background);
			if (!assets.empty())
				asset = assets[0];
		}
		// The viewfinder, 320 wide, moved back into the 600 wide band; top 0, bottom 240
		int left = pos.x - kCameraWidth / 2;
		if (left < 0)
			left = 0;
		if (left + kCameraWidth > kBandWidth)
			left = kBandWidth - kCameraWidth;
		addPhoto(asset, left, left + kCameraWidth, _hooks ? _hooks->getPhotoTime() : 0);
		_photosOnCamera++;
		log(Common::String::format("      camera: photos %d", _photosOnCamera));
	}

	// The objects under the viewfinder (320 wide at the click, in the band): the character views of the view, then
	// the explorables; the first of each kind that had a camera action to run ends its loop
	int vLeft = MAX<int>(pos.x - kCameraWidth / 2, kBandLeft);
	int vRight = MIN<int>(pos.x + kCameraWidth / 2, kBandLeft + kBandWidth);
	VCView view;
	if (!_data.getView(_view, view))
		return;
	for (int kind = 0; kind < 2; kind++) {
		Common::Array<uint32> ids;
		_data.getMembers(kind == 0 ? view.characters : view.explorables, ids);
		for (uint i = 0; i < ids.size(); i++) {
			VCRect r;
			if (!isEnabled(ids[i]))
				continue;
			if (kind == 0) {
				VCCharView cv;
				if (!_data.getCharView(ids[i], cv))
					continue;
				r = cv.rect;
			} else {
				VCExplorable e;
				if (!_data.getExplorable(ids[i], e))
					continue;
				r = e.rect;
			}
			if (r.left >= vRight || r.right <= vLeft || r.top >= kBandTop + kBandHeight || r.bottom <= kBandTop)
				continue;
			if (runCameraActions(ids[i]))
				break;
		}
	}

	// The first character view with a reaction clip in slot 2 not already playing speaks at once
	Common::Array<uint32> charViews;
	_data.getMembers(view.characters, charViews);
	for (uint i = 0; i < charViews.size(); i++) {
		uint32 slots[kReactionSlots];
		if (!isEnabled(charViews[i]) || !getReactionSlots(charViews[i], slots) || !slots[2])
			continue;
		if (_sounds.contains(slots[2]) && _sounds[slots[2]])
			continue;
		playStock(slots[2]);
		break;
	}
}

void Script::addToolHooks() {
	HookTable<Script> &t = _hookTable;
	t.add("RegViewSpecial", [](Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		s._viewSpecialVariable = c.action.args[0].value; // 0x481140
		return true;
	});
	t.add("OnBinocClick", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// 0x4e1b30: the item goes into the variable if it is not in hand; then, unless the view has a view change of the
		// binoculars' own (not ported), a character reacts
		if (s._heldVariable && s.getVariable(s._heldVariable) != c.args[0]) {
			int32 prior = s.getVariable(s._heldVariable);
			s._variables[s._heldVariable] = c.args[0];
			s._binocularsHeld = s.isHolding(); // stands for the held object 0x51baf0
			s._binocularsMode = s.binocularsMode();
			s.toolPause(prior, c.args[0]); // its activation, 0x51c130
			if (!s.binocularsView(true))
				s.toolReaction(c.args[0]);
		}
		return true;
	});
	t.add("OnFLightClick", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// 0x4e1d80
		if (s._heldVariable && s.getVariable(s._heldVariable) != c.args[0]) {
			s.toolPause(s.getVariable(s._heldVariable), c.args[0]);
			s._variables[s._heldVariable] = c.args[0];
			s.toolReaction(c.args[0]);
		}
		return true;
	});
	t.add("OnNVisionClick", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// 0x4e1f80
		if (s._heldVariable && s.getVariable(s._heldVariable) != c.args[0]) {
			s.toolPause(s.getVariable(s._heldVariable), c.args[0]);
			s._variables[s._heldVariable] = c.args[0];
			s.toolReaction(c.args[0]);
		}
		return true;
	});
	t.add("OnCameraClick", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// 0x4e1c50: no character reaction for the camera (it passes 3 to Tool_CharReaction). The camera object is
		// enabled unless the view is a special one (iCurrViewSpecial 1)
		if (s._heldVariable && s.getVariable(s._heldVariable) != c.args[0]) {
			s.toolPause(s.getVariable(s._heldVariable), c.args[0]);
			s._variables[s._heldVariable] = c.args[0];
			s._cameraEnabled = !s._viewSpecialVariable || s.getVariable(s._viewSpecialVariable) != 1;
		}
		return true;
	});
	t.add("OnGunClick", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// 0x4e1e80: the gun is taken like the camera, without a character reaction (no Tool_CharReaction)
		if (s._heldVariable && s.getVariable(s._heldVariable) != c.args[0])
			s._variables[s._heldVariable] = c.args[0];
		s._gunValue = c.args[0];
		return true;
	});
	t.add("RegCurrInvVar", [](Script &s, const HookCall &c) {
		if (!c.has(2) || !c.isVariable(0))
			return false;
		// 0x458e60: the variable starts as "nothing in hand"
		s._heldVariable = c.action.args[0].value;
		s._heldNone = c.args[1];
		s._variables[s._heldVariable] = s._heldNone;
		return true;
	});
	t.add("OnInvClick", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// Inv_Select 0x49b410: the item's value goes into the variable
		if (s._heldVariable)
			s._variables[s._heldVariable] = c.args[0];
		return true;
	});
	t.add("SelectInv", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// 0x50a390: if the item has an icon, put back what is in hand and
		// take the item, as a click on its icon would
		if (s.hasInventoryIcon(c.args[0])) {
			if (s._hooks)
				s._hooks->showInventoryBar();
			s.selectInventory(c.args[0]);
		}
		return true;
	});
	t.add("StopPlayAssets", [](Script &s, const HookCall &c) {
		// 0x4ccc40 -> 0x4b4170(queue, b): b = 1 drops the queued events first, then the shown videos stop
		if (!c.has(1))
			return false;
		s.stopShownVideos(c.args[0] != 0);
		return true;
	});
	t.add("DeselectInv", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		// 0x50a570: 0 puts back any item, else only that one
		if (c.args[0] == 0 || s._heldCursorItem == (uint32)c.args[0])
			s.putBack();
		return true;
	});
	t.add("OnIntInvClick", [](Script &s, const HookCall &c) {
		// 0x467fb0: the PDA and phone icons never set iCurrInventory. For the
		// PDA, while a sound object plays an asset with a picture (a video, a
		// clip), it sets bDisablePDA, which the icon's trigger tests before it
		// shows the PDA, plays the alert and clears only the cursor (a tool in
		// hand stays active). Else the timers pause, the held object goes back
		// as Inv_PutBack does (the variable to none, a tool's view change back,
		// the timed text resumes) and the cursor clears
		if (c.has(1) && c.args[0] == kInventoryPDA) {
			if (!s._videoQueue.empty() || !s._clips.empty()) {
				s._variables[kDisablePDAVariable] = 1;
				s.log(Common::String::format("      PDA refused: bDisablePDA = 1, alert #%u", kPDAAlertSound));
				if (s._hooks)
					s._hooks->playAsset(kPDAAlertSound, kModePlayOnce, false, 0, 0);
				s._heldCursorItem = 0;
				if (s._hooks)
					s._hooks->heldItemChanged(0);
				return true;
			}
			if (s._hooks)
				s._hooks->pauseTimers();
		}
		if (s.isHolding()) {
			s.putBack();
		} else {
			s._heldCursorItem = 0;
			if (s._hooks)
				s._hooks->heldItemChanged(0);
		}
		return true;
	});
}

void XFilesEngine::drawPhotos() {
	const ComputerHooks &computer = _script->getComputerHooks();
	if (!computer.isPhotoViewerOpen())
		return;
	const Common::Array<VCPhoto> &photos = _script->getPhotos();
	const Common::Array<uint> &thumbs = computer.getPhotoThumbs();
	for (uint k = 0; k < thumbs.size(); k++) {
		VCInterfaceItem item;
		if (thumbs[k] >= photos.size() || !_data->getInterfaceItem(ComputerHooks::getPhotoSlotItem(k), item))
			continue;
		const Graphics::Surface *pic = getPhotoPicture(photos[thumbs[k]], 80, 60);
		if (pic)
			blit(*pic, item.rect.left, item.rect.top);
	}
	int enlarged = computer.getPhotoEnlarged();
	VCInterfaceItem item;
	if (enlarged >= 0 && (uint)enlarged < photos.size() && _data->getInterfaceItem(ComputerHooks::getPhotoEnlargedItem(), item)) {
		const Graphics::Surface *pic = getPhotoPicture(photos[enlarged], 160, 120);
		if (pic)
			blit(*pic, item.rect.left, item.rect.top);
	}
}

void XFilesEngine::drawViewfinder() {
	static const uint32 kViewfinderAsset = 34749;
	// System h=-13 for the font, (3, 5) inside the picture (0x4f2e50)
	static const int kCounterX = 3, kCounterY = 5, kCounterFontSize = 13;
	if (!_script || !_script->isCameraEnabled())
		return;
	if (!_viewfinder)
		_viewfinder = loadPicture(*_data, _discFiles, kViewfinderAsset, _screen.format);
	if (!_viewfinder)
		return;
	// Centred on the mouse (hotspot (160, 120) of the 320 x 241 frame), clipped to the band, not clamped (0x507e80)
	int x = _mouse.x - _viewfinder->getWidth() / 2;
	int y = _mouse.y - _viewfinder->getSurface().h / 2;
	// A sub-surface of the band clips the picture and the counter
	Graphics::Surface band = _screen.getSubArea(Common::Rect(kBandLeft, kBandTop, kBandLeft + kBandWidth, kBandTop + kBandHeight));
	x -= kBandLeft;
	y -= kBandTop;
	_viewfinder->draw(band, x, y);
	// The photos left, red, System bold 13 at the picture's corner
	if (!_counterFont) {
		_counterFont = Graphics::loadTTFFontFromArchive("LiberationSans-Bold.ttf", kCounterFontSize, Graphics::kTTFSizeModeCharacter, 0, 0, getPreference(kPrefSmoothText) ? Graphics::kTTFRenderModeLight : Graphics::kTTFRenderModeMonochrome);
		_counterFontOwned = _counterFont != nullptr;
		if (!_counterFont)
			_counterFont = FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	}
	if (_counterFont) {
		Common::String left = Common::String::format("%d", 24 - _script->getPhotosOnCamera());
		_counterFont->drawString(&band, left, x + kCounterX, y + kCounterY, 16, band.format.RGBToColor(255, 0, 0));
	}
}

bool XFilesEngine::hasToolOverlay() const {
	if (!_script)
		return false;
	int type = _script->heldToolType();
	return type == 1 || type == 3 || type == 4;
}

void XFilesEngine::drawToolOverlay() {
	if (!hasToolOverlay())
		return;
	int type = _script->heldToolType();
	// The overlays work on the band (20, 90)-(620, 330)
	Graphics::Surface band = _screen.getSubArea(Common::Rect(kBandLeft, kBandTop, kBandLeft + kBandWidth, kBandTop + kBandHeight));
	if (type == 4)
		drawNightVision(band);
	else if (type == 3)
		drawFlashlight(band);
	else if (_script->binocularsMode() == 0)
		drawBinoculars(band);
}

void XFilesEngine::drawNightVision(Graphics::Surface &band) {
	static const uint32 kNightVisionAsset = 34751;
	// The band blend colour 0x6c09dc = RGB(0, 150, 100): each pixel (r, g, b) becomes (0, g * 150 / 255, b * 100 / 255)
	for (int y = 0; y < band.h; y++) {
		for (int x = 0; x < band.w; x++) {
			byte r, g, b;
			readPixel(band, x, y, r, g, b);
			writePixel(band, x, y, 0, g * 150 / 255, b * 100 / 255);
		}
	}
	// The frame at the band's top-left, the blue key transparent; it does not follow the mouse
	if (!_nightVisionMask)
		_nightVisionMask = loadPicture(*_data, _discFiles, kNightVisionAsset, _screen.format);
	if (_nightVisionMask)
		_nightVisionMask->draw(band, 0, 0);
}

void XFilesEngine::drawFlashlight(Graphics::Surface &band) {
	static const uint32 kFlashlightMaskAsset = 34745;
	if (!_flashlightMask)
		_flashlightMask = loadPicture(*_data, _discFiles, kFlashlightMaskAsset, _screen.format);
	if (!_flashlightMask)
		return;
	const byte *props = _script->getFlashlight();
	bool outer = props[1] != 0;
	int32 beam = _script->getPreferenceValue(kPrefFlashlightBeam);
	int32 brightness = _script->getPreferenceValue(kPrefFlashlightBrightness);

	// The beam size stretches the mask (0x4d8300)
	const Graphics::Surface *mask = &_flashlightMask->getSurface();
	double factor = 1.0;
	if (outer)
		factor = beam == 'S' ? 0.45 : 0.6;
	else
		factor = beam == 'M' ? 0.72 : beam == 'S' ? 0.45 : 1.0;
	if (factor != 1.0) {
		int32 key = beam * 2 + (outer ? 1 : 0);
		if (!_flashlightScaled || _flashlightScaledKey != key) {
			if (_flashlightScaled) {
				_flashlightScaled->free();
				delete _flashlightScaled;
			}
			int w = (int)(mask->w * factor), h = (int)(mask->h * factor);
			if (w & 1) {
				w++;
				h++;
			}
			_flashlightScaled = mask->scale(w, h, false);
			_flashlightScaledKey = key;
		}
		mask = _flashlightScaled;
	}

	// Centred on the mouse, clipped to the band, not clamped
	int ml = _mouse.x - kBandLeft - (mask->w >> 1), mt = _mouse.y - kBandTop - (mask->h >> 1);
	// With the mouse off the band (in the bar) the mask can lie wholly outside it: clip() leaves an empty rect
	Common::Rect clip(ml, mt, ml + mask->w, mt + mask->h);
	clip.clip(band.w, band.h);
	if (props[0] == 0) {
		// The grey light (n = 0/1): a grey level from c2 and the brightness preference (0x4eb1e0) through the mask
		// darkened to 55 / 255 (0x4d8790 with 200): b + (G - b) * m' / 255
		static const byte kLevels[3][3] = {{0x50, 0x80, 0xa0}, {0xa0, 0xc8, 0xdc}, {0x80, 0xa0, 0xc8}}; // [B, D, other][c2]
		int row = brightness == 'B' ? 0 : brightness == 'D' ? 1 : 2;
		int g = props[2] <= 2 ? kLevels[row][props[2]] : 0xa0;
		for (int y = clip.top; y < clip.bottom; y++) {
			for (int x = clip.left; x < clip.right; x++) {
				byte m, mg, mb;
				readPixel(*mask, x - ml, y - mt, m, mg, mb);
				int weight = m * 55 / 255;
				if (!weight)
					continue;
				byte r, gr, b;
				readPixel(band, x, y, r, gr, b);
				writePixel(band, x, y, r + (g - r) * weight / 255, gr + (g - gr) * weight / 255, b + (g - b) * weight / 255);
			}
		}
		return;
	}
	// The n = 2 look (tool-objects.md "Overlay drawing" point 5): the untinted picture P shows through the mask over
	// the tinted band T, out = T + (P - T) * m' / 255. Without a tint T = P
	if (_bandUntinted.w != band.w || _bandUntinted.h != band.h)
		return;
	// 'B' lifts the mask inside its inscribed oval
	for (int y = clip.top; y < clip.bottom; y++) {
		for (int x = clip.left; x < clip.right; x++) {
			byte m, mg, mb;
			readPixel(*mask, x - ml, y - mt, m, mg, mb);
			int mp = m;
			double u = (x - ml + 0.5 - mask->w / 2.0) / (mask->w / 2.0), v = (y - mt + 0.5 - mask->h / 2.0) / (mask->h / 2.0);
			bool inOval = u * u + v * v <= 1.0;
			if (brightness == 'D')
				mp = m * 55 / 255;
			else if (brightness == 'B' && inOval)
				mp = m + (255 - m) * 200 / 255;
			if (!mp)
				continue;
			byte pr, pg, pb, tr, tg, tb;
			readPixel(_bandUntinted, x, y, pr, pg, pb);
			readPixel(band, x, y, tr, tg, tb);
			writePixel(band, x, y, tr + ((pr - tr) * mp + 127) / 255, tg + ((pg - tg) * mp + 127) / 255, tb + ((pb - tb) * mp + 127) / 255);
		}
	}
}

void XFilesEngine::drawBinoculars(Graphics::Surface &band) {
	static const uint32 kBinocularMaskAsset = 34753;
	if (!_binocularMask)
		_binocularMask = loadPicture(*_data, _discFiles, kBinocularMaskAsset, _screen.format);
	if (!_binocularMask)
		return;
	// The beam size (PrefFlashBeam) stretches the mask, width and height truncated, both + 1 if the width is odd (0x51c5d0)
	int32 beam = _script->getPreferenceValue(kPrefFlashlightBeam);
	const Graphics::Surface *mask = &_binocularMask->getSurface();
	double factor = beam == 'M' ? 0.75 : beam == 'S' ? 0.53 : 1.0;
	if (factor != 1.0) {
		if (!_binocularScaled || _binocularScaledBeam != beam) {
			if (_binocularScaled) {
				_binocularScaled->free();
				delete _binocularScaled;
			}
			int w = (int)(mask->w * factor), h = (int)(mask->h * factor);
			if (w & 1) {
				w++;
				h++;
			}
			_binocularScaled = mask->scale(w, h, false);
			_binocularScaledBeam = beam;
		}
		mask = _binocularScaled;
	}
	int w = mask->w, h = mask->h;

	// The mask is centred on the mouse and clipped to the band, not clamped. The zoom rect is that rect with each clipped
	// edge pushed back out by the mask size, and its centre is the zoom centre (0x567ac0)
	int ml = _mouse.x - kBandLeft - (w >> 1), mt = _mouse.y - kBandTop - (h >> 1);
	Common::Rect clip(ml, mt, ml + w, mt + h);
	clip.clip(band.w, band.h);
	if (clip.isEmpty()) {
		band.fillRect(Common::Rect(band.w, band.h), 0);
		return;
	}
	int l = clip.left, t = clip.top, r = clip.right, b = clip.bottom;
	if (l == 0)
		l = r - w;
	if (r == band.w)
		r = l + w;
	if (t == 0)
		t = b - h;
	if (b == band.h)
		b = t + h;
	int cx = l + (r - l) / 2, cy = t + (b - t) / 2;
	// With both opposite edges clipped (the W beam's 275 against 240) the mask is drawn from the clip's own edge, measured
	// from the exe's shots (B-228, B-240). Horizontally this cannot happen: no mask is wider than 600
	if (clip.top == 0 && clip.bottom == band.h)
		mt = 0;

	Graphics::Surface copy;
	copy.copyFrom(band);
	// The band holds the untinted picture while the binoculars are in hand (getBandTint): the zoom shows it tinted
	byte tr, tg, tb;
	bool tinted = _script->getSceneTint(tr, tg, tb);
	band.fillRect(Common::Rect(band.w, band.h), 0);
	for (int y = clip.top; y < clip.bottom; y++) {
		for (int x = clip.left; x < clip.right; x++) {
			byte m, mg, mb;
			readPixel(*mask, x - ml, y - mt, m, mg, mb);
			if (!m)
				continue;
			int sx = CLIP(cx + (x - cx) / 2, 0, (int)band.w - 1), sy = CLIP(cy + (y - cy) / 2, 0, (int)band.h - 1);
			byte red, green, blue;
			readPixel(copy, sx, sy, red, green, blue);
			if (tinted) {
				red = red * tr / 255;
				green = green * tg / 255;
				blue = blue * tb / 255;
			}
			writePixel(band, x, y, red * m / 255, green * m / 255, blue * m / 255);
		}
	}
	copy.free();
}

} // End of namespace XFiles
