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
#include "common/events.h"
#include "common/formats/winexe.h"
#include "common/system.h"

#include "xfiles/xfiles.h"
#include "xfiles/media/movie.h"
#include "xfiles/script/script.h"
#include "xfiles/script/vcdata.h"

namespace XFiles {

enum {
	kScreenWidth = 640,
	kScreenHeight = 480,
	// The view band: navigation frames and videos are 600x240 at (20, 90);
	// hotspot rectangles are in screen coordinates
	kBandLeft = 20,
	kBandTop = 90,
	kBandWidth = 600,
	kBandHeight = 240,
	// Two clicks at most this far apart in time and space make a double-click
	// (the Windows defaults)
	kDoubleClickMs = 500,
	kDoubleClickDistance = 4
};

// The X corner, XCorner_ctor's rectangle (right and bottom exclusive)
static const int kMenuCornerLeft = 501, kMenuCornerBottom = 34;
// Its cursor, the VCCursor named "X"
static const uint32 kXCursor = 25541;

void XFilesEngine::updateInterface(bool underMovie) {
	bool hiddenBar = isBarHidden();
	_interface->setAIGlow(_script->isHintGlowing());
	// A modal layout closing resumes the AI icon when a hint is available (0x509520); it never suspends it
	bool modal = _interface->isModal();
	if (_wasModal && !modal)
		_script->resumeAIIcon();
	_wasModal = modal;
	_script->aiSoundEndCheck();
	_interface->setAISuspended(_script->isAISuspended());
	_interface->update(*_script, _mouse, getPreference(kPrefClosedCaptioning), hiddenBar, getPreference(kPrefRolloverText));
	// Roll Into and Roll Out Of for the icons and layout items under the
	// mouse (the evidence kit icon opens the kit from these)
	Interface::Hit hover = _interface->hitTest(_mouse);
	// B-290: under a movie the mouse inside the video's rect changes no roll state (the exe's sound object takes
	// the hit): the rolls and the character view stay as they were
	if (underMovie) {
		Common::Rect rect = movieRect();
		if (rect.isEmpty() || rect.contains(_mouse))
			return;
	}
	if (hover.kind != _hover.kind || hover.id != _hover.id) {
		Interface::Hit old = _hover;
		_hover = hover;
		if (old.kind != Interface::kHitNone)
			_script->roll(old.id, false);
		if (hover.kind != Interface::kHitNone)
			_script->roll(hover.id, true);
	}
	// Roll Into and Roll Out Of for the character view under the mouse (CharView_OnRoll 0x443010: the gun,
	// handcuffs or stiletto in hand make a character with stock replies say "Don't do that")
	uint32 charHover = 0;
	if (hover.kind == Interface::kHitNone && !_interface->isModal() && !_script->binocularsBlock(_mouse)) {
		ViewHit viewHit = hitView(_mouse);
		if (viewHit.kind == ViewHit::kCharView)
			charHover = viewHit.id;
	}
	if (charHover != _charHover) {
		uint32 old = _charHover;
		_charHover = charHover;
		if (old)
			_script->roll(old, false);
		if (charHover)
			_script->roll(charHover, true);
	}
	// The item in hand goes back when the mouse moves into the bar
	// (InvBar_OnRoll 0x468330); a disabled one goes back in Script::enable
	if (isHolding() && _interface->hasEnteredBar())
		putBack();
}

bool XFilesEngine::isHolding() const {
	return _script->isHolding();
}

void XFilesEngine::putBack() {
	_script->putBack();
}

bool XFilesEngine::queryInventoryIcon(uint32 id, bool &hasIcon) const {
	hasIcon = _interface->hasInventoryIcon(id);
	return true;
}

void XFilesEngine::heldItemChanged(uint32 id) {
	if (id && _dialogBox.isOpen()) {
		_dialogBox.close();
		_script->closeBox();
	}
	_interface->setHeld(id);
}

bool XFilesEngine::delayPlay(uint32 asset, uint ms) {
	DelayedPlay play = { asset, gameMillis() + ms };
	debugC(1, kDebugScript, "-- play of #%u in %u ms", asset, ms);
	_delayedPlays.push_back(play);
	return true;
}

void XFilesEngine::showInventoryBar() {
	// SelectInv 0x50a390: Queue_Show of the bar when hidden. It stays like a new item's reveal, then hides
	bool hiddenBar = _preferences.variable(kPrefInventory) && _script->getVariable(_preferences.variable(kPrefInventory)) == 'H';
	if (!hiddenBar || _interface->isBarOpen() || !_fastForward.empty())
		return;
	_barRevealed = true;
	_barRevealEnd = gameMillis() + kRevealHideDelay;
}

bool XFilesEngine::deferInventoryIcon(uint32 id) {
	// 0x4a1fb0: with the bar shown (the "visible" preference, the mouse
	// over it, an earlier item's reveal) the icon is appended at once;
	// else the bar is shown now, the icon is added by a 500 ms timer and
	// a second one hides the bar 1000 ms later. The save keeps the order
	// the icons joined in. A fast-forward has no time: the sim's way
	// (append, steps.py's `icon:` steps reorder)
	if (!_fastForward.empty())
		return false;
	bool hiddenBar = _preferences.variable(kPrefInventory) && _script->getVariable(_preferences.variable(kPrefInventory)) == 'H';
	if (!hiddenBar || _interface->isBarOpen())
		return false;
	uint32 now = gameMillis();
	DelayedPlay icon = { id, now + kIconJoinDelay };
	_pendingIcons.push_back(icon);
	_barRevealed = true;
	_barRevealEnd = now + kIconJoinDelay + kRevealHideDelay;
	debugC(1, kDebugScript, "-- icon #%u joins the bar in %u ms", id, kIconJoinDelay);
	return true;
}

void XFilesEngine::flushPendingIcons() {
	_pendingIcons.clear();
	_barRevealed = false;
}

void XFilesEngine::clickInventory(uint32 inventory) {
	// InvIcon_OnMouseClick (0x49c180); Script does it, heldItemChanged
	// closes the box and takes the cursor
	if (inventory == _interface->getHeld())
		return;
	_script->selectInventory(inventory);
}

static bool inMenuCorner(const Common::Point &pos) {
	return pos.x >= kMenuCornerLeft && pos.x < kScreenWidth && pos.y >= 0 && pos.y < kMenuCornerBottom;
}

static bool containsPoint(const VCRect &r, const Common::Point &pos) {
	return pos.x >= r.left && pos.x < r.right && pos.y >= r.top && pos.y < r.bottom;
}

void XFilesEngine::handleClick(const Common::Point &pos) {
	Interface::Hit hit = _interface->hitTest(pos);
	uint32 now = gameMillis();
	bool doubleClick = now - _lastClickTime <= kDoubleClickMs && hit.kind != Interface::kHitNone &&
		hit.kind == _lastClickHit.kind && hit.id == _lastClickHit.id &&
		ABS(pos.x - _lastClickPos.x) <= kDoubleClickDistance && ABS(pos.y - _lastClickPos.y) <= kDoubleClickDistance;
	_lastClickTime = doubleClick ? 0 : now;
	_lastClickPos = pos;
	_lastClickHit = hit;
	if (doubleClick) {
		handleDoubleClick(hit);
		return;
	}

	if (hit.kind == Interface::kHitItem) {
		debugC(1, kDebugScript, "-- click:%u (interface item)", hit.id);
		_script->press(hit.id);
		return;
	}

	// Over Options and Help the scene stays suspended: only the layout's items take clicks
	if (_menuLayout)
		return;

	// The camera in hand takes a click in the band (kind 24 comes after the interface items, before the X corner and
	// the dialog box)
	if (hit.kind == Interface::kHitNone && !_interface->isModal() && _script->isCameraEnabled() && pos.x >= kBandLeft &&
		pos.x < kBandLeft + kBandWidth && pos.y >= kBandTop && pos.y < kBandTop + kBandHeight) {
		debugC(1, kDebugScript, "-- photo:%d,%d", pos.x, pos.y);
		_script->cameraClick(pos);
		return;
	}

	// The binoculars in hand take every click in their rect (before the X corner, the dialog box, .HOT and the view)
	if (hit.kind == Interface::kHitNone && !_interface->isModal() && _script->binocularsBlock(pos)) {
		debugC(1, kDebugScript, "-- click: binoculars take it");
		return;
	}

	// The X corner (kind 26) comes after the interface items, before the
	// dialog box and the modal layouts
	if (hit.kind == Interface::kHitNone && !_dialogBox.contains(pos) && inMenuCorner(pos)) {
		openGameMenu();
		return;
	}

	if (_dialogBox.isOpen()) {
		// A click outside the box closes it (assumed; DisplayDBox(0) also
		// does); one on an icon goes on to the icon
		uint32 topic = 0;
		DialogBox::Hit boxHit = _dialogBox.click(pos, topic);
		if (boxHit == DialogBox::kHitTopic) {
			askTopic(topic);
			return;
		}
		if (boxHit == DialogBox::kHitInside)
			return;
		// An emotion or action icon leaves the box open (box-click-kinds); an inventory item or the kit closes it
		if (hit.kind != Interface::kHitEmotion && hit.kind != Interface::kHitAction &&
				hit.kind != Interface::kHitIdea) {
			_dialogBox.close();
			_script->closeBox();
		}
		if (hit.kind == Interface::kHitNone)
			return;
	}

	switch (hit.kind) {
	case Interface::kHitInventory:
		clickInventory(hit.id);
		return;
	case Interface::kHitEmotion:
		debugC(1, kDebugScript, "-- click:%u (emotion icon)", hit.id);
		_script->click(hit.id);
		return;
	case Interface::kHitAction:
		debugC(1, kDebugScript, "-- click:%u (action icon)", hit.id);
		_script->click(hit.id);
		return;
	case Interface::kHitEvidence:
		debugC(1, kDebugScript, "-- click:%u (evidence kit)", hit.id);
		_script->click(hit.id);
		return;
	case Interface::kHitEvidenceRow:
		debugC(1, kDebugScript, "-- click:%u (evidence row)", hit.id);
		_script->click(hit.id);
		return;
	case Interface::kHitIdea:
		// The icon is held until the button goes up: on a character it plays that character's topic, in place it plays
		// the open box's (IdeaIcon_DragEnd)
		_dragIdea = hit.id;
		_dragStart = pos;
		_dragMoved = false;
		return;
	case Interface::kHitAIIcon:
		// AIIconObj's click (0x5091d0): a glowing icon goes to its level's view (0x509aa0); without a glow the exe runs
		// the next hint-list action whose condition holds (0x5097e0)
		if (_script->isHintGlowing()) {
			debugC(1, kDebugScript, "-- click: AI icon glowing, level %d", _script->getHintGlowLevel());
			_script->clickAIIcon();
		} else {
			debugC(1, kDebugScript, "-- click: AI icon without a glow");
			_script->clickAIHint();
		}
		return;
	default:
		break;
	}
	if (_interface->isModal())
		return; // A layout has the input

	// A live .HOT entry takes the click (kind 29 objects come before the view's own), also when nothing happens
	if (uint32 kind = hotKindAt(pos)) {
		debugC(1, kDebugScript, "-- hot:%u", kind);
		_script->hotClick(kind);
		return;
	}

	ViewHit viewHit = hitView(pos);
	switch (viewHit.kind) {
	case ViewHit::kExplorable:
		debugC(1, kDebugScript, "-- click:%u (explorable)", viewHit.id);
		_script->click(viewHit.id);
		break;
	case ViewHit::kCharView: {
		debugC(1, kDebugScript, "-- click:%u (character view)", viewHit.id);
		// The view's triggers handle inventory items held over the
		// character; the exe opens the dialog box unless a reaction clip
		// plays for the item in hand
		_script->click(viewHit.id, &_mouse);
		VCCharView cv;
		if (_script->opensBoxOnClick() && _data->getCharView(viewHit.id, cv))
			openDialogBox(cv);
		break;
	}
	case ViewHit::kNav:
		_script->setClickPoint(_mouse);
		_script->step(Common::String::format("nav:%u", viewHit.id));
		break;
	default:
		// The gun in hand takes a click nothing else took (its object is the top one, 0x4cb520)
		if (_script->isGunHeld())
			_script->gunClick();
		break;
	}
}

Common::String XFilesEngine::stepAt(const Common::Point &pos) const {
	// The order of handleClick, without its effects; what a click does with no video playing
	Interface::Hit hit = _interface->hitTest(pos);
	if (hit.kind == Interface::kHitItem)
		return Common::String::format("click:%u", hit.id);
	if (_menuLayout || _menuOpen)
		return Common::String();
	if (hit.kind == Interface::kHitNone && !_dialogBox.contains(pos) && inMenuCorner(pos))
		return Common::String();
	if (_dialogBox.isOpen()) {
		if (uint32 topic = _dialogBox.topicAt(pos))
			return Common::String::format("ask:%u", topic);
		if (_dialogBox.contains(pos) || hit.kind == Interface::kHitNone)
			return Common::String();
	}
	switch (hit.kind) {
	case Interface::kHitInventory:
	case Interface::kHitEmotion:
	case Interface::kHitAction:
	case Interface::kHitEvidence:
	case Interface::kHitEvidenceRow:
		return Common::String::format("click:%u", hit.id);
	case Interface::kHitAIIcon:
		return "aiclick";
	case Interface::kHitIdea:
		return Common::String(); // a drag, not a click
	default:
		break;
	}
	if (_interface->isModal() || hotKindAt(pos))
		return Common::String();
	ViewHit viewHit = hitView(pos);
	switch (viewHit.kind) {
	case ViewHit::kExplorable:
	case ViewHit::kCharView:
		return Common::String::format("click:%u", viewHit.id);
	case ViewHit::kNav:
		return Common::String::format("nav:%u", viewHit.id);
	default:
		return Common::String();
	}
}

void XFilesEngine::logStepPoint(const Common::String &step) {
	// click, dclick, rollin and rollout of an object share the point of its click
	Common::String want = step;
	uint colon = step.findFirstOf(':');
	if (step.hasPrefix("dclick:") || step.hasPrefix("rollin:") || step.hasPrefix("rollout:"))
		want = "click:" + step.substr(colon + 1);
	// A hidden inventory bar opens while the mouse is over it: open it for the scan, the point is sent with a move there
	bool hiddenBar = isBarHidden();
	_interface->updateBar(Common::Point(kScreenWidth / 2, kScreenHeight - 10), hiddenBar);
	// Every grid point (cell centres of 4 pixels) that gives the step; the one nearest the matches' centre is the point
	Common::Array<Common::Point> found;
	int64 sumX = 0, sumY = 0;
	for (int y = 2; y < kScreenHeight; y += 4) {
		for (int x = 2; x < kScreenWidth; x += 4) {
			Common::Point pos(x, y);
			Common::String got = stepAt(pos);
			// A character view or explorable object both give click:ID
			if (got == want) {
				found.push_back(pos);
				sumX += x;
				sumY += y;
			}
		}
	}
	_interface->updateBar(_mouse, hiddenBar);
	if (found.empty()) {
		debugC(1, kDebugScript, "-- point %s off-screen", step.c_str());
		return;
	}
	int cx = (int)(sumX / (int64)found.size()), cy = (int)(sumY / (int64)found.size());
	Common::Point best = found[0];
	int bestDist = 0x7fffffff;
	for (uint i = 0; i < found.size(); i++) {
		int dx = found[i].x - cx, dy = found[i].y - cy;
		if (dx * dx + dy * dy < bestDist) {
			bestDist = dx * dx + dy * dy;
			best = found[i];
		}
	}
	debugC(1, kDebugScript, "-- point %s %d,%d", step.c_str(), best.x, best.y);
}

XFilesEngine::ViewHit XFilesEngine::hitView(const Common::Point &pos, bool skipCharViews) const {
	ViewHit hit;
	hit.kind = ViewHit::kNone;
	hit.id = 0;
	hit.cursor = 0;
	VCView view;
	if (!_data->getView(_script->getView(), view))
		return hit;
	Common::Array<uint32> ids;
	_data->getMembers(view.explorables, ids);
	for (uint i = 0; i < ids.size(); i++) {
		VCExplorable e;
		if (_script->isEnabled(ids[i]) && _data->getExplorable(ids[i], e) && containsPoint(e.rect, pos)) {
			hit.kind = ViewHit::kExplorable;
			hit.id = ids[i];
			hit.cursor = e.cursor;
			return hit;
		}
	}
	ids.clear();
	if (!skipCharViews)
		_data->getMembers(view.characters, ids);
	for (uint i = 0; i < ids.size(); i++) {
		VCCharView cv;
		if (_script->isEnabled(ids[i]) && _data->getCharView(ids[i], cv) && containsPoint(cv.rect, pos)) {
			hit.kind = ViewHit::kCharView;
			hit.id = ids[i];
			hit.cursor = cv.cursor;
			return hit;
		}
	}
	ids.clear();
	_data->getMembers(view.navs, ids);
	for (uint i = 0; i < ids.size(); i++) {
		VCNav nav;
		VCHotSpot hotSpot;
		if (_script->isEnabled(ids[i]) && _data->getNav(ids[i], nav) && _data->getHotSpot(nav.hotSpot, hotSpot) &&
				containsPoint(hotSpot.rect, pos)) {
			hit.kind = ViewHit::kNav;
			hit.id = ids[i];
			hit.cursor = hotSpot.cursor;
			return hit;
		}
	}
	return hit;
}

void XFilesEngine::updateCursor() {
	// An object shows its VCCursor while the mouse is over it (vtable slot
	// +0x68, called on Roll Into and Roll Out Of); 0 is the default cursor
	// The firing cursor stays until the hit view is gone (the next view, see showCursorAnimation)
	if (_firingCursor && _script->getView() != _firingView)
		_firingCursor = 0;
	// Over AlienBlast's white no view or nav is live (AlienBlast 0x4b6220 cancels the queued activations), so the
	// refresh (kind 16, 0x4468d0) finds nothing and shows the default cursor (observed in the original, B-284)
	uint32 cursor = _whiteHold ? 0 : getCursorAt(_mouse);
	// With the gun (inventory value 9) in hand the default cursor is the tool sight, group 132 (screen +0x960/+0x96c,
	// 0x5105d0, 0x4463b0), wherever the default would show
	if (!cursor && !_whiteHold && _script->isGunHeld())
		cursor = Cursors::kToolCursorId;
	_cursors->show(cursor);
	_cursors->update(getPreference(kPrefTranslucency));
}

void XFilesEngine::reshowAIIcon() {
	// 0x493090: the show handler (0x509280) re-applies the glow from the level; the icon exists while Artificial
	// Intuition is on (Prefs_ApplyAIIcon); the suspension re-check (0x508e40) follows the show handler
	if (_script->getPreferenceValue(kPrefArtificialIntuition) != 0) {
		_script->aiIconShown();
		_script->recheckAIIcon();
	}
}

void XFilesEngine::showCursorAnimation(uint32 id) {
	// HotSpot_ClickAction (0x4b4a20) activates the VCCursor (slot +0x70): it is the cursor until an object under the
	// mouse shows one, in practice after the hit view is replaced
	_firingCursor = id;
	_firingView = _script->getView();
}

uint32 XFilesEngine::getCursorAt(const Common::Point &pos) const {
	if (_firingCursor)
		return _firingCursor;
	// An idea icon being dragged is the pointer: no character's lips over it
	if (_dragIdea && _dragMoved)
		return 0;
	uint32 held = isHolding() ? _interface->getHeldCursor() : 0;
	// B-290: the bar is live under a playing video (its icons show the crosshair as their own cursor)
	Interface::Hit hit = _interface->hitTest(pos);
	if (hit.kind == Interface::kHitItem) {
		// IfaceItem_ShowCursor (0x4469e0): the item's own, also with an item in hand
		VCInterfaceItem item;
		return _data->getInterfaceItem(hit.id, item) ? item.cursor : 0;
	}
	if (hit.kind != Interface::kHitNone)
		return 0; // Icons show the default cursor (0x46e1d0 reads a field that is always 0)
	if (_dialogBox.contains(pos))
		return 0; // Topics in the box show the default cursor (0x478940)
	if (inMenuCorner(pos))
		return kXCursor; // The X corner object (slot +0x68 looks up the VCCursor "X")
	if (_interface->isModal())
		return held;
	// The binoculars in hand take the hover in their rect: no nav or object cursor (the exe's small crosshair is not drawn)
	if (_script->binocularsBlock(pos))
		return 0;
	// With the camera in hand the viewfinder is the pointer: no object's hand over the band (seen in a window)
	if (_script->isCameraEnabled() && pos.x >= kBandLeft && pos.x < kBandLeft + kBandWidth && pos.y >= kBandTop &&
		pos.y < kBandTop + kBandHeight)
		return 0;
	// A live .HOT entry (HotSpotObj_Cursor 0x491b20, read from the exe only): the stiletto and fist cursors of some
	// kinds, else the tool cursor (Screen_SetToolCursor, group 132) with the gun in hand. With none of these the
	// exe shows no cursor of its own; what stays then is not read (B-031), so the lookup below goes on
	if (uint32 kind = hotKindAt(pos)) {
		const int32 value = _script->getHeldValue();
		const bool stiletto = value == 23;
		switch (kind) {
		case 433:
		case 461:
		case 470:
			if (stiletto)
				return 31931; // InventoryStiletto
			break;
		case 462:
			if (stiletto)
				return 58095; // ActionStiletto
			break;
		case 441:
		case 451:
		case 463:
			return 17492; // ActionFist
		default:
			break;
		}
		if (_script->isGunHeld())
			return Cursors::kToolCursorId;
	}
	// A playing picture video hits before layouts with +0x178, navs, explorables and character views (the
	// HitTestObject order 0x519e40, sound-object step 0x45a0a0) and shows the default crosshair
	// B-290: the exe keeps the last refresh's cursor inside the rect instead of re-choosing it (not ported)
	if (_movieCursor && movieRect().contains(pos))
		return 0;
	ViewHit viewHit = hitView(pos);
	switch (viewHit.kind) {
	case ViewHit::kExplorable:
		// With an item in hand, an object that takes it keeps the item's
		// cursor (0x46e050), any other shows its own
		if (held && _script->isTestedOnClick(viewHit.id, _script->getHeldVariable()))
			return held;
		return viewHit.cursor;
	case ViewHit::kCharView:
		// CharView_ShowCursor (0x418520): the item's cursor is kept when the
		// character has a reaction slot for it, or a Roll Into or Mouse Click
		// trigger takes it
		if (held && (_script->hasHeldReactionSlot(viewHit.id) ||
					 _script->isTestedOnClick(viewHit.id, _script->getHeldVariable(), kEventRollInto) ||
					 _script->isTestedOnClick(viewHit.id, _script->getHeldVariable())))
			return held;
		// The view's own cursor (lips) only with an enabled topic or idea; else it sets none and the object
		// under it shows its cursor (the far Skinner shows the nav's hand, seen in the original)
		if (_script->hasTopicOrIdea(viewHit.id))
			return viewHit.cursor;
		// CharView_ShowCursor's CharTeleport lookup (0x407cd0): the cursor of the character view the click would lead
		// to, when it has a topic (the far Smolnikoff shows the lips, the far Skinner nothing)
		if (uint32 target = _script->getTeleportTarget(viewHit.id)) {
			VCCharView cv;
			if (_data->getCharView(target, cv))
				return cv.cursor;
		}
		return hitView(pos, true).cursor;
	case ViewHit::kNav:
		return viewHit.cursor; // 0x446aa0: the hotspot's cursor
	default:
		// The held item's cursor is only set by an object that takes the item (Obj_TakesHeldItem); empty
		// parts of the view show the default cursor
		return 0;
	}
}

void XFilesEngine::handleRelease(const Common::Point &pos) {
	if (!_dragIdea)
		return;
	uint32 icon = _dragIdea;
	_dragIdea = 0;
	if (!_dragMoved) {
		// A click: the box's character
		debugC(1, kDebugScript, "-- idea:%u", icon);
		_script->idea(icon);
		return;
	}
	// Dropped: the character view under the mouse takes it (a drop elsewhere does nothing)
	ViewHit viewHit = hitView(pos);
	if (viewHit.kind == ViewHit::kCharView) {
		debugC(1, kDebugScript, "-- idea:%u on character view #%u", icon, viewHit.id);
		_script->idea(icon, viewHit.id);
	}
}

void XFilesEngine::handleDoubleClick(const Interface::Hit &hit) {
	debugC(1, kDebugScript, "-- dclick:%u", hit.id);
	if (hit.kind == Interface::kHitInventory) {
		// InvIcon_OnDoubleClick (0x4a7ce0) also closes the box and the kit
		if (_dialogBox.isOpen()) {
			_dialogBox.close();
			_script->closeBox();
		}
		_interface->closeKit();
	}
	_script->doubleClick(hit.id);
}

void XFilesEngine::handleRightClick(const Common::Point &pos) {
	// A right button press on an inventory icon fires its Mouse Double-Click triggers, as the left double-click does
	// (the close-up of the book, the photos; the run logs show trigger event=1 right after msg=rdown)
	Interface::Hit hit = _interface->hitTest(pos);
	if (hit.kind == Interface::kHitInventory)
		handleDoubleClick(hit);
}

// -- Dialog box ---------------------------------------------------------------

void XFilesEngine::openDialogBox(const VCCharView &charView, bool openScript) {
	// DisplayDBox / XFiles.exe 0x481660: only for a character with topics or an enabled idea (0x436080)
	if (!_script->hasTopicOrIdea(charView.id))
		return;
	_headCredits.hide(); // DBox_OnActivate 0x4767a0
	_dialogBox.open(charView.id, Common::Rect(charView.rect.left, charView.rect.top, charView.rect.right, charView.rect.bottom));
	_dialogBoxView = _script->getView();
	debugC(1, kDebugScript, "-- dialog box of #%u", charView.id);
	if (openScript)
		_script->openBox(charView.id);
	updateDialogBox();
}

void XFilesEngine::updateDialogBox() {
	if (!_dialogBox.isOpen())
		return;
	VCCharView charView;
	if (_script->getView() != _dialogBoxView || !_script->isEnabled(_dialogBox.getCharView()) ||
			!_data->getCharView(_dialogBox.getCharView(), charView)) {
		_dialogBox.close();
		_script->closeBox();
		return;
	}
	// Enabled topics with a label: Talk lists those not asked yet, History
	// those asked (seen in a reference run of XFiles.exe)
	Common::Array<uint32> ids;
	_data->getMembers(charView.conversations, ids);
	Common::Array<DialogBox::Topic> talk, history;
	for (uint i = 0; i < ids.size(); i++) {
		VCConversation c;
		Common::Array<uint32> labels;
		VCAsset label;
		DialogBox::Topic topic;
		topic.id = ids[i];
		// The label is an asset without media naming a string of XFilesT.dll
		// (RT_STRING = 6); its authoring name is an older wording
		if (!_script->isEnabled(ids[i]) || !_data->getConversation(ids[i], c) || !c.label ||
				!_data->getMembers(c.label, labels) || labels.empty() || !_data->getAsset(labels[0], label) ||
				!_data->getName(label.name, topic.label))
			continue;
		uint32 type, string;
		if (_texts && _data->getResourceRef(labels[0], type, string) && type == 6) {
			Common::String text = _texts->loadString(string);
			if (!text.empty())
				topic.label = Common::U32String(text, Common::kWindows1252);
		}
		if (_script->getAskCount(ids[i]))
			history.push_back(topic);
		else
			talk.push_back(topic);
	}
	_dialogBox.setTopics(talk, history);
}

void XFilesEngine::askTopic(uint32 conversation) {
	debugC(1, kDebugScript, "-- ask:%u", conversation);
	_script->ask(conversation);
}

} // End of namespace XFiles
