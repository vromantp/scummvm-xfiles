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
#include "xfiles/movie.h"
#include "xfiles/pictures.h"
#include "xfiles/script.h"
#include "xfiles/vcdata.h"

namespace XFiles {

enum {
	// The view band: navigation frames and videos are 600x240 at (20, 90);
	// hotspot rectangles are in screen coordinates
	kBandLeft = 20,
	kBandTop = 90,
	kBandWidth = 600,
	kBandHeight = 240
};

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
			// The clip's .HOT file: its entries open and close by the clip's time
			if (!_hotMovie && _hot.load(_discFiles, asset, _script->getPreferenceValue(kPrefActionSceneDifficulty)))
				_hotMovie = movie;
			debugC(2, kDebugScript, "-- view clip #%u from %d ms", ids[i], _pendingClipTime >= 0 ? _pendingClipTime : (int)asset.files[0].start);
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
		_headCredits.draw(_screen); // fixed on top, out of the slid picture (B-247)
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
	_charHover = 0;
	_hotMovie = nullptr;
	for (uint i = 0; i < _layers.size(); i++)
		delete _layers[i];
	_layers.clear();
	_viewAssets.clear();
	_viewId = 0;
	_viewPaused = false; // A new view starts unpaused
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

static void readPixel(const Graphics::Surface &s, int x, int y, byte &r, byte &g, byte &b);
static void writePixel(Graphics::Surface &s, int x, int y, byte r, byte g, byte b);

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
	// The rest follows the exe's draw list, bottom to top (screen-composition.md): icons, the dialog box (translucent
	// by Pref_bTranslucency, Prefs_Apply 0x452d70; Options and Help cover it and it takes no clicks then), the location
	// card and head credits, captions, the subliminal word, then the layout items over all of it
	_interface->drawBar(_screen);
	_dialogBox.draw(_screen, _mouse, !getPreference(kPrefTranslucency));
	_captions.drawCard(_screen);
	_headCredits.draw(_screen);
	for (uint i = 0; i < _layers.size(); i++)
		drawCaptions(*_layers[i], getBandPosition(*_layers[i]));
	_subliminal.draw(_screen);
	_interface->drawItems(_screen);
	if (_dragIdea && _dragMoved)
		_interface->drawDragged(_screen, _dragIdea, _mouse);
	drawPhotos();
	// PlayAssetAt clips: group +0x220 key 0x16, over every layout item, the dialog box, captions and the flashlight light
	_sound.drawClips(_screen, true);
}

const Graphics::Surface *XFilesEngine::getPhotoPicture(const VCPhoto &photo, int w, int h) {
	Common::String key = Common::String::format("%u/%u/%d,%d,%d,%d/%dx%d", photo.asset, photo.clipTime, photo.crop[0], photo.crop[1], photo.crop[2], photo.crop[3], w, h);
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

static void readPixel(const Graphics::Surface &s, int x, int y, byte &r, byte &g, byte &b) {
	const byte *p = (const byte *)s.getBasePtr(x, y);
	s.format.colorToRGB(s.format.bytesPerPixel == 2 ? *(const uint16 *)p : *(const uint32 *)p, r, g, b);
}

static void writePixel(Graphics::Surface &s, int x, int y, byte r, byte g, byte b) {
	byte *p = (byte *)s.getBasePtr(x, y);
	uint32 value = s.format.RGBToColor(r, g, b);
	if (s.format.bytesPerPixel == 2)
		*(uint16 *)p = value;
	else
		*(uint32 *)p = value;
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
