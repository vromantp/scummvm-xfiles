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

#include "xfiles/ui/wide_layout.h"
#include "xfiles/ui/view_band.h"

namespace XFiles {

// Screen to canvas offsets of the B table (the canvas is 854x480, the screen 640x480; the band 20..620 x 90..330 is
// zoomed 1.25x into 52..802 x 34..334)
static const int kFrameDx = (kWideWidth - kScreenWidth) / 2;      // 107: the screen centred
static const int kLeftDx = kZoomLeft - kBandLeft;                 // 32: aligned with the band's left edge
static const int kRightDx = kZoomLeft + kZoomWidth - kBandRight;  // 182: aligned with the band's right edge
static const int kBelowDy = kZoomTop + kZoomHeight - kBandBottom; // 4: under the band's bottom edge
// The X corner's rectangle (XCorner_ctor) moves to the canvas top right corner
static const int kXCornerDx = kWideWidth - kScreenWidth;
static const int kBarTop = 420;

static Common::Rect xCorner() {
	return Common::Rect(501, 0, kScreenWidth, 34);
}

Common::Point WideLayout::offsetOf(Element element) {
	switch (element) {
	case kAIIcon:
		return Common::Point(115, -46);
	case kIdeas:
	case kIdeaText:
		return Common::Point(kLeftDx, 26);
	case kEvidence:
	case kEmotions:
	case kCard:
		return Common::Point(kLeftDx, kBelowDy);
	case kActions:
		return Common::Point(kRightDx, kBelowDy);
	case kInventory:
	case kKit:
		return Common::Point(kLeftDx, 0);
	case kCredits:
	case kCaptions:
		return Common::Point(kFrameDx, kBelowDy);
	case kSubliminal:
		return Common::Point(kFrameDx, (kZoomTop + kZoomHeight / 2) - (kBandTop + kBandHeight / 2));
	default:
		return Common::Point(kFrameDx, 0);
	}
}

bool WideLayout::interactive(Element element) {
	return element == kAIIcon || element == kIdeas || element == kEvidence || element == kEmotions || element == kActions ||
		   element == kInventory || element == kKit || element == kBox;
}

void WideLayout::begin(const Graphics::Surface &screen) {
	_active = true;
	_layers.clear();
	_prev.free();
	_prev.copyFrom(screen);
}

Common::Point WideLayout::boxOffset(const Common::Rect &box) {
	int centre = kZoomLeft + ((box.left + box.right) / 2 - kBandLeft) * 5 / 4;
	int left = CLIP<int>(centre - box.width() / 2, kZoomLeft, kZoomLeft + kZoomWidth - box.width());
	return Common::Point(left - box.left, kBelowDy);
}

void WideLayout::mark(Element element, const Graphics::Surface &screen) {
	if (!_active)
		return;
	const int bpp = screen.format.bytesPerPixel;
	int minX = screen.w, maxX = 0, minY = screen.h, maxY = 0;
	for (int y = 0; y < screen.h; y++) {
		const byte *a = (const byte *)_prev.getBasePtr(0, y);
		const byte *b = (const byte *)screen.getBasePtr(0, y);
		if (!memcmp(a, b, screen.w * bpp))
			continue;
		int first = 0, last = screen.w - 1;
		while (!memcmp(a + first * bpp, b + first * bpp, bpp))
			first++;
		while (!memcmp(a + last * bpp, b + last * bpp, bpp))
			last--;
		minX = MIN(minX, first);
		maxX = MAX(maxX, last + 1);
		minY = MIN(minY, y);
		maxY = y + 1;
	}
	if (maxX <= minX || maxY <= minY)
		return;
	Common::Rect box(minX, minY, maxX, maxY);
	Layer layer;
	layer.element = element;
	layer.src = box;
	layer.backing = element == kIdeaText;
	layer.offset = offsetOf(element);
	if (element == kDrag) {
		layer.offset = _mouseOffset;
	} else if (element == kBox) {
		// The box's centre follows the character: the band's mapping of its centre, kept inside the band
		layer.offset = boxOffset(box);
	}
	layer.px.resize(box.width() * box.height() * bpp);
	layer.mask.resize(box.width() * box.height());
	for (int y = box.top; y < box.bottom; y++) {
		const byte *a = (const byte *)_prev.getBasePtr(box.left, y);
		const byte *b = (const byte *)screen.getBasePtr(box.left, y);
		uint at = (y - box.top) * box.width();
		memcpy(&layer.px[at * bpp], b, box.width() * bpp);
		for (int x = 0; x < box.width(); x++)
			layer.mask[at + x] = memcmp(a + x * bpp, b + x * bpp, bpp) != 0;
		memcpy((byte *)_prev.getBasePtr(box.left, y), b, box.width() * bpp);
	}
	_layers.push_back(layer);
}

void WideLayout::markBox(const Graphics::Surface &screen, const Graphics::Surface *black, const Graphics::Surface *white) {
	uint before = _layers.size();
	mark(kBox, screen); // keeps the screen diff current; the layer it adds is the fallback
	if (!_active || !black || !white || _layers.size() == before)
		return;
	// alpha per channel: white - black = 255 - a (the box over white and over black); colour = black * 255 / a
	const int w = black->w, h = black->h;
	Common::Array<byte> alpha(w * h), rgb(w * h * 3);
	int minX = w, maxX = 0, minY = h, maxY = 0;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			byte br, bg, bb, wr, wg, wb;
			black->format.colorToRGB(black->getPixel(x, y), br, bg, bb);
			white->format.colorToRGB(white->getPixel(x, y), wr, wg, wb);
			int a = 255 - ((wr - br) + (wg - bg) + (wb - bb)) / 3;
			a = CLIP(a, 0, 255);
			uint at = y * w + x;
			alpha[at] = a;
			if (!a)
				continue;
			rgb[at * 3] = MIN(br * 255 / a, 255);
			rgb[at * 3 + 1] = MIN(bg * 255 / a, 255);
			rgb[at * 3 + 2] = MIN(bb * 255 / a, 255);
			minX = MIN(minX, x);
			maxX = MAX(maxX, x + 1);
			minY = MIN(minY, y);
			maxY = y + 1;
		}
	}
	if (maxX <= minX || maxY <= minY)
		return;
	Common::Rect box(minX, minY, maxX, maxY);
	Layer &layer = _layers.back();
	const int bpp = screen.format.bytesPerPixel;
	layer.src = box;
	layer.offset = boxOffset(box);
	layer.px.resize(box.width() * box.height() * bpp);
	layer.mask.clear();
	layer.alpha.resize(box.width() * box.height());
	for (int y = 0; y < box.height(); y++) {
		for (int x = 0; x < box.width(); x++) {
			uint from = (box.top + y) * w + box.left + x, at = y * box.width() + x;
			layer.alpha[at] = alpha[from];
			uint32 color = screen.format.RGBToColor(rgb[from * 3], rgb[from * 3 + 1], rgb[from * 3 + 2]);
			if (bpp == 4)
				WRITE_UINT32(&layer.px[at * bpp], color);
			else
				WRITE_UINT16(&layer.px[at * bpp], color);
		}
	}
}

void WideLayout::compose(Graphics::Surface &canvas) const {
	const int bpp = canvas.format.bytesPerPixel;
	for (uint i = 0; i < _layers.size(); i++) {
		const Layer &layer = _layers[i];
		Common::Rect dest = layer.src;
		dest.translate(layer.offset.x, layer.offset.y);
		if (layer.backing) {
			Common::Rect backing = dest;
			backing.grow(2);
			backing.clip(Common::Rect(canvas.w, canvas.h));
			if (!backing.isEmpty())
				canvas.fillRect(backing, 0);
		}
		for (int y = 0; y < layer.src.height(); y++) {
			int cy = dest.top + y;
			if (cy < 0 || cy >= canvas.h)
				continue;
			for (int x = 0; x < layer.src.width(); x++) {
				int cx = dest.left + x;
				uint at = y * layer.src.width() + x;
				if (!layer.alpha.empty()) {
					byte a = layer.alpha[at];
					if (!a || cx < 0 || cx >= canvas.w)
						continue;
					uint32 color = bpp == 4 ? READ_UINT32(&layer.px[at * bpp]) : READ_UINT16(&layer.px[at * bpp]);
					if (a != 255) {
						byte sr, sg, sb, dr, dg, db;
						canvas.format.colorToRGB(color, sr, sg, sb);
						canvas.format.colorToRGB(canvas.getPixel(cx, cy), dr, dg, db);
						color = canvas.format.RGBToColor((sr * a + dr * (255 - a)) / 255, (sg * a + dg * (255 - a)) / 255,
							(sb * a + db * (255 - a)) / 255);
					}
					canvas.setPixel(cx, cy, color);
					continue;
				}
				if (layer.mask[at] && cx >= 0 && cx < canvas.w)
					memcpy(canvas.getBasePtr(cx, cy), &layer.px[at * bpp], bpp);
			}
		}
	}
}

Common::Point WideLayout::toScreen(const Common::Point &canvas) const {
	Common::Point screen;
	bool found = false;
	// The interactive records first, the last drawn on top
	for (int i = (int)_layers.size() - 1; i >= 0 && !found; i--) {
		const Layer &layer = _layers[i];
		Common::Rect dest = layer.src;
		dest.translate(layer.offset.x, layer.offset.y);
		if (interactive(layer.element) && dest.contains(canvas)) {
			screen = Common::Point(canvas.x - layer.offset.x, canvas.y - layer.offset.y);
			found = true;
		}
	}
	if (!found) {
		Common::Rect corner = xCorner();
		corner.translate(kXCornerDx, 0);
		if (corner.contains(canvas)) {
			screen = Common::Point(canvas.x - kXCornerDx, canvas.y);
		} else if (canvas.x >= kZoomLeft && canvas.x < kZoomLeft + kZoomWidth && canvas.y >= kZoomTop &&
				   canvas.y < kZoomTop + kZoomHeight) {
			// The band through the inverse scale (pixel-centre rounding)
			screen = Common::Point(kBandLeft + (4 * (canvas.x - kZoomLeft) + 2) / 5, kBandTop + (4 * (canvas.y - kZoomTop) + 2) / 5);
		} else {
			// The strips round the band: the bar's hover zone takes the full width, a pane's empty part its group's offset
			Common::Point offset(kFrameDx, 0);
			if (canvas.y >= kBarTop)
				offset = offsetOf(kInventory);
			else if (canvas.y >= kZoomTop + kZoomHeight)
				offset = offsetOf(canvas.x < kWideWidth / 2 ? kEmotions : kActions);
			else if (canvas.y < kZoomTop && canvas.x >= 645)
				offset = offsetOf(kAIIcon);
			screen = Common::Point(canvas.x - offset.x, canvas.y - offset.y);
		}
	}
	return Common::Point(CLIP<int>(screen.x, 0, kScreenWidth - 1), CLIP<int>(screen.y, 0, kScreenHeight - 1));
}

Common::Point WideLayout::toCanvas(const Common::Point &screen) {
	if (screen.x >= kBandLeft && screen.x < kBandRight && screen.y >= kBandTop && screen.y < kBandBottom)
		return Common::Point(kZoomLeft + (screen.x - kBandLeft) * 5 / 4, kZoomTop + (screen.y - kBandTop) * 5 / 4);
	Common::Point offset(kFrameDx, 0);
	if (xCorner().contains(screen))
		offset = Common::Point(kXCornerDx, 0);
	else if (screen.y >= kBarTop)
		offset = offsetOf(kInventory);
	else if (screen.y >= kBandBottom)
		offset = offsetOf(screen.x < kScreenWidth / 2 ? kEmotions : kActions);
	return Common::Point(screen.x + offset.x, screen.y + offset.y);
}

} // End of namespace XFiles
