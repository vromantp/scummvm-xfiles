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
#include "common/system.h"
#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/fonts/ttf.h"
#include "graphics/surface.h"

#include "xfiles/media/movie.h"
#include "xfiles/media/pictures.h"
#include "xfiles/script/script.h"
#include "xfiles/script/vcdata.h"
#include "xfiles/ui/prefs.h"
#include "xfiles/ui/view_band.h"
#include "xfiles/xfiles.h"

namespace XFiles {

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
		// System bold is a bitmap font in Windows: never smoothed (docs/plans/B-350.md)
		_counterFont = Graphics::loadTTFFontFromArchive("LiberationSans-Bold.ttf", kCounterFontSize, Graphics::kTTFSizeModeCharacter, 0, 0,
			smoothTextMode(getPreference(kPrefSmoothText), kGdiSystem, kCounterFontSize, true));
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
