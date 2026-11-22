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

#ifndef XFILES_VIEW_BAND_H
#define XFILES_VIEW_BAND_H

#include "graphics/surface.h"

namespace XFiles {

enum {
	kScreenWidth = 640,
	kScreenHeight = 480,
	kWideWidth = 854, // the widescreen canvas (B-352), 16:9
	// The view band: navigation frames and videos are 600x240 at (20, 90);
	// hotspot rectangles are in screen coordinates
	kBandLeft = 20,
	kBandTop = 90,
	kBandWidth = 600,
	kBandHeight = 240,
	kBandRight = kBandLeft + kBandWidth,
	kBandBottom = kBandTop + kBandHeight
};

inline void readPixel(const Graphics::Surface &s, int x, int y, byte &r, byte &g, byte &b) {
	const byte *p = (const byte *)s.getBasePtr(x, y);
	s.format.colorToRGB(s.format.bytesPerPixel == 2 ? *(const uint16 *)p : *(const uint32 *)p, r, g, b);
}

inline void writePixel(Graphics::Surface &s, int x, int y, byte r, byte g, byte b) {
	byte *p = (byte *)s.getBasePtr(x, y);
	uint32 value = s.format.RGBToColor(r, g, b);
	if (s.format.bytesPerPixel == 2)
		*(uint16 *)p = value;
	else
		*(uint32 *)p = value;
}

} // End of namespace XFiles

#endif
