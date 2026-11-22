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

#include "common/events.h"
#include "common/system.h"
#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/fonts/ttf.h"
#include "graphics/pixelformat.h"

#include "xfiles/xfiles.h"
#include "xfiles/media/movie.h"
#include "xfiles/media/pictures.h"
#include "xfiles/script/script.h"
#include "xfiles/script/vcdata.h"
#include "xfiles/ui/view_band.h"

namespace XFiles {

void XFilesEngine::syncView() {
	Common::Array<uint32> ids;
	uint32 background;
	_script->getViewAssets(ids, background);
	if (_viewId == _script->getView() && ids == _viewAssets) {
		for (uint i = 0; i < _layers.size(); i++)
			_layers[i]->update();
		return;
	}

	// A nav click's view change animates in the band (the Transitions preference)
	int mode = _script->getTransition();
	_script->clearTransition();
	bool animate = mode != Script::kTransitionCut && _viewId && _viewId != _script->getView() && canPlayTransition();
	// The old picture is what is on screen; with a tool overlay (night vision) or a head credit on it, it is redrawn
	// without, since playTransition draws both over every frame
	Graphics::Surface oldBand;
	if (animate) {
		if (hasToolOverlay() || _headCredits.isShown())
			drawBand(oldBand);
		else
			oldBand.copyFrom(_screen.getSubArea(Common::Rect(kBandLeft, kBandTop, kBandLeft + kBandWidth, kBandTop + kBandHeight)));
	}

	closeView();
	latchBandTint(); // the view play takes the tint the nav's SetLBoxTint/ClearLBoxTint set (0x45d2f0)
	_viewId = _script->getView();
	_viewAssets = ids;
	for (uint i = 0; i < ids.size(); i++) {
		VCAsset asset;
		Movie *movie = new Movie();
		// openMovie also applies a play range PrepBombTime gave the asset
		if (!_data->getAsset(ids[i], asset) || !openMovie(*movie, ids[i])) {
			delete movie;
			continue;
		}
		if (ids[i] == background)
			movie->showStill();
		else if ((asset.flags & kAssetLoop) && !getPreference(kPrefLiveEnvironments)) {
			// Live Environments off (Screen_PlayView): a looping clip is drawn as its first frame
			movie->showStill();
			debugC(2, kDebugScript, "-- view clip #%u shown as a still (live environments off)", ids[i]);
		} else {
			// The restored clip time applies to the first player started
			movie->play(asset.flags & kAssetLoop, _pendingClipTime);
			// The clip's .HOT file, only for an asset with flag 0x10 (Player_ForAsset): its entries open and close by the clip's time
			if (!_hotMovie && (asset.flags & kAssetHotSpots) && _hot.load(_discFiles, asset,
				_script->getPreferenceValue(kPrefActionSceneDifficulty)))
				_hotMovie = movie;
			debugC(2, kDebugScript, "-- view clip #%u from %d ms", ids[i],
				_pendingClipTime >= 0 ? _pendingClipTime : (int)asset.files[0].start);
			_pendingClipTime = -1;
		}
		_layers.push_back(movie);
	}
	_pendingClipTime = -1;
	if (animate) {
		for (uint i = 0; i < _layers.size(); i++)
			_layers[i]->update();
		Graphics::Surface newBand;
		drawBand(newBand);
		playTransition(mode, oldBand, newBand);
		newBand.free();
	}
	oldBand.free();
}

void XFilesEngine::drawBand(Graphics::Surface &band) {
	// The view's layers drawn into the band the way drawView does, without captions and tool overlay
	Graphics::Surface keep;
	keep.copyFrom(_screen);
	clearScreen();
	for (uint i = 0; i < _layers.size(); i++) {
		Common::Point pos = getBandPosition(*_layers[i]);
		blit(_layers[i]->getFrame(), pos.x, pos.y);
	}
	applyBandTint(false);
	band.copyFrom(_screen.getSubArea(Common::Rect(kBandLeft, kBandTop, kBandLeft + kBandWidth, kBandTop + kBandHeight)));
	_screen.copyFrom(keep);
	keep.free();
}

bool XFilesEngine::canPlayTransition() const {
	// Screen_PlayView 0x464330: the preference is on and the held item is not a tool mode of type 3 (the camera). A
	// step list and a fast-forward have no time: they cut, so fixtures stay deterministic
	if (!_steps.empty() || !_fastForward.empty() || !getPreference(kPrefTransitions))
		return false;
	return !(_script->isHolding() && _script->getVariable(_script->getHeldVariable()) == 3);
}

/**
 * The slide (modes 1, 2; 0x5664b0) and the wipe (mode 3; 0x565fd0) in the band (docs/exe/view-transitions.md). The
 * slide moves the old picture, a 100 px edge and the new picture together by the step table (0x65ee40) per tick;
 * the wipe's two edges start at the hotspot centre and each tick move 100 px apart. The edge is a linear
 * cross-fade of old and new, the last tick's frame is the plain new view. The exe has no tick period (one tick per
 * compositor pass, about 33 ms in DOSBox), so one fixed period is used.
 */
void XFilesEngine::playTransition(int mode, const Graphics::Surface &oldBand, const Graphics::Surface &newBand) {
	static const uint32 kTransitionTick = 33;
	static const int kEdge = 100;
	static const int kSlideSteps[] = {25, 50, 100, 125, 125, 100, 50, 25};
	debugC(1, kDebugScript, "-- transition mode %d", mode);

	const Graphics::PixelFormat &fmt = _screen.format;
	Common::Point centre(kBandLeft + kBandWidth / 2, kBandTop + kBandHeight / 2);
	_script->getTransitionCentre(centre);
	int wipeX = centre.x; // the hotspot's screen x, taken as a band x (the exe does not subtract the band's left)

	uint32 start = g_system->getMillis();
	int slidePos = 0; // S, the old picture's offset
	for (int tick = 0;; tick++) {
		int left = 0, right = 0; // the wipe's edges
		bool done;
		if (mode == Script::kTransitionForward) {
			left = wipeX - kEdge - kEdge * tick;
			right = wipeX + kEdge * tick;
			done = left + kEdge <= 0 && right >= kBandWidth;
			debugC(1, kDebugScript, "-- transition wipe tick %d l=%d r=%d%s", tick, left, right, done ? " done" : "");
		} else {
			slidePos += kSlideSteps[MIN<int>(tick, ARRAYSIZE(kSlideSteps) - 1)];
			done = slidePos > kBandWidth - kEdge;
			debugC(1, kDebugScript, "-- transition slide tick %d S=%d%s", tick, slidePos, done ? " done" : "");
		}
		for (int y = 0; y < kBandHeight; y++) {
			for (int x = 0; x < kBandWidth; x++) {
				// the pixel is old[oi], new[ni], or the blend of both with the new share a (1 to 254 of 255)
				int oi = x, ni = x, a = 0;
				if (done) {
					a = 255;
				} else if (mode == Script::kTransitionForward) {
					if (x < left) {
						a = 0;
					} else if (x < left + kEdge) {
						a = 1 + (x - left) * 253 / (kEdge - 1); // old on its left, new on its right
					} else if (x < right) {
						a = 255;
					} else if (x < right + kEdge) {
						a = 1 + (kEdge - 1 - (x - right)) * 253 / (kEdge - 1);
					} else {
						a = 0;
					}
				} else {
					// Right (2): the old picture at x + S, the new one at x - (500 - S), the edge between; Left (1)
					// is its mirror image
					int sx = mode == Script::kTransitionRight ? x : kBandWidth - 1 - x;
					int nx = sx - (kBandWidth - kEdge - slidePos);
					int ox = sx + slidePos;
					if (nx < 0) {
						a = 0;
					} else if (nx < kEdge) {
						a = 1 + nx * 253 / (kEdge - 1);
					} else {
						a = 255;
					}
					if (mode != Script::kTransitionRight) {
						ox = kBandWidth - 1 - ox;
						nx = kBandWidth - 1 - nx;
					}
					oi = ox;
					ni = nx;
				}
				uint32 p;
				if (a == 0) {
					p = oldBand.getPixel(CLIP(oi, 0, kBandWidth - 1), y);
				} else if (a == 255) {
					p = newBand.getPixel(CLIP(ni, 0, kBandWidth - 1), y);
				} else {
					byte r1, g1, b1, r2, g2, b2;
					fmt.colorToRGB(oldBand.getPixel(CLIP(oi, 0, kBandWidth - 1), y), r1, g1, b1);
					fmt.colorToRGB(newBand.getPixel(CLIP(ni, 0, kBandWidth - 1), y), r2, g2, b2);
					p = fmt.RGBToColor((r2 * a + r1 * (255 - a)) / 255, (g2 * a + g1 * (255 - a)) / 255, (b2 * a + b1 * (255 - a)) / 255);
				}
				_screen.setPixel(kBandLeft + x, kBandTop + y, p);
			}
		}
		// A held tool's overlay stays over the moving picture (night vision: else it drops out for the transition)
		drawToolOverlay();
		captureBandPass();
		_headCredits.draw(_screen); // fixed on top, out of the slid picture (B-247)
		_wide.mark(WideLayout::kCredits, _screen);
		present();
		if (done)
			break;
		// The scene's sounds keep playing; the player's input is dropped
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
		}
		if (shouldQuit())
			break;
		_sound.update(getPreference(kPref3DSound));
		uint32 due = start + (tick + 1) * kTransitionTick;
		while (!shouldQuit() && (int32)(due - g_system->getMillis()) > 0)
			g_system->delayMillis(5);
	}
}

void XFilesEngine::closeView() {
	_hot.clear();
	_viewHover = 0;
	_hotMovie = nullptr;
	for (uint i = 0; i < _layers.size(); i++)
		delete _layers[i];
	_layers.clear();
	_viewAssets.clear();
	_viewId = 0;
	_viewPaused = false; // A new view starts unpaused
	_clipHold = false;
	_viewHot.clear();
	_viewHotMovie = nullptr;
}

void XFilesEngine::updateViewPause() {
	// Scene_PauseView (0x46cd80, B-007): a shown layout (but the five Field Office ones) stops the view's started
	// movies, which go on from the same frame when the last such layout closes; with Live Environments off a looping
	// clip is rewound instead. The engine's own pause (pauseGame) keeps the clips stopped
	bool want = _interface->pausesView();
	if (want == _viewPaused)
		return;
	_viewPaused = want;
	if (_paused)
		return;
	bool rewind = !want && !getPreference(kPrefLiveEnvironments);
	for (uint i = 0; i < _layers.size(); i++) {
		if (rewind && _layers[i]->isLooping() && _layers[i]->isPlaying()) {
			// Live Environments off: the resumed loop shows its first frame instead of playing (0x44f630)
			_layers[i]->showStill();
			continue;
		}
		_layers[i]->pause(want);
	}
}

Common::Rect XFilesEngine::movieRect() const {
	// The on-screen rect of the playing picture video (B-290); empty for a still or no video
	if (!_video || !_video->isOpen() || _script->getVideoQueue().empty() || isStillVideo(_script->getVideoQueue()[0]))
		return Common::Rect();
	Common::Point pos = getBandPosition(*_video);
	return Common::Rect(pos.x, pos.y, pos.x + _video->getWidth(), pos.y + _video->getHeight());
}

bool XFilesEngine::isBarHidden() const {
	// A new item's reveal shows the hidden bar (deferInventoryIcon)
	return _preferences.variable(kPrefInventory) && _script->getVariable(_preferences.variable(kPrefInventory)) == 'H' && !_barRevealed;
}

/**
 * Clears the screen to black. The SetLBoxTint colour is not a background: the band's picture is multiplied by it
 * (applyBandTint). Not done: the caption-on band (0x6c0c40, down to y 350).
 */
void XFilesEngine::clearScreen() {
	_screen.fillRect(Common::Rect(_screen.w, _screen.h), 0);
}

/**
 * The letterbox tint (SetLBoxTint): every band pixel channel v becomes v * c / 255 (docs/exe/hooks/groups.md). With
 * the flashlight's n = 2 look in hand the untinted band is kept for drawFlashlight. Still views, view pictures and
 * band clips take the multiply; a playing movie does not (B-309: reference run crowbar-band-movie4, movie 20395 drawn
 * at full brightness under tint 65026 while the stills before and after were dark), so the movie path only calls
 * relatchBandTint. Band clips (0x80800) are not measured and stay tinted.
 */
void XFilesEngine::applyBandTint(bool keepUntinted) {
	_bandUntinted.free();
	relatchBandTint();
	if (!_bandTintOn)
		return;
	byte tr = _bandTintRGB[0], tg = _bandTintRGB[1], tb = _bandTintRGB[2];
	Graphics::Surface band = _screen.getSubArea(Common::Rect(kBandLeft, kBandTop, kBandLeft + kBandWidth, kBandTop + kBandHeight));
	if (keepUntinted)
		_bandUntinted.copyFrom(band);
	for (int y = 0; y < band.h; y++) {
		for (int x = 0; x < band.w; x++) {
			byte r, g, b;
			readPixel(band, x, y, r, g, b);
			writePixel(band, x, y, r * tr / 255, g * tg / 255, b * tb / 255);
		}
	}
}

/** A tool show/hide latches the tint again (B-286). */
void XFilesEngine::relatchBandTint() {
	if (_bandTintTool != _script->heldToolType() * 4 + _script->binocularsMode())
		latchBandTint();
}

/**
 * The exe latches the letterbox tint: a SetLBoxTint or ClearLBoxTint takes effect at the next view play or tool
 * show/hide (0x45d2f0 copies the entry into the band, docs/exe/hooks/groups.md), so a turn out of a tinted view
 * slides from the tinted picture although the nav cleared the tint first (B-286).
 */
void XFilesEngine::latchBandTint() {
	_bandTintTool = _script->heldToolType() * 4 + _script->binocularsMode();
	_bandTintOn = _script->getBandTint(_bandTintRGB[0], _bandTintRGB[1], _bandTintRGB[2]);
}

void XFilesEngine::drawView() {
	clearScreen();
	for (uint i = 0; i < _layers.size(); i++) {
		Common::Point pos = getBandPosition(*_layers[i]);
		blit(_layers[i]->getFrame(), pos.x, pos.y);
	}
	// A queued still covers the view band; the bar and the box stay over it
	if (_video->isOpen() && !_script->getVideoQueue().empty() && isStillVideo(_script->getVideoQueue()[0])) {
		Common::Point pos = getBandPosition(*_video);
		blit(_video->getFrame(), pos.x, pos.y);
	}
	// Band clips are part of the view group (0x80800)
	_sound.drawClips(_screen, false);
	applyBandTint(_script->heldToolType() == 3 && _script->getFlashlight()[0]);
	if (_interface->isModal()) {
		// The suspend tint over the view's picture (B-007)
		Common::Rect dim;
		for (uint i = 0; i < _layers.size(); i++) {
			Common::Point pos = getBandPosition(*_layers[i]);
			const Graphics::Surface &frame = _layers[i]->getFrame();
			Common::Rect r(pos.x, pos.y, pos.x + frame.w, pos.y + frame.h);
			if (dim.isEmpty())
				dim = r;
			else
				dim.extend(r);
		}
		Interface::tintRect(_screen, dim);
	}
	drawViewfinder();
	drawToolOverlay();
	captureBandPass(); // the band pass ends here; the rest is the UI pass (B-353)
	// The rest follows the exe's draw list, bottom to top (screen-composition.md): icons, the dialog box (translucent
	// by Pref_bTranslucency, Prefs_Apply 0x452d70; Options and Help cover it and it takes no clicks then), the location
	// card and head credits, captions, the subliminal word, then the layout items over all of it
	_interface->drawBar(_screen, &_wide);
	_dialogBox.draw(_screen, _mouse, !getPreference(kPrefTranslucency));
	_wide.mark(WideLayout::kBox, _screen);
	_captions.drawCard(_screen);
	_wide.mark(WideLayout::kCard, _screen);
	_headCredits.draw(_screen);
	_wide.mark(WideLayout::kCredits, _screen);
	for (uint i = 0; i < _layers.size(); i++)
		drawCaptions(*_layers[i], getBandPosition(*_layers[i]));
	_subliminal.draw(_screen);
	_wide.mark(WideLayout::kSubliminal, _screen);
	_interface->drawItems(_screen);
	if (_dragIdea && _dragMoved) {
		// The dragged idea sits at the canvas mouse
		_wide.setMouseOffset((_mouse == _canvasMouseScreen ? _canvasMouse : toCanvas(_mouse)) - _mouse);
		_interface->drawDragged(_screen, _dragIdea, _mouse);
		_wide.mark(WideLayout::kDrag, _screen);
	}
	drawPhotos();
	// PlayAssetAt clips: group +0x220 key 0x16, over every layout item, the dialog box, captions and the flashlight light
	_sound.drawClips(_screen, true);
}

const Graphics::Surface *XFilesEngine::getPhotoPicture(const VCPhoto &photo, int w, int h) {
	Common::String key = Common::String::format("%u/%u/%d,%d,%d,%d/%dx%d", photo.asset, photo.clipTime, photo.crop[0], photo.crop[1],
		photo.crop[2], photo.crop[3], w, h);
	for (uint i = 0; i < _photoPictures.size(); i++) {
		if (_photoPictures[i].key == key)
			return _photoPictures[i].surface;
	}
	PhotoPicture pic;
	pic.key = key;
	pic.surface = nullptr;
	Movie movie;
	if (openMovie(movie, photo.asset) && movie.showStillAt(photo.clipTime)) {
		const Graphics::Surface &frame = movie.getFrame();
		Common::Rect crop = photo.cropRect();
		crop.clip(Common::Rect(frame.w, frame.h));
		if (crop.isEmpty())
			crop = Common::Rect(frame.w, frame.h);
		Graphics::Surface part = frame.getSubArea(crop);
		pic.surface = part.scale(w, h, true);
	}
	_photoPictures.push_back(pic);
	return pic.surface;
}

} // End of namespace XFiles
