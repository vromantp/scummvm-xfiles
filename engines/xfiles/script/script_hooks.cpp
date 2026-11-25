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

/** PDAEscape's action lists (XFiles.exe 0x47d090): escape on (argument 1) or off. */
static const uint32 kPDAEscapeOnActions = 75533;
static const uint32 kPDAEscapeOffActions = 75530;

/** PageTurnSound runs trigger 92463's list (XFiles.exe 0x4b3930): one of six random page sounds. */
static const uint32 kPageTurnSoundActions = 92468;

/** Willmore's journal (XFiles.exe 0x4bfb20): the page list's item and the two page text items. */
static const uint32 kJournalSource = 49422;
static const uint32 kJournalLeftItem = 46370;
static const uint32 kJournalRightItem = 46371;

/** Scully's Bible (XFiles.exe 0x4f8970): the page list's item, the two page items and the two edge buttons. */
static const uint32 kBibleSource = 80907;
static const uint32 kBibleLeftItem = 80868;
static const uint32 kBibleRightItem = 80869;
static const uint32 kBibleBackItem = 80870;
static const uint32 kBibleForwardItem = 80871;

/** DrawGunRauch's view (XFiles.exe 0x4f97d0): the Rauch porch shootout. */
static const uint32 kRauchShootoutView = 39746;

/** PrepBombTime's countdown video length in ms (XFiles.exe 0x50c2d0). */
static const uint32 kBombTimerLength = 180000;

void Script::addHooks() {
	addCoreHooks();
	_phone.addHooks(_hookTable);
	_computer.addHooks(_hookTable);
	_pda.addHooks(_hookTable);
	_prefs.addHooks(_hookTable);
}

void Script::addCoreHooks() {
	addGameHooks();
	addNodeHooks();
	addToolHooks();
	addInterfaceHooks();
	addStateHooks();
}

struct Script::GameHooks {
	static bool hookSwapDisc(Script &s, const HookCall &c) {
		if (!c.has(4))
			return false;
		// The last call in an event wins (0x4cfc70 replaces the pending travel)
		for (int i = 0; i < 4; i++)
			s._travel.to[i] = c.args[i];
		s._travel.pending = true;
		return true;
	}

	static bool hookNukeAllIcons(Script &s, const HookCall &) {
		// 0x49f3c0: deactivates the icons of kinds 14, 12, 15 (and others the engine does not model); the
		// emotion and action ones lose their enable flags
		s.spendIcons(kClassEmotionIcon);
		s.spendIcons(kClassActionIcon);
		// Kind 24 (the held tool) is in 0x44d850's kinds too; its deactivation (0x437c20) sets iCurrInventory to
		// Inventory_None, see docs/subsystems/inventory.md
		s.putBack();
		return true;
	}

	static bool hookInitSmShootout(Script &s, const HookCall &c) {
		if (!c.isVariable(0) || !c.isVariable(1) || !c.isVariable(2))
			return false;
		for (int i = 0; i < 3; i++)
			s._shoot.vars[i] = c.action.args[i].value;
		s.shootoutReset(); // its three rolls are the reset's (0x4f9630)
		return true;
	}

	static bool hookGetPlayFBIShot(Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		s.setVariable(c.action.args[0].value, s._once->fbiShot); // 0x4f9ef0
		s._once->fbiShot = false;
		return true;
	}

	static bool hookGetPlayBones(Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		s.setVariable(c.action.args[0].value, s._once->playBones); // 0x4f9f20
		s._once->playBones = false;
		return true;
	}

	static bool hookGetPlayCancer(Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		s.setVariable(c.action.args[0].value, s._once->playCancer); // 0x4f9f70
		s._once->playCancer = false;
		return true;
	}

	static bool hookPrepBombTime(Script &s, const HookCall &c) {
		if (!c.has(2))
			return false;
		// 0x50c2d0: a running timer with that id; the asset plays from 180000 minus what is left to 180000
		uint32 left;
		if (!s._hooks || !s._hooks->getTimerLeft(c.args[0], left))
			return true;
		uint32 start = left < kBombTimerLength ? kBombTimerLength - left : 0;
		s.log(Common::String::format("      PrepBombTime: timer %d has %u ms left, asset #%d plays from %u ms", c.args[0], left, c.args[1],
			start));
		s._hooks->setAssetRange(c.args[1], start, kBombTimerLength);
		return true;
	}

	static bool hookDrawGunRauch(Script &s, const HookCall &c) {
		if (!c.isVariable(0) || !c.isVariable(1))
			return false;
		// 0x4f97b0: with the NSA outside, the view state becomes 'b' and the exe's own Set View goes to the shootout
		if (s.getVariable(c.action.args[0].value) == 1) {
			s.setVariable(c.action.args[1].value, 'b');
			s.setViewByExe(kRauchShootoutView);
		}
		return true;
	}

	static bool hookEndGame(Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		// 0x4cc870 closes the dialog box; the question and the restart are the engine's (answerEndGame)
		s.closeBox();
		s._endGameVar = c.action.args[0].value;
		s._endGameValue = s.getVariable(s._endGameVar);
		s._endGamePending = true;
		return true;
	}
};

void Script::addGameHooks() {
	HookTable<Script> &t = _hookTable;
	t.add("SwapDisc", GameHooks::hookSwapDisc);
	t.add("NukeAllIcons", GameHooks::hookNukeAllIcons);
	t.add("EnableKitIcon", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.enableKitIcon(c.args[0]);
		return true;
	});
	t.add("RandGen", [](Script &s, const HookCall &c) {
		if (!c.has(2) || !c.isVariable(0))
			return false;
		s.randGen(c.action.args[0].value, c.args[1]);
		return true;
	});
	t.add("InitSmShootout", GameHooks::hookInitSmShootout);
	t.add("DoSmolShootout", [](Script &s, const HookCall &c) {
		s.doSmolShootout(c);
		return true;
	});
	t.add("SmIsWillShot1", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.smIsWillShot(c.args[0]);
		return true;
	});
	t.add("SmGoonShoots", [](Script &s, const HookCall &c) {
		if (!c.has(3) || !c.isVariable(0) || !c.isVariable(1))
			return false;
		s.smGoonShoots(c.action.args[0].value, c.action.args[1].value, c.args[2]);
		return true;
	});
	t.add("SmolGoonShot1", [](Script &s, const HookCall &) {
		s.smolGoonShot();
		return true;
	});
	t.add("GetPlayFBIShot", GameHooks::hookGetPlayFBIShot);
	t.add("GetPlayBones", GameHooks::hookGetPlayBones);
	t.add("GetPlayCancer", GameHooks::hookGetPlayCancer);
	t.add("RandGenRange", [](Script &s, const HookCall &c) {
		if (!c.has(3) || !c.isVariable(0))
			return false;
		s.randGenRange(c.action.args[0].value, c.args[1], c.args[2]);
		return true;
	});
	t.add("TakePhoto", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.takePhoto(c.args[0]);
		return true;
	});
	t.add("PrepBombTime", GameHooks::hookPrepBombTime);
	t.add("DrawGunRauch", GameHooks::hookDrawGunRauch);
	t.add("EndGame", GameHooks::hookEndGame);
	t.add("IsIItemHeld", [](Script &s, const HookCall &c) {
		if (!c.has(2) || !c.isVariable(1))
			return false;
		s.isIItemHeld(c.action.args[1].value, c.args[0]);
		return true;
	});
}

void Script::addNodeHooks() {
	HookTable<Script> &t = _hookTable;
	t.add("RegCurrNode", [](Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		s._currNodeVariable = c.action.args[0].value; // 0x4926a0
		return true;
	});
	t.add("RegNode2Vars", [](Script &s, const HookCall &c) {
		return s.regNodeVars(0, 3, c);
	});
	t.add("RegNode3Vars", [](Script &s, const HookCall &c) {
		return s.regNodeVars(1, 4, c);
	});
	t.add("RegNode4Vars", [](Script &s, const HookCall &c) {
		return s.regNodeVars(2, 2, c);
	});
	t.add("RegNode5Vars", [](Script &s, const HookCall &c) {
		return s.regNodeVars(3, 3, c);
	});
	t.add("RegNode6Vars", [](Script &s, const HookCall &c) {
		return s.regNodeVars(4, 2, c);
	});
	t.add("RegLink2Vars", [](Script &s, const HookCall &c) {
		return s.regNodeVars(5, 2, c);
	});
	t.add("RegLink3Vars", [](Script &s, const HookCall &c) {
		return s.regNodeVars(6, 2, c);
	});
	t.add("SetIsNode2Done", [](Script &s, const HookCall &) {
		return s.setNodeDone(0);
	});
	t.add("SetIsNode3Done", [](Script &s, const HookCall &) {
		return s.setNodeDone(1);
	});
	t.add("SetIsNode4Done", [](Script &s, const HookCall &) {
		return s.setNodeDone(2);
	});
	t.add("SetIsNode5Done", [](Script &s, const HookCall &) {
		return s.setNodeDone(3);
	});
	t.add("SetIsNode6Done", [](Script &s, const HookCall &) {
		return s.setNodeDone(4);
	});
	t.add("SetIsLink2Done", [](Script &s, const HookCall &) {
		return s.setNodeDone(5);
	});
	t.add("SetIsLink3Done", [](Script &s, const HookCall &) {
		return s.setNodeDone(6);
	});
	t.add("WillmoreSleeps", [](Script &, const HookCall &) {
		return true; // 0x4df9f0 is empty
	});
	t.add("PreloadEmIcon", [](Script &, const HookCall &) {
		return true; // 0x49c1f0 only builds the icon's object, hidden: nothing to draw
	});
	t.add("RegShanksNode2", [](Script &s, const HookCall &c) {
		return s.regShanksNode(0, 20, c);
	});
	t.add("RegShanksNode3", [](Script &s, const HookCall &c) {
		return s.regShanksNode(1, 18, c);
	});
	t.add("RegShanksNode6", [](Script &s, const HookCall &c) {
		return s.regShanksNode(2, 5, c);
	});
	t.add("SetShanksVar", [](Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		s.setShanksVar(c.action.args[0].value);
		return true;
	});
}

struct Script::InterfaceHooks {
	static bool hookAlienBlast(Script &s, const HookCall &) {
		// 0x4b6220: drops the queued activations of nodes, locations, viewpoints and views (0x41a770), then flashes
		// the screen (B-199). Only a travel queued behind the deactivation that ran it exists (B-204)
		if (s._travel.enterDeactivating) {
			s._travel.dropped = true;
			s.log("      -- AlienBlast: queued travel dropped");
		}
		if (s._hooks)
			s._hooks->fadeToWhite();
		return true;
	}

	static bool hookIsoChamberStat(Script &s, const HookCall &c) {
		// 0x4fc970 -> 0x4b6120 (docs/data/addresses.tsv): the 11 arguments are variables, 4-11 are outputs
		if (c.action.args.size() < 11)
			return false;
		for (int i = 0; i < 11; i++) {
			if (!c.isVariable(i))
				return false;
		}
		const bool east = s.getVariable(c.action.args[0].value) != 0;
		const bool west = s.getVariable(c.action.args[1].value) != 0;
		int32 out[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // arguments 4-11
		if (s.getVariable(c.action.args[2].value) == 1) {
			if (east && west) {
				out[2] = 1;
				out[4] = 1;
			} else if (east) {
				out[1] = 1;
				out[6] = 1;
			} else if (west) {
				out[2] = 1;
				out[5] = 1;
			} else {
				out[1] = 1;
				out[7] = 1;
			}
		} else {
			out[0] = 1;
			out[3] = 1;
		}
		for (int i = 0; i < 8; i++) {
			uint32 var = c.action.args[3 + i].value;
			if (s.getVariable(var) != out[i])
				s.setVariable(var, out[i]);
		}
		return true;
	}

	static bool hookSetLanguage(Script &s, const HookCall &c) {
		// (text, var): 0x48b680 reads the text asset (0x45f690: a file asset's file first) and writes it
		// into the string variable only if it differs (0x41bcb0)
		if (!c.has(2))
			return false;
		Common::U32String text;
		if (!s.readTextFile(c.args[0], text) && !s.getTextAsset(c.args[0], text))
			return false;
		Common::String value = text.encode(Common::kWindows1252);
		uint32 variable = c.action.args[1].value;
		if (s.getString(variable) != value)
			s.setString(variable, value);
		return true;
	}

	static bool hookSetIItemText(Script &s, const HookCall &c) {
		// (item, text, colour, font, size, shadow, align): 0x47de90 -> 0x4be1e0, laid out at twice
		// the item's width (+0x194 is 0), as the PDA texts
		if (!c.has(7))
			return false;
		Common::U32String text;
		if (!s.readTextFile(c.args[1], text) && !s.getTextAsset(c.args[1], text))
			return false;
		uint32 color = 0x000000;
		if (c.args[2] == 'w')
			color = 0xffffff;
		else if (c.args[2] == 'x')
			color = 0x2e6b8c;
		Script::TextAlign align = Script::kAlignLeft;
		if (c.args[6] == 'c')
			align = Script::kAlignCenter;
		else if (c.args[6] == 'r')
			align = Script::kAlignRight;
		VCInterfaceItem item;
		int width = 0;
		if (s._data.getInterfaceItem(c.args[0], item))
			width = (item.rect.right - item.rect.left) * 2;
		s.setItemText(c.args[0], text, c.args[4], false, color, align, c.args[5] == 'y', width, c.args[3] == 'H');
		return true;
	}

	static bool hookSuicideNote(Script &s, const HookCall &c) {
		// (Event::1) 0x4f9fc0: one of three note texts (table 0x659a00) on the activated item. The face is
		// ZonkersHand 14 (Jrn.ttr), black, left, wrapped at the item's own width (+0x194 is 1), 22 px lines
		if (!c.has(1) || !c.args[0])
			return false;
		static const uint32 kNotes[3] = {62868, 62870, 62872};
		Script::Dice dice = s.newDice(0x4fa005);
		uint32 asset = kNotes[s.roll(dice, 0, 2)];
		Common::U32String text;
		if (!s.readTextFile(asset, text) && !s.getTextAsset(asset, text))
			return false;
		VCInterfaceItem item;
		int width = 0;
		if (s._data.getInterfaceItem(c.args[0], item))
			width = item.rect.right - item.rect.left;
		s.setItemText(c.args[0], text, 14, false, 0x000000, Script::kAlignLeft, false, width, Script::kFaceHand);
		return true;
	}

	static bool hookJournalInit(Script &s, const HookCall &c) {
		// 0x4bfb20: the enabled pages; the front if the value is not 0, else the last spread. Never writes the variable
		if (!c.has(1))
			return false;
		s._journalPages.clear();
		VCInterfaceItem item;
		Common::Array<uint32> members;
		if (s._data.getInterfaceItem(kJournalSource, item))
			s._data.getMembers(item.assets, members);
		for (uint i = 0; i < members.size(); i++) {
			if (s.isEnabled(members[i]))
				s._journalPages.push_back(members[i]);
		}
		uint count = s._journalPages.size();
		if (count == 0 || c.args[0] != 0)
			s._journalLeft = 0;
		else
			s._journalLeft = (count & 1) ? count - 1 : count - 2;
		s.showJournalPages();
		return true;
	}

	static bool hookJournalFlip(Script &s, const HookCall &c) {
		// 0x4bff40: nothing with two pages or fewer
		if (!c.has(3) || !c.isVariable(1) || !c.isVariable(2))
			return false;
		uint count = s._journalPages.size();
		if (count <= 2)
			return true;
		if (c.args[0] == 0) {
			if (s._journalLeft < count - 2)
				s._journalLeft += 2;
		} else if (s._journalLeft != 0) {
			s._journalLeft -= 2;
		}
		int32 first = s._journalLeft == 0 ? 1 : 0;
		int32 last = s._journalLeft < count - 2 ? 0 : 1;
		if (s.getVariable(c.variableArg(1)) != first)
			s.setVariable(c.variableArg(1), first);
		if (s.getVariable(c.variableArg(2)) != last)
			s.setVariable(c.variableArg(2), last);
		s.showJournalPages();
		return true;
	}

	static bool hookBibleInit(Script &s, const HookCall &) {
		// 0x4f8970: the enabled pages of item 80907; the first spread, the back button off, the forward button on
		s._biblePages.clear();
		VCInterfaceItem item;
		Common::Array<uint32> members;
		if (s._data.getInterfaceItem(kBibleSource, item))
			s._data.getMembers(item.assets, members);
		for (uint i = 0; i < members.size(); i++) {
			if (s.isEnabled(members[i]))
				s._biblePages.push_back(members[i]);
		}
		s._bibleLeft = 0;
		s.setItemEnabled(kBibleBackItem, false);
		s.setItemEnabled(kBibleForwardItem, true);
		// No count check on the right page in the exe; an empty list gives both pages an empty text
		s.showBiblePages(true);
		return true;
	}

	static bool hookBibleFlip(Script &s, const HookCall &c) {
		// 0x4f8d40: dir 1 goes back, any other value forward; the last spread is left index 8 (untested, not in any run)
		if (!c.has(1))
			return false;
		s.setItemEnabled(kBibleBackItem, true);
		s.setItemEnabled(kBibleForwardItem, true);
		if (c.args[0] == 1) {
			if (s._bibleLeft == 0)
				return true;
			s._bibleLeft -= 2;
			if (s._bibleLeft == 0)
				s.setItemEnabled(kBibleBackItem, false);
		} else {
			if (s._bibleLeft > 7)
				return true;
			s._bibleLeft += 2;
			if (s._bibleLeft > 7)
				s.setItemEnabled(kBibleForwardItem, false);
		}
		s.showBiblePages(false);
		return true;
	}

	static bool hookRegViewState(Script &s, const HookCall &c) {
		if (!c.isVariable(0))
			return false;
		s._variables[c.action.args[0].value] = 0; // 0x47e9d0 clears the variable it registers
		s._pos.stateVariable = c.action.args[0].value; // and keeps it as the view-state variable (0x6c1450)
		return true;
	}
};

void Script::addInterfaceHooks() {
	HookTable<Script> &t = _hookTable;
	t.add("AlienBlast", InterfaceHooks::hookAlienBlast);
	t.add("IsoChamberStat", InterfaceHooks::hookIsoChamberStat);
	t.add("StopEnvSounds", [](Script &s, const HookCall &c) {
		s.stopEnvSounds();
		return true;
	});
	t.add("RegPreferences", [](Script &s, const HookCall &c) {
		s.regPreferences(c.action);
		return true;
	});
	t.add("SetLanguage", InterfaceHooks::hookSetLanguage);
	t.add("SetIItemText", InterfaceHooks::hookSetIItemText);
	t.add("SuicideNote", InterfaceHooks::hookSuicideNote);
	t.add("JournalInit", InterfaceHooks::hookJournalInit);
	t.add("JournalFlip", InterfaceHooks::hookJournalFlip);
	t.add("JournalDestroy", [](Script &, const HookCall &) {
		return true; // 0x4bfb00 only frees the font
	});
	t.add("BibleInit", InterfaceHooks::hookBibleInit);
	t.add("BibleFlip", InterfaceHooks::hookBibleFlip);
	t.add("SetIItemFocus", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s._focus = c.args[0];
		return true;
	});
	t.add("RegViewState", InterfaceHooks::hookRegViewState);
	t.add("PDAEscape", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.runActionList(c.args[0] == 1 ? kPDAEscapeOnActions : kPDAEscapeOffActions, c.event, c.fields);
		return true;
	});
	t.add("PageTurnSound", [](Script &s, const HookCall &c) {
		s.runActionList(kPageTurnSoundActions, c.event, c.fields);
		return true;
	});
}

struct Script::StateHooks {
	// The hooks whose arguments a save keeps (GameState_Save 0x4d9450): the hint list and glow are handled by the AI icon,
	// the flashlight and tint by the tool overlay and the band
	static bool hookSetAIList(Script &s, const HookCall &c) {
		// SetAIList(trigger, list)
		if (c.has(2)) {
			s._ai.trigger = c.args[0];
			s._ai.list = c.args[1];
			s._ai.cursor = 0;
			s.recheckAIIcon();
		}
		return false;
	}

	static bool hookSetAIGlow(Script &s, const HookCall &c) {
		// AI_SetGlow (0x480670): 0 clears the flag and the level, 1 sets the flag with level 0, n > 1 flag and level n
		if (c.has(1)) {
			s._ai.glow = c.args[0] > 1 ? c.args[0] : 0;
			s._ai.glowOn = c.args[0] != 0;
		}
		return false;
	}

	static bool hookSetFLightProps(Script &s, const HookCall &c) {
		// 0x44bda0 turns the arguments into the three values the save keeps:
		// 2 -> 1 (else 0), 'O' -> 1 (else 0), 'B' -> 0, 'D' -> 2, else 1
		if (c.has(3)) {
			s._flashlight[0] = c.args[0] == 2;
			s._flashlight[1] = c.args[1] == 'O';
			s._flashlight[2] = 1;
			if (c.args[2] == 'B')
				s._flashlight[2] = 0;
			else if (c.args[2] == 'D')
				s._flashlight[2] = 2;
		}
		// FLight_SetProps (0x4d7d20) also re-selects the held item when it is the flashlight, so the held light
		// takes the new look. That belongs to the flashlight tool mode, which is not ported
		return true;
	}

	static bool hookCharTeleport(Script &s, const HookCall &c) {
		// Hook_CharTeleport 0x4fb570 -> 0x4c56a0: Event::0/1 are the click. The character view under it (the current
		// view's) gives the character; the target view must have an enabled character view of the same one, else
		// nothing happens. Only within the same node, the player is cut to the target (0x4c5cd0)
		if (!c.has(6))
			return false;
		VCView view, target;
		if (!s._data.getView(s._pos.view, view) || !s._data.getView(c.args[5], target))
			return true;
		const Common::Point click = s._travel.clickPoint;
		uint32 character = 0;
		Common::Array<uint32> ids;
		s._data.getMembers(view.characters, ids);
		for (uint i = 0; i < ids.size() && !character; i++) {
			VCCharView cv;
			if (s._data.getCharView(ids[i], cv) && s.isEnabled(ids[i]) && click.x >= cv.rect.left && click.x < cv.rect.right &&
				click.y >= cv.rect.top && click.y < cv.rect.bottom)
				character = cv.character;
		}
		if (!character)
			return true;
		bool found = false;
		ids.clear();
		s._data.getMembers(target.characters, ids);
		for (uint i = 0; i < ids.size() && !found; i++) {
			VCCharView cv;
			found = s._data.getCharView(ids[i], cv) && s.isEnabled(ids[i]) && cv.character == character;
		}
		if (found && s._pos.node == (uint32)c.args[2])
			s.enter(c.args[2], c.args[3], c.args[4], c.args[5]);
		return true;
	}

	static bool hookRegCookEmail(Script &s, const HookCall &c) {
		// 0x48e830 keeps the two handles. The only reader is the notepad's send-to-address code (0x4e6b90, called
		// by AddrSelPDA, PdaHooks::notesPreview): sent to address 45290 it adds 1 to the count and, on notepad
		// day 1, sets the flag
		if (!c.isVariable(0) || !c.isVariable(1))
			return false;
		s._cookEmailCount = c.variableArg(0);
		s._cookEmailFlag = c.variableArg(1);
		return true;
	}

	static bool hookStopAllTimers(Script &s, const HookCall &c) {
		// 0x49fde0 queues a Timer Stop (type 2, command 1) for every timer of the list, paused ones included;
		// they run right after the hook
		Common::Array<uint> ids;
		for (Common::HashMap<uint, Timer>::const_iterator it = s._timers.begin(); it != s._timers.end(); ++it)
			ids.push_back(it->_key);
		Common::sort(ids.begin(), ids.end());
		for (uint i = 0; i < ids.size(); i++) {
			s._timers.erase(ids[i]);
			s.log(Common::String::format("      timer %u stopped (StopAllTimers)", ids[i]));
		}
		return true;
	}

	static bool hookSetLBoxTint(Script &s, const HookCall &c) {
		if (c.has(3)) {
			s._tint = 1;
			s._tintColor[0] = c.args[2];
			s._tintColor[1] = c.args[1];
			s._tintColor[2] = c.args[0];
		}
		return true; // the band multiply, drawn by XFilesEngine::applyBandTint
	}

	static bool hookDisplayDBox(Script &s, const HookCall &c) {
		// 0x451d90 -> 0x481660: opens the box of a character view with topics or an enabled idea, replacing the
		// open one; 0 closes it (0x4bdc30). The engine hook opens the widget
		if (!c.has(1))
			return false;
		VCCharView charView;
		if (c.args[0] == 0)
			s.closeBox();
		else if (s._data.getCharView(c.args[0], charView) && s.hasTopicOrIdea(charView.id))
			s.openBox(charView.id);
		return false;
	}

	static bool hookDispEvIcons(Script &s, const HookCall &c) {
		// 0x4efb20: every group 0 icon is shown or hidden; the interface draws the row from the pairs
		if (c.has(1)) {
			s._evidenceRowShown = c.args[0] != 0;
			for (uint i = 0; i + 2 <= s._evidence.size(); i += 2)
				s._evidence[i + 1] = s._evidenceRowShown;
		}
		return false;
	}
};

void Script::addStateHooks() {
	HookTable<Script> &t = _hookTable;
	t.add("SetAIList", StateHooks::hookSetAIList);
	t.add("ClearAIList", [](Script &s, const HookCall &c) {
		s._ai.trigger = s._ai.list = 0;
		s._ai.cursor = 0;
		s.recheckAIIcon();
		return false;
	});
	t.add("SetAIGlow", StateHooks::hookSetAIGlow);
	t.add("SetFLightProps", StateHooks::hookSetFLightProps);
	t.add("FixJumpCut", [](Script &s, const HookCall &c) {
		// 0x48aa20 reads both variables and calls 0x48aa10, a bare ret 0xc: no effect in this build
		return true;
	});
	t.add("CharTeleport", StateHooks::hookCharTeleport);
	t.add("RegDayNight", [](Script &s, const HookCall &c) {
		// 0x49b5b0 is a bare ret 0xc: the handle is not kept; the scripts read iDayNight themselves
		return true;
	});
	t.add("RegUberVars", [](Script &s, const HookCall &c) {
		// 0x48ba50; the Script adds to iUberParanoia (0x4bf500). The engine's hook keeps all three for the subliminals
		s.registerUberParanoia(c.variableArg(1));
		return true;
	});
	t.add("RegCookEmail", StateHooks::hookRegCookEmail);
	t.add("StopAllTimers", StateHooks::hookStopAllTimers);
	t.add("SetLBoxTint", StateHooks::hookSetLBoxTint);
	t.add("SetIFLAsView", [](Script &s, const HookCall &c) {
		// Layout_SetAsView 0x41b300: the layout's items go to the view group (key 0x80800), for the rest of the game
		if (!c.has(1))
			return false;
		if (!s.isViewLayout(c.args[0]))
			s._viewLayouts.push_back(c.args[0]);
		return true;
	});
	t.add("ClearLBoxTint", [](Script &s, const HookCall &c) {
		s._tint = 0;
		return true;
	});
	t.add("DisplayDBox", StateHooks::hookDisplayDBox);
	t.add("DispEvIcons", StateHooks::hookDispEvIcons);

	// Hooks only logged on purpose: the menu's Save and Load (B-063)
	static const char *const kLoggedMenuHooks[] = {"SaveKeyPress", "SaveLoadComit", "SaveLoadInit", "SaveSlotSelect"};
	for (uint i = 0; i < ARRAYSIZE(kLoggedMenuHooks); i++)
		t.addLogged(kLoggedMenuHooks[i]);
}

void Script::logHook(const Common::String &name, HookStatus status) {
	static const char *const kStatus[] = {"unknown", "logged", "handled"};
	// Level 2 and not log(): vcdump's sim has no hooks, and its logs stay as they are (tools/hook_coverage.py)
	debugC(2, kDebugScript, "hook %s %s", name.c_str(), kStatus[status]);
}

void Script::traceHookAsset(uint32 id) {
	if (_traceHook && id && Common::find(_traceAssets.begin(), _traceAssets.end(), id) == _traceAssets.end())
		_traceAssets.push_back(id);
}

void Script::flushHookEffects() {
	_traceFlushing = true;
	Common::Array<uint32> changed;
	for (Common::HashMap<uint32, int32>::const_iterator it = _variables.begin(); it != _variables.end(); ++it) {
		if (it->_value != _traceBefore.getValOrDefault(it->_key, 0) &&
			Common::find(_traceScript.begin(), _traceScript.end(), it->_key) == _traceScript.end())
			changed.push_back(it->_key);
	}
	Common::sort(changed.begin(), changed.end());
	for (uint i = 0; i < changed.size(); i++)
		log(Common::String::format("      var.set #%u = %d", changed[i], _variables[changed[i]]));
	Common::sort(_traceAssets.begin() + _traceAssetsLogged, _traceAssets.end());
	for (uint i = _traceAssetsLogged; i < _traceAssets.size(); i++)
		log(Common::String::format("      hook asset #%u", _traceAssets[i]));
	_traceAssetsLogged = _traceAssets.size();
	_traceBefore = _variables;
	_traceScript.clear();
	_traceFlushing = false;
}

bool Script::isHolding() const {
	return _tool.variable && getVariable(_tool.variable) != _tool.none;
}

void Script::showJournalPages() {
	// ZonkersHand 14 (Jrn.ttr), wrapped at the item's own width, 22 px lines
	for (uint k = 0; k < 2; k++) {
		uint32 item = k == 0 ? kJournalLeftItem : kJournalRightItem;
		uint index = _journalLeft + k;
		Common::U32String text;
		if (index < _journalPages.size() && !readTextFile(_journalPages[index], text))
			getTextAsset(_journalPages[index], text);
		VCInterfaceItem data;
		int width = 0;
		if (_data.getInterfaceItem(item, data))
			width = data.rect.right - data.rect.left;
		setItemText(item, text, 14, false, 0x000000, kAlignLeft, false, width, kFaceHand);
	}
}

void Script::showBiblePages(bool init) {
	// GetFont(3, 12) in the exe: the sans face stands in. Outside the init, a page the exe does not set keeps its text
	uint count = _biblePages.size();
	for (uint k = 0; k < 2; k++) {
		uint index = _bibleLeft + k;
		bool set;
		if (init)
			set = true;
		else if (k == 0)
			set = _bibleLeft < count;
		else
			set = _bibleLeft < count && _bibleLeft != 10 && index < count;
		if (!set)
			continue;
		uint32 item = k == 0 ? kBibleLeftItem : kBibleRightItem;
		if (index < count)
			debugC(2, kDebugScript, "bible left=%u page %u item %u", _bibleLeft, _biblePages[index], item);
		Common::U32String text;
		if (index < count && !readTextFile(_biblePages[index], text))
			getTextAsset(_biblePages[index], text);
		VCInterfaceItem data;
		int width = 0;
		if (_data.getInterfaceItem(item, data))
			width = data.rect.right - data.rect.left;
		setItemText(item, text, 12, false, 0x000000, kAlignLeft, false, width, kFaceSans);
	}
}

void Script::clearHeldItem() {
	if (_tool.variable)
		_variables[_tool.variable] = _tool.none;
}

void Script::putBack() {
	// The held-item object exists only while something is in hand; its
	// deactivation (0x437c20) resets the variable
	if (!isHolding())
		return;
	debugC(1, kDebugScript, "-- #%u put back", _tool.cursorItem);
	int32 held = getVariable(_tool.variable);
	clearHeldItem();
	_tool.cursorItem = 0; // Inv_SetHeldCursor(0)
	if (_hooks)
		_hooks->heldItemChanged(0);
	// The binoculars' held object deactivates (0x51c160): the base first, then the view change back
	if (_tool.binocularsHeld) {
		_tool.binocularsHeld = false;
		binocularsView(false);
		_tool.binocularsMode = 0;
	}
	toolPause(held, _tool.none); // 0x5078f0(1) after the tool object's deactivation (after the view change back)
}

bool Script::hasInventoryIcon(uint32 id) const {
	bool has;
	if (_hooks && _hooks->queryInventoryIcon(id, has))
		return has;
	return _data.getClassOf(id) == kClassInventory && isEnabled(id);
}

void Script::selectInventory(uint32 id) {
	if (_tool.cursorItem == id)
		return;
	putBack();
	_tool.cursorItem = id; // Inv_SetHeldCursor, before the Mouse Click triggers
	if (_hooks)
		_hooks->heldItemChanged(id);
	debugC(1, kDebugScript, "-- click:%u (inventory)", id);
	click(id);
}

void Script::runActionList(uint32 actionList, int event, const EventFields &fields) {
	Common::Array<uint32> actions;
	_data.getMembers(actionList, actions);
	runActions(actions, event, fields);
}

void Script::randGen(uint32 variableId, int32 n) {
	// 0x49f6d0: a generator from 0 to n, rolled once
	Dice dice = newDice(0x49f6f9);
	int32 value = roll(dice, 0, n);
	_variables[variableId] = value;
	Common::U32String name;
	_data.getVariableName(variableId, name);
	log(Common::String::format("      %s = %d", name.encode().c_str(), value));
}

void Script::randGenRange(uint32 variableId, int32 lo, int32 hi) {
	// 0x49f740: Random_Seed(lo, hi) swaps the bounds when hi < lo
	Dice dice = newDice(0x49f79b);
	int32 value = roll(dice, MIN(lo, hi), MAX(lo, hi));
	setVariable(variableId, value);
	Common::U32String name;
	_data.getVariableName(variableId, name);
	log(Common::String::format("      %s = %d", name.encode().c_str(), value));
}

void Script::isIItemHeld(uint32 variableId, int32 item) {
	// 0x4b3850: 1 when the object the last mouse-down captured is the interface item
	_variables[variableId] = _held && _held == (uint32)item ? 1 : 0;
	Common::U32String name;
	_data.getVariableName(variableId, name);
	log(Common::String::format("      %s = %d", name.encode().c_str(), _variables[variableId]));
}

void Script::enableKitIcon(uint32 id) {
	// XFiles.exe 0x4effd0: enable the evidence icon and give it an object in
	// the kit, whose activation fires its Object Activation
	if (_data.getClassOf(id) != kClassEvidenceIcon)
		return;
	_enabled[id] = true;
	if (Common::find(_kit.begin(), _kit.end(), id) == _kit.end())
		_kit.push_back(id);
	fire(id, kEventObjectActivation);
}

} // End of namespace XFiles
