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
#include "common/textconsole.h"

#include "xfiles/detection.h"
#include "xfiles/script/script.h"

namespace XFiles {

void Script::shootoutReset() {
	// 0x48ccd0 (Game_Start) and InitSmShootout 0x4f9630: forget the shots, clear the variables, roll three permutations
	for (int i = 0; i < 3; i++) {
		_shoot.idx[i] = 0;
		_shoot.dead[i] = false;
		if (_shoot.vars[i])
			setVariable(_shoot.vars[i], 0);
	}
	_shoot.cur = 0;
	static const int perms[6][3] = {{1, 2, 3}, {1, 3, 2}, {2, 1, 3}, {2, 3, 1}, {3, 1, 2}, {3, 2, 1}};
	for (int i = 0; i < 3; i++) {
		// Nothing reads the permutations; without fixed seeds the dice would draw from (and shift) the shared sequence
		if (!_fixedSeeds)
			break;
		Dice dice = newDice(0x4f9658);
		const int *perm = perms[roll(dice, 1, 6) - 1];
		for (int k = 0; k < 3; k++)
			_shoot.perm[i][k] = perm[k];
	}
}

bool Script::findViewPath(uint32 view, uint32 (&path)[4]) const {
	// The exe finds the view's owners in the database; the current viewpoint is the likely one
	path[0] = view;
	VCViewPoint viewPoint;
	if (_data.getViewPoint(_pos.viewPoint, viewPoint) && Common::find(viewPoint.views.begin(), viewPoint.views.end(),
		view) != viewPoint.views.end()) {
		path[1] = _pos.node;
		path[2] = _pos.location;
		path[3] = _pos.viewPoint;
		return true;
	}
	Common::Array<NeoEntry> nodes;
	_data.getHdb().getObjects(kClassNode, nodes);
	for (uint n = 0; n < nodes.size(); n++) {
		VCNode node;
		if (!_data.getNode(nodes[n].key, node))
			continue;
		for (uint l = 0; l < node.locations.size(); l++) {
			VCLocation location;
			if (!_data.getLocation(node.locations[l], location))
				continue;
			for (uint v = 0; v < location.viewPoints.size(); v++) {
				if (_data.getViewPoint(location.viewPoints[v], viewPoint) &&
					Common::find(viewPoint.views.begin(), viewPoint.views.end(), view) != viewPoint.views.end()) {
					path[1] = node.id;
					path[2] = location.id;
					path[3] = viewPoint.id;
					return true;
				}
			}
		}
	}
	return false;
}

void Script::doSmolShootout(const HookCall &call) {
	// 0x4f9a40: the next goon to appear, then a Set View the exe makes (0x4b3bc0, no VCAction in the data)
	static const uint32 views[3][3] = {{62256, 62258, 62260}, {62262, 62264, 62266}, {62268, 62270, 62272}};
	if (_shoot.dead[0] && _shoot.dead[1] && _shoot.dead[2]) {
		_shoot.cur = 0;
	} else {
		Dice dice = newDice(0x4f9a59);
		int r = roll(dice, 1, 3);
		int goon = r - 1;
		if (_shoot.dead[goon])
			goon = (goon + 1) % 3;
		if (_shoot.dead[goon])
			goon = (goon + 1) % 3;
		_shoot.cur = goon + 1;
	}
	uint32 view = 5208;
	if (_shoot.cur) {
		int &idx = _shoot.idx[_shoot.cur - 1];
		view = views[_shoot.cur - 1][idx];
		if (idx < 2)
			idx++;
	}
	// The exe also drops the pending .HOT entries first (nothing to drop until B-175)
	VCAction a;
	a.id = 0;
	a.type = kActionSetView;
	if (!findViewPath(view, a.path)) {
		warning("Script: DoSmolShootout: no owner for view %u", view);
		return;
	}
	LocalVariables locals;
	execute(a, call.event, call.fields, locals);
}

void Script::smIsWillShot(uint32 timer) {
	// 0x4f9bf0
	if (!_shoot.cur)
		return;
	int n = _shoot.idx[_shoot.cur - 1];
	Dice dice = newDice(0x4f9c07);
	int r = roll(dice, 0, 100);
	int32 difficulty = getPreferenceValue(kPrefActionSceneDifficulty);
	bool easy = difficulty == 'E', hard = difficulty == 'H';
	int limit1 = easy ? 20 : (hard ? 60 : 40);
	int limit2 = easy ? 50 : (hard ? 80 : 65);
	bool shot = n >= 3 || (n == 1 && r < limit1) || (n == 2 && r < limit2);
	if (shot) {
		// No reference run takes this branch (docs/subsystems/smolnikoff-shootout.md): a Timer Stop, then asset 21168
		_timers.erase(timer);
		playAsset(21168, kModePlayOnce, nullptr); // the SoundObj play of 0x491de0: a plain play of the death clip
	}
}

void Script::smGoonShoots(uint32 deadVar, uint32 countVar, uint32 timer) {
	// 0x4f9820: the roll is always made; the count picks the threshold row (2 or 3), else no shot
	Dice dice = newDice(0x4f9853);
	int r = roll(dice, 0, 100);
	int32 difficulty = getPreferenceValue(kPrefActionSceneDifficulty);
	bool easy = difficulty == 'E', hard = difficulty == 'H';
	int32 count = getVariable(countVar);
	int limit = -1;
	if (count == 2)
		limit = easy ? 20 : (hard ? 60 : 40);
	else if (count == 3)
		limit = easy ? 50 : (hard ? 80 : 65);
	int32 dead = limit >= 0 && r < limit ? 1 : 0;
	if (!dead)
		_timers.erase(timer & 0xff); // Timer Stop, as smIsWillShot does
	if (getVariable(deadVar) != dead)
		setVariable(deadVar, dead);
}

void Script::smolGoonShot() {
	// 0x4f9d20: the goon on screen is dead; every variable is written, changed or not
	if (_shoot.cur >= 1 && _shoot.cur <= 3)
		_shoot.dead[_shoot.cur - 1] = true;
	for (int i = 0; i < 3; i++) {
		if (_shoot.vars[i])
			setVariable(_shoot.vars[i], _shoot.dead[i]);
	}
}

} // End of namespace XFiles
