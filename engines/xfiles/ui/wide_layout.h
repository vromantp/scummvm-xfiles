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

#ifndef XFILES_WIDE_LAYOUT_H
#define XFILES_WIDE_LAYOUT_H

#include "common/array.h"
#include "common/rect.h"
#include "graphics/surface.h"

namespace XFiles {

/**
 * The widescreen UI placement (B-354, docs/plans/B-352.md "Candidate layouts" B). The game draws its UI into the 640x480
 * screen as always; while the band is zoomed, each UI element is recorded right after it is drawn (the pixels it
 * changed, with the offset the layout gives it) and the canvas pastes every record at its place over the scaled band.
 * The hit rects of the interactive records and the band map take the mouse back to the screen (toScreen).
 */
class WideLayout {
public:
	/** The drawn elements, in the order the UI pass draws them. */
	enum Element {
		kOther,    ///< Whatever no mark names: the frame offset
		kAIIcon,   ///< Top row, left of the X corner
		kIdeas,    ///< Idea icons, over the band's top left corner
		kIdeaText, ///< The idea icons' rollover text: over the video, so on a black backing
		kEvidence, ///< Evidence row and emotion pane: the left group
		kEmotions,
		kActions,   ///< Action pane: the right group
		kInventory, ///< Inventory bar, icons aligned with the band
		kKit,
		kBox,        ///< Dialog box: the mapped character centre, clamped to the band
		kCard,       ///< Location card: the band's bottom left corner
		kCredits,    ///< Head credits: the band's bottom centre
		kCaptions,   ///< Under the band, centred
		kSubliminal, ///< The band's centre
		kDrag        ///< The dragged idea: at the canvas mouse
	};

	WideLayout() : _active(false) {}

	/** Start recording a zoomed frame: the screen as the band pass left it. */
	void begin(const Graphics::Surface &screen);
	/** Stop recording; the records stay for the mouse and the shots until the next begin. */
	void end() { _active = false; }
	/** Forget the records and the recording (the canvas size changed, B-356). */
	void reset() {
		_active = false;
		_layers.clear();
		_prev.free();
		_mouseOffset = Common::Point();
	}
	bool isActive() const { return _active; }
	/** Record what changed on the screen since the last mark as this element (no-op when not recording). */
	void mark(Element element, const Graphics::Surface &screen);
	/**
	 * Record the dialog box (B-359). black and white are the box drawn alone over a black and over a white 32 bit surface
	 * (null: not translucent, or not recorded); from the two the box's colour and alpha follow, and compose blends it
	 * over the scaled band instead of pasting the pixels blended over the 1x band. screen is as for mark().
	 */
	void markBox(const Graphics::Surface &screen, const Graphics::Surface *black, const Graphics::Surface *white);
	/** The dragged image's offset: canvas mouse minus screen mouse. */
	void setMouseOffset(const Common::Point &offset) { _mouseOffset = offset; }

	/** Paste the records over the canvas (the scaled band is already there). */
	void compose(Graphics::Surface &canvas) const;
	/** Canvas point to screen point, for a canvas with the zoomed band (-1,-1: the dead gap between the pane groups, B-360). */
	Common::Point toScreen(const Common::Point &canvas) const;
	/** Screen point to canvas point, for a canvas with the zoomed band. */
	static Common::Point toCanvas(const Common::Point &screen);

private:
	struct Layer {
		Element element;
		Common::Rect src;         ///< Bounding box of the changed pixels, screen coordinates
		Common::Point offset;     ///< Canvas position minus screen position
		Common::Array<byte> px;   ///< The box's pixels
		Common::Array<byte> mask; ///< One byte per pixel: changed
		Common::Array<byte> alpha; ///< The translucent box: one byte per pixel, blended over the canvas (else empty)
		bool backing;             ///< A black backing under the box (text over the video)
	};

	static Common::Point offsetOf(Element element);
	static bool interactive(Element element);
	static Common::Point boxOffset(const Common::Rect &box);

	bool _active;
	Graphics::Surface _prev;
	Common::Array<Layer> _layers;
	Common::Point _mouseOffset;
};

} // End of namespace XFiles

#endif
