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

#include "xfiles/debug/vcdump.h"
#include "xfiles/script/script.h"
#include "xfiles/script/vcdata.h"

namespace XFiles {

static const char *const kEventNames[] = {
	"Mouse Click", "Mouse Double-Click", "Mouse Move", "Key Press", "Roll Into", "Roll Out Of", "Drop",
	"Timer Expiration", "Object Activation", "Object Deactivation", "VC Window Activate", "VC Window Deactivate"
};

static Common::String cursorName(const VCData &data, uint32 id) {
	VCCursor cursor;
	return data.getCursor(id, cursor) ? nameOrNone(data, cursor.name) : Common::String("None");
}

Common::String dumpClassTable(const NeoFile &db) {
	Common::String out = Common::String::format("version %u  classes %u  next id %u  class root 0x%x  freelist 0x%x\n",
		db._version, db._classCount, db._nextId, db._classRoot, db._freeListRoot);
	const Common::Array<NeoClass> &classes = db.getClasses();
	for (uint i = 0; i < classes.size(); i++) {
		const NeoClass &c = classes[i];
		Common::Array<NeoEntry> entries;
		db.walk(c.root, entries);
		out += Common::String::format("  class %3u  objects %6u  root 0x%06x  key %s", c.id, c.count, c.root,
			pythonRepr(Common::U32String(c.key, Common::kISO8859_1)).c_str());
		if (entries.size() != c.count)
			out += Common::String::format("  (walked %u)", entries.size());
		out += '\n';
	}
	return out;
}

Common::String dumpActions(const VCData &data, uint limit) {
	Common::Array<NeoEntry> actions;
	data.getHdb().getObjects(kClassAction, actions);
	Common::String out;
	for (uint i = 0; i < actions.size() && i < limit; i++) {
		Common::String hex = Common::String::format("0x%x", actions[i].key);
		while (hex.size() < 7)
			hex = " " + hex;
		out += hex + "  " + describeAction(data, actions[i].key) + "\n";
	}
	return out;
}

static void dumpTriggers(const VCData &data, uint32 triggerList, const char *indent, Common::String &out) {
	Common::Array<uint32> triggers;
	data.getMembers(triggerList, triggers);
	for (uint i = 0; i < triggers.size(); i++) {
		VCTrigger t;
		if (!data.getTrigger(triggers[i], t))
			continue;
		out += Common::String(indent) + "on " + tableName(kEventNames, t.event) + ":\n";
		for (uint j = 0; j < t.actions.size(); j++)
			out += Common::String(indent) + "    " + describeAction(data, t.actions[j], t.event) + "\n";
	}
}

Common::String dumpSceneTree(const VCData &data, const Common::String &nodeFilter, bool withTriggers) {
	Common::String out;
	VCTitle title;
	if (!data.getTitle(title))
		return out;
	out += Common::String::format("Title #%u \"", title.id) + nameOrNone(data, title.name) + "\"\n";
	if (withTriggers)
		dumpTriggers(data, title.triggers, "  ", out);

	Common::String filter = nodeFilter;
	filter.toLowercase();
	for (uint n = 0; n < title.nodes.size(); n++) {
		VCNode node;
		data.getNode(title.nodes[n], node);
		Common::String nodeName = nameOrNone(data, node.name);
		Common::String lower = nodeName;
		lower.toLowercase();
		if (!filter.empty() && !lower.contains(filter))
			continue;
		out += Common::String::format("  Node #%u \"", node.id) + nodeName + "\"\n";
		if (withTriggers)
			dumpTriggers(data, node.triggers, "    ", out);

		for (uint l = 0; l < node.locations.size(); l++) {
			VCLocation loc;
			data.getLocation(node.locations[l], loc);
			out += Common::String::format("    Location #%u \"", loc.id) + nameOrNone(data, loc.name) + "\"\n";
			if (withTriggers)
				dumpTriggers(data, loc.triggers, "      ", out);

			for (uint p = 0; p < loc.viewPoints.size(); p++) {
				VCViewPoint vp;
				data.getViewPoint(loc.viewPoints[p], vp);
				out += Common::String::format("      ViewPoint #%u \"", vp.id) + nameOrNone(data, vp.name) +
					Common::String::format("\" at (%d, %d)\n", vp.x, vp.y);
				if (withTriggers)
					dumpTriggers(data, vp.triggers, "        ", out);

				for (uint v = 0; v < vp.views.size(); v++) {
					VCView view;
					data.getView(vp.views[v], view);
					Common::String viewName = view.facing < ARRAYSIZE(kDirections) ?
						Common::String(kDirections[view.facing]) : nameOrNone(data, view.facing);
					out += Common::String::format("        View #%u ", view.id) + viewName;

					Common::Array<uint32> navs;
					data.getMembers(view.navs, navs);
					for (uint i = 0; i < navs.size(); i++) {
						VCNav nav;
						VCHotSpot hotSpot;
						data.getNav(navs[i], nav);
						data.getHotSpot(nav.hotSpot, hotSpot);
						out += (i ? ", " : "  [") + cursorName(data, hotSpot.cursor) + Common::String::format(" -> #%u", nav.target);
					}
					out += navs.empty() ? "\n" : "]\n";
					if (withTriggers)
						dumpTriggers(data, view.triggers, "          ", out);
				}
			}
		}
	}
	return out;
}

/** JSON {"id": value, ...}, ten pairs to a line. */
static Common::String jsonMap(const Common::Array<uint32> &ids, const Common::Array<int32> &values) {
	Common::String out = "{";
	for (uint i = 0; i < ids.size(); i++)
		out += Common::String::format("%s\"%u\": %d", i ? (i % 10 ? ", " : ",\n  ") : "\n  ", ids[i], values[i]);
	return out + (ids.empty() ? "}" : "\n }");
}

/** JSON `,\n "name": [id, ...]`. */
static Common::String jsonList(const char *name, const Common::Array<uint32> &ids) {
	Common::String out = Common::String::format(",\n \"%s\": [", name);
	for (uint i = 0; i < ids.size(); i++)
		out += Common::String::format("%s%u", i ? ", " : "", ids[i]);
	return out + "]";
}

/** JSON `  "name": [v, v, ...]` of a game_state list. */
static Common::String jsonStateList(const char *name, const Common::Array<uint32> &list) {
	Common::String out = Common::String::format("  \"%s\": [", name);
	for (uint i = 0; i < list.size(); i++)
		out += Common::String::format("%s%u", i ? ", " : "", list[i]);
	return out + "]";
}

Common::String dumpState(const VCData &data, const Script &script, const VCSave *snapshot) {
	Common::Array<NeoEntry> entries;
	Common::Array<uint32> ids;
	Common::Array<int32> values;
	data.getGam().getObjects(kClassVariable, entries);
	for (uint i = 0; i < entries.size(); i++) {
		ids.push_back(entries[i].key);
		values.push_back(script.getVariable(entries[i].key));
	}
	Common::String out = "{\n \"format\": \"xfiles-engine-state 1\",\n \"variables\": " + jsonMap(ids, values);

	// VCString values the game changed, as hex bytes: {"id": "hex", ...}
	Script::SavedValues saved;
	script.getSaved(saved);
	out += ",\n \"strings\": {";
	for (uint i = 0; i < saved.stringIds.size(); i++) {
		out += Common::String::format("%s\"%u\": \"", i ? ", " : "", saved.stringIds[i]);
		for (uint j = 0; j < saved.strings[i].size(); j++)
			out += Common::String::format("%02x", (byte)saved.strings[i][j]);
		out += "\"";
	}
	out += "}";

	// By VCEnabled ID, as in a save's delta (the Script keeps them by target)
	entries.clear();
	ids.clear();
	values.clear();
	data.getGam().getObjects(kClassEnabled, entries);
	for (uint i = 0; i < entries.size(); i++) {
		uint32 target;
		bool enabled;
		if (data.getEnabled(entries[i].key, target, enabled)) {
			ids.push_back(entries[i].key);
			values.push_back(script.isEnabled(target));
		}
	}
	out += ",\n \"enabled\": " + jsonMap(ids, values);
	out += jsonList("emails", script.getEmails());
	out += jsonList("emails_read", script.getEmailsRead());
	out += jsonList("emails_pending", script.getEmailsPending());
	out += jsonList("pda_notes", script.getPDANotes());
	out += jsonList("inventory", script.getInventory());
	VCSave own;
	if (!snapshot) {
		script.snapshot(own);
		snapshot = &own;
	}
	out += jsonList("conversation_history", snapshot->history);
	// Running timers: number -> [ms, periodic]
	out += ",\n \"timers\": {";
	const Common::HashMap<uint, Script::Timer> &timers = script.getTimers();
	Common::Array<uint> numbers;
	for (Common::HashMap<uint, Script::Timer>::const_iterator it = timers.begin(); it != timers.end(); ++it)
		numbers.push_back(it->_key);
	Common::sort(numbers.begin(), numbers.end());
	for (uint i = 0; i < numbers.size(); i++)
		out += Common::String::format("%s\"%u\": [%u, %d]", i ? ", " : "", numbers[i], timers.getVal(numbers[i]).ms, timers.getVal(numbers[i]).periodic ? 1 : 0);
	out += "}";
	// The save's game state with the keys of save_delta.py (docs/formats/saves.md)
	const VCSave &s = *snapshot;
	out += ",\n \"game_state\": {\n";
	out += Common::String::format("  \"position\": [%u, %u, %u, %u],\n", script.getNode(), script.getLocation(), script.getViewPoint(),
		script.getView());
	out += Common::String::format("  \"disc\": %d, \"photos_on_camera\": %d, \"ai_actions\": %u, \"ai_trigger\": %u, \"ai_glow\": %d,\n",
		s.disc, s.photos, s.hintList, s.hintTrigger, s.hintGlow);
	out += Common::String::format("  \"credits\": %d, \"flashlight\": [%u, %u, %u], \"lbox_tint\": %u, \"lbox_color\": [%u, %u, %u],\n", s.credits,
		s.flashlight[0], s.flashlight[1], s.flashlight[2], s.tint, s.tintColor[0], s.tintColor[1], s.tintColor[2]);
	out += Common::String::format("  \"view_clip\": %u, \"view_clip_flag\": %u, \"view_clip_time\": %d, \"dbox_charview\": %u, \"layout\": %u, \"position_mode\": %d,\n",
		s.clip, s.clipFlag, s.clipTime, s.dialogBox, s.layout, s.positionMode);
	out += jsonStateList("sounds", s.sounds) + ",\n";
	out += jsonStateList("inventory", s.inventory) + ",\n";
	out += jsonStateList("evidence", s.evidence) + ",\n";
	out += jsonStateList("evidence_kit", s.kit) + ",\n";
	out += jsonStateList("timers", s.timers) + "\n }\n}\n";
	return out;
}

} // End of namespace XFiles
