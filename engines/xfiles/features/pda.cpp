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

#include "xfiles/features/pda.h"
#include "xfiles/script/script.h"

namespace XFiles {

// The PDA's e-mail: three ID lists (VCEmail, VCEmailRead, VCEmailPending)
// and the inbox rows built from them (XFiles.exe 0x484810 to 0x48c680).
// The texts of the inbox, the reader, the address book and the notepad
// (docs/subsystems/pda.md, "Text drawing").

/** The e-mail DisplayInBox drops from the list (0x48bfd0). */
static const uint32 kRemovedEmail = 63611;
/** Rows the inbox shows at once. */
static const uint kInboxRows = 8;
/** Action lists EmailInit('a') and EmailInit('b') run when the lists differ in size (0x4754e0). */
static const uint32 kEmailInitA = 86310;
static const uint32 kEmailInitB = 86316;
/** The reader item whose e-mails show other items than the inbox reader's. */
static const uint32 kEmailReadItem = 54950;
/** Courier New 12 (EmailInit('P'), the PDA) and 14 (the E-Mail layout) (0x475170). */
static const int kPDAFontSize = 12;
static const int kMailFontSize = 14;

/** The items InitEmailRead shows and hides for one e-mail (0x43df10). */
struct EmailReadItems {
	bool readerItem;  ///< For item 54950; otherwise for any other item
	uint32 email;
	uint32 show[4];
	uint32 hide;
	uint32 textItem;  ///< Where the text goes
};

static const EmailReadItems kEmailReadItems[] = {
	{ true, 61775, { 72713, 72710, 72715, 72722 }, 69527, 72758 },
	{ true, 61765, { 72713, 72710, 72723, 91939 }, 69527, 72758 },
	{ true, 64527, { 72713, 72710, 91038, 91007 }, 0, 72758 },
	{ false, 61775, { 72741, 72760, 72751, 91947 }, 71372, 72761 },
	{ false, 64527, { 72741, 72760, 72751, 91947 }, 0, 72761 },
	{ false, 61765, { 72741, 72760, 72743, 72759 }, 71372, 72761 }
};

/** The inbox slots' items (tables 0x6bd8d8, 0x6bd7f0, 0x6bd7e8). */
static uint32 fromItem(bool pda, uint slot) {
	return (pda ? 40539 : 38282) + slot;
}

static uint32 subjectItem(bool pda, uint slot) {
	return (pda ? 42749 : 42733) + slot;
}

static uint32 markerItem(bool pda, uint slot) {
	if (pda)
		return 40876 + 2 * slot;
	return slot == 0 ? 15293 : 38253 + 2 * (slot - 1);
}

/** The address book (0x4ad130, 0x4acec0). */
static const uint32 kAddressSource = 45307;
static const uint32 kAddressItem = 35320;
static const int kAddressFontSize = 14;
static const uint32 kAddressEnable[2] = { 42906, 42908 };

/** The notepad (0x4e6220; tables 0x6584b8 and 0x658520). */
static const uint kNoteDayCount = 23;
static const uint32 kNoteDayItems[kNoteDayCount] = {
	45344, 45345, 45346, 45348, 45347, 45349, 45350, 50378, 63395, 63389, 63393, 63397,
	63399, 63401, 63403, 63405, 63409, 63411, 63413, 63415, 63417, 63419, 63421
};
/** The first u32 of each day's entry of 0x658520: the note that sets the marker. */
static const uint32 kNoteKeys[kNoteDayCount] = {
	63249, 63273, 63280, 63292, 63290, 63300, 63312, 63324, 63332, 63336, 63338, 63340,
	63346, 63348, 63352, 63354, 63358, 63362, 63364, 63366, 63370, 63379, 5
};
static const uint32 kNoteTextItem = 51444;
static const uint32 kNoteLabelItem = 67486;
static const uint32 kNoteMarkerItem = 63423;
static const uint32 kNoteMarkerExtra[2] = { 63282, 63284 };
static const uint kNoteMarkerDay = 2;
static const int kNoteFontSize = 12;

/** The address book's rows and check items per mode (docs/plans/B-089.md). */
static const uint32 kAddrRowsT[5] = { 40423, 40424, 40425, 40426, 40427 };
static const uint32 kAddrChecksT[5] = { 40416, 40417, 40418, 40419, 40420 };
static const uint32 kAddrRowsC[5] = { 40453, 40454, 40455, 40456, 40457 };
static const uint32 kAddrChecksC[5] = { 40448, 40449, 40450, 40451, 40452 };
static const uint32 kAddrRowsP[9] = { 65031, 65030, 65029, 37215, 37216, 37217, 37218, 37219, 37220 };
static const uint32 kAddrRowsA[3] = { 40430, 40431, 40432 };
static const uint32 kAddrRowsB[3] = { 40433, 40434, 40435 };
static const uint32 kAddrRowsD[1] = { 40516 };
/** The item whose asset list is the address book, and the send button. */
static const uint32 kAddressBookItem = 45311;
static const uint32 kMailSendButton = 40502;

static bool listHas(const Common::Array<uint32> &list, uint32 id) {
	return Common::find(list.begin(), list.end(), id) != list.end();
}

static void listRemove(Common::Array<uint32> &list, uint32 id) {
	Common::Array<uint32>::iterator it;
	while ((it = Common::find(list.begin(), list.end(), id)) != list.end())
		list.erase(it);
}

PdaHooks::PdaHooks(Script &script, const VCData &data)
	: _script(script), _data(data), _inboxTop(0), _addrTop(0), _addrMode(0), _addrDest(nullptr), _pdaFontSize(kPDAFontSize), _pdaLayout(false), _scrollItem(0),
	  _scrollSize(kPDAFontSize), _scrollColor(0), _scrollBold(false), _scrollSans(false), _addressIndex(0), _noteDay(0), _noteDays(0) {
	for (uint i = 0; i < kNoteDayCount; i++)
		_noteOrder[i] = i;
}

struct PdaHooks::Handlers {
	static bool hookSendEmail(Script &s, const HookCall &c) {
		PdaHooks &p = s.pda();
		if (!c.has(1))
			return false;
		p._emailPending.push_back(c.args[0]);
		return true;
	}

	static bool hookEmailFlushMess(Script &s, const HookCall &c) {
		PdaHooks &p = s.pda();
		for (uint i = 0; i < p._emailPending.size(); i++) {
			if (!listHas(p._emails, p._emailPending[i]))
				p._emails.push_back(p._emailPending[i]);
		}
		p._emailPending.clear();
		return true;
	}

	static bool hookEmailDelete(Script &s, const HookCall &c) {
		PdaHooks &p = s.pda();
		if (!c.has(1))
			return false;
		listRemove(p._emails, c.args[0]);
		listRemove(p._emailsRead, c.args[0]);
		return true;
	}

	static bool hookEmailInit(Script &s, const HookCall &c) {
		PdaHooks &p = s.pda();
		if (!c.has(1))
			return false;
		if (c.args[0] == 'a' || c.args[0] == 'b') {
			if (p._emails.size() != p._emailsRead.size())
				s.runActionList(c.args[0] == 'a' ? kEmailInitA : kEmailInitB, c.event, c.fields);
		} else {
			p._inboxRows.clear();
			p._inboxTop = 0;
			p._pdaLayout = c.args[0] == 'P';
			p._pdaFontSize = p._pdaLayout ? kPDAFontSize : kMailFontSize;
			// 0x475170: the address book starts unselected, with no destination
			p._addrWork.clear();
			p._addrTo.clear();
			p._addrCc.clear();
			p._addrPda.clear();
			p._addrMode = 0;
			p._addrDest = nullptr;
			VCInterfaceItem book;
			Common::Array<uint32> members;
			if (s.data().getInterfaceItem(kAddressBookItem, book) && s.data().getMembers(book.assets, members)) {
				for (uint i = 0; i < members.size(); i++) {
					// 0x435590: only the enabled assets
					if (!s.isEnabled(members[i]))
						continue;
					AddrEntry e;
					e.id = members[i];
					e.selected = false;
					p._addrWork.push_back(e);
				}
			}
		}
		return true;
	}

	static bool hookGetCurrEmail(Script &s, const HookCall &c) {
		PdaHooks &p = s.pda();
		if (!c.has(2) || !c.isVariable(1))
			return false;
		uint i = p._inboxTop + c.args[0];
		s.setVariable(c.action.args[1].value, i < p._inboxRows.size() ? (int32)p._inboxRows[i].id : 0);
		return true;
	}

	static bool hookMarkEmailRead(Script &s, const HookCall &c) {
		PdaHooks &p = s.pda();
		if (!c.has(1))
			return false;
		uint i = p._inboxTop + c.args[0];
		if (i < p._inboxRows.size() && !p._inboxRows[i].read) {
			p._emailsRead.push_back(p._inboxRows[i].id);
			p._inboxRows[i].read = true;
		}
		return true;
	}
};

void PdaHooks::addHooks(HookTable<Script> &t) {
	t.add("ScrollTextList", [](Script &s, const HookCall &c) {
		if (!c.has(5))
			return false;
		s.pda().scrollTextList(c.args[0], c.args[1], c.args[2], c.args[3], c.args[4]);
		return true;
	});
	t.add("ScrollTextInit", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.pda().scrollTextList(c.args[0], 0, 'b', 'c', kPDAFontSize);
		return true;
	});
	t.add("SendEmail", Handlers::hookSendEmail);
	t.add("EmailFlushMess", Handlers::hookEmailFlushMess);
	t.add("EmailDelete", Handlers::hookEmailDelete);
	t.add("EmailInit", Handlers::hookEmailInit);
	t.add("DisplayAddrs", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.pda().displayAddrs((char)c.args[0]);
		return true;
	});
	t.add("AddrSelected", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.pda().addrSelected(c.args[0]);
		return true;
	});
	t.add("ScrollAddrs", [](Script &s, const HookCall &c) {
		if (!c.has(2))
			return false;
		s.pda().scrollAddrs((char)c.args[0], c.args[1]);
		return true;
	});
	t.add("CreateAddrList", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.pda().createAddrList((char)c.args[0]);
		return true;
	});
	t.add("AddrSelPDA", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.pda().addrSelPDA(c.args[0]);
		return true;
	});
	t.add("DisplayInBox", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.pda().displayInBox(c.args[0]);
		return true;
	});
	t.add("GetCurrEmail", Handlers::hookGetCurrEmail);
	t.add("MarkEmailRead", Handlers::hookMarkEmailRead);
	t.add("InitEmailRead", [](Script &s, const HookCall &c) {
		if (!c.has(2))
			return false;
		s.pda().initEmailRead(c.args[0], c.args[1]);
		return true;
	});
	t.add("MainScroll", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.pda().mainScroll(c.args[0]);
		return true;
	});
	t.add("ScrollHeaders", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.pda().scrollHeaders(c.args[0]);
		return true;
	});
	t.add("InitAddrList", [](Script &, const HookCall &) {
		return true; // 0x4dfa00 is empty
	});
	t.add("ScrollTextKill", [](Script &s, const HookCall &c) {
		PdaHooks &p = s.pda();
		s.clearItemText(p._scrollItem);
		p._scrollItem = 0;
		return true;
	});
	t.add("PDAInitAddr", [](Script &s, const HookCall &c) {
		s.pda().pdaInitAddr();
		return true;
	});
	t.add("PDAFlipAddr", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.pda().pdaFlipAddr(c.args[0]);
		return true;
	});
	t.add("EnablePDANote", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.pda().enablePDANote(c.args[0]);
		return true;
	});
	t.add("EnableAllNotes", [](Script &s, const HookCall &c) {
		s.pda().enableAllNotes();
		return true;
	});
	t.add("NotePadInit", [](Script &s, const HookCall &c) {
		s.pda().notePadInit();
		return true;
	});
	t.add("NoteFlipDay", [](Script &s, const HookCall &c) {
		if (!c.has(1))
			return false;
		s.pda().noteFlipDay(c.args[0]);
		return true;
	});
}

void PdaHooks::restore(const VCSave &save) {
	_emails = save.emails;
	_emailsRead = save.emailsRead;
	_emailPending = save.emailsPending;
	_notes = save.notes;
}

void PdaHooks::snapshot(VCSave &save) const {
	save.notes = _notes;
	save.emails = _emails;
	save.emailsRead = _emailsRead;
	save.emailsPending = _emailPending;
}

void PdaHooks::displayInBox(int32 value) {
	listRemove(_emails, kRemovedEmail);
	_inboxRows.clear();
	for (uint i = 0; i < _emails.size(); i++) {
		InboxRow row;
		row.id = _emails[i];
		row.read = listHas(_emailsRead, row.id);
		_inboxRows.push_back(row);
	}
	uint rows = _inboxRows.size();
	if (value == 0) {
		_inboxTop = rows > kInboxRows ? rows - kInboxRows : 0;
	} else if (_emails.size() > kInboxRows) {
		uint index = _emails.size();
		for (uint i = 0; i < _emails.size(); i++) {
			if ((int32)_emails[i] == value) {
				index = i;
				break;
			}
		}
		_inboxTop += index;
	}
	if (rows >= kInboxRows && _inboxTop > rows - kInboxRows)
		_inboxTop = rows - kInboxRows;
	showInboxTexts();
}

void PdaHooks::addEmail(uint32 id) {
	if (!listHas(_emails, id))
		_emails.push_back(id);
}

void PdaHooks::initEmailRead(int32 item, int32 email) {
	// 0x43df10: the whole file goes into a scroll text over the item, or over the text item
	// of the three special e-mails
	uint32 textItem = item;
	for (uint i = 0; i < ARRAYSIZE(kEmailReadItems); i++) {
		const EmailReadItems &e = kEmailReadItems[i];
		if ((int32)e.email != email || e.readerItem != (item == (int32)kEmailReadItem))
			continue;
		for (uint j = 0; j < 4; j++)
			_script.setItemEnabled(e.show[j], true);
		if (e.hide)
			_script.setItemEnabled(e.hide, false);
		textItem = e.textItem;
		break;
	}
	Common::U32String text;
	if (_script.readTextFile(email, text))
		setScrollText(textItem, text, _pdaFontSize);
}

const Common::U32StringArray &PdaHooks::getEmailLines(uint32 email) {
	if (!_emailLines.contains(email)) {
		// 0x48ca50: split on newlines: To, From, Subject, then the body
		Common::U32StringArray &lines = _emailLines[email];
		Common::U32String text;
		if (_script.readTextFile(email, text)) {
			Common::U32String line;
			for (uint i = 0; i <= text.size(); i++) {
				if (i < text.size() && text[i] != '\n') {
					line += text[i];
				} else {
					lines.push_back(line);
					line.clear();
				}
			}
		}
	}
	return _emailLines[email];
}

void PdaHooks::showInboxTexts() {
	// 0x48c680: rows top ... top + 7 fill the slots: the From and Subject lines, and the
	// marker if the e-mail was read. Slots past the last row are empty.
	for (uint k = 0; k < kInboxRows; k++) {
		uint32 from = fromItem(_pdaLayout, k), subject = subjectItem(_pdaLayout, k);
		uint row = _inboxTop + k;
		if (row < _inboxRows.size()) {
			const Common::U32StringArray &lines = getEmailLines(_inboxRows[row].id);
			_script.setPDAText(from, lines.size() > 1 ? lines[1] : Common::U32String(), _pdaFontSize, true);
			_script.setPDAText(subject, lines.size() > 2 ? lines[2] : Common::U32String(), _pdaFontSize, true);
			_script.setItemEnabled(markerItem(_pdaLayout, k), _inboxRows[row].read);
		} else {
			_script.clearItemText(from);
			_script.clearItemText(subject);
			_script.setItemEnabled(markerItem(_pdaLayout, k), false);
		}
	}
}

void PdaHooks::setScrollText(uint32 item, const Common::U32String &text, int size, uint32 color, bool bold, bool sans) {
	// 0x4be340: a scroll text replaces the earlier one; wrapped at the item's width
	if (_scrollItem && _scrollItem != item)
		_script.clearItemText(_scrollItem);
	_scrollItem = item;
	_scrollSize = size;
	_scrollColor = color;
	_scrollBold = bold;
	_scrollSans = sans;
	_script.setPDAText(item, text, size, false, color, bold, sans);
}

void PdaHooks::replaceScrollText(const Common::U32String &text) {
	if (_scrollItem)
		_script.setPDAText(_scrollItem, text, _scrollSize, false, _scrollColor, _scrollBold, _scrollSans);
}

void PdaHooks::scrollTextList(int32 item, int32 asset, int32 color, int32 font, int32 size) {
	// 0x481160 / 0x45e280 -> 0x4be340. The asset is a file asset (its file, as for an e-mail), a text asset
	// or a string of XFilesT.dll (0x45f690). 'H' is Arial made bold, anything else Courier New 400.
	// The colours: 'w' white, 'x' Color_TextBlue, 'b' and anything else black.
	uint32 rgb = color == 'w' ? 0xffffff : color == 'x' ? 0x2e6b8c : 0;
	bool arial = font == 'H';
	Common::U32String text;
	if (asset && !_script.readTextFile(asset, text))
		_script.getTextAsset(asset, text);
	// Arial 12 is 15 px per line (font.metrics, regular); the bold one is assumed to match
	setScrollText(item, text, size, rgb, arial, arial);
}

void PdaHooks::scrollHeaders(int32 n) {
	// 0x4ec910: only with more than 8 rows; the redraw is 0x48c680
	uint rows = _inboxRows.size();
	if (rows <= kInboxRows)
		return;
	switch (n) {
	case 0:
		if (_inboxTop == 0)
			return;
		_inboxTop--;
		break;
	case 1:
		if (_inboxTop >= rows - kInboxRows)
			return;
		_inboxTop++;
		break;
	case 10:
		if (_inboxTop == 0)
			return;
		_inboxTop = _inboxTop < kInboxRows ? 0 : _inboxTop - 7;
		break;
	case 11:
		if (_inboxTop >= rows - kInboxRows)
			return;
		_inboxTop = MIN<uint>(_inboxTop + 7, rows - kInboxRows);
		break;
	default:
		break;
	}
	showInboxTexts();
}

void PdaHooks::mainScroll(int32 n) {
	// 0x42ce60: a line or a page (the lines that fit the item) up or down, kept inside the text
	Script::ItemText *text = _scrollItem ? _script.findItemText(_scrollItem) : nullptr;
	if (!text)
		return;
	Script::ItemText &t = *text;
	VCInterfaceItem data;
	if (!_data.getInterfaceItem(_scrollItem, data))
		return;
	uint page = MAX(1, (data.rect.bottom - data.rect.top) / t.lineHeight);
	uint last = t.lines.size() > page ? t.lines.size() - page : 0;
	uint first = t.firstLine;
	switch (n) {
	case 0:
		first = first > 0 ? first - 1 : 0;
		break;
	case 1:
		first++;
		break;
	case 2:
		first = first > page ? first - page : 0;
		break;
	case 3:
		first += page;
		break;
	default:
		return;
	}
	t.firstLine = MIN(first, last);
}

// -- Address book -------------------------------------------------------------

void PdaHooks::buildAddressList() {
	// 0x435590: the enabled assets of the item's list
	_addressList.clear();
	VCInterfaceItem item;
	Common::Array<uint32> entries;
	if (_data.getInterfaceItem(kAddressSource, item))
		_data.getMembers(item.assets, entries);
	for (uint i = 0; i < entries.size(); i++) {
		if (_script.isEnabled(entries[i]))
			_addressList.push_back(entries[i]);
	}
}

bool PdaHooks::setAddrMode(char c) {
	switch (c) {
	case 'T':
		_addrDest = &_addrTo;
		break;
	case 'C':
		_addrDest = &_addrCc;
		break;
	case 'P':
		_addrDest = &_addrPda;
		break;
	case 'A':
	case 'B':
	case 'D':
		break;
	default:
		return false;
	}
	_addrMode = c;
	return true;
}

PdaHooks::AddrModeInfo PdaHooks::addrModeInfo() const {
	AddrModeInfo m = { nullptr, nullptr, 0 };
	switch (_addrMode) {
	case 'T':
		m.rows = kAddrRowsT;
		m.checks = kAddrChecksT;
		m.count = ARRAYSIZE(kAddrRowsT);
		break;
	case 'C':
		m.rows = kAddrRowsC;
		m.checks = kAddrChecksC;
		m.count = ARRAYSIZE(kAddrRowsC);
		break;
	case 'P':
		m.rows = kAddrRowsP;
		m.count = ARRAYSIZE(kAddrRowsP);
		break;
	case 'A':
		m.rows = kAddrRowsA;
		m.count = ARRAYSIZE(kAddrRowsA);
		break;
	case 'B':
		m.rows = kAddrRowsB;
		m.count = ARRAYSIZE(kAddrRowsB);
		break;
	case 'D':
		m.rows = kAddrRowsD;
		m.count = ARRAYSIZE(kAddrRowsD);
		break;
	default:
		break;
	}
	return m;
}

uint PdaHooks::addrShownCount() const {
	switch (_addrMode) {
	case 'A':
		return _addrTo.size();
	case 'B':
		return _addrCc.size();
	case 'D':
		return _addrPda.size();
	default:
		return _addrWork.size();
	}
}

uint32 PdaHooks::addrShownId(uint i) const {
	switch (_addrMode) {
	case 'A':
		return _addrTo[i];
	case 'B':
		return _addrCc[i];
	case 'D':
		return _addrPda[i];
	default:
		return _addrWork[i].id;
	}
}

void PdaHooks::drawAddrs() {
	// 0x44af40
	AddrModeInfo mode = addrModeInfo();
	uint count = addrShownCount();
	bool working = _addrMode == 'T' || _addrMode == 'C' || _addrMode == 'P';
	for (uint r = 0; r < mode.count; r++) {
		uint i = _addrTop + r;
		if (i >= count) {
			_script.clearItemText(mode.rows[r]);
			continue;
		}
		if (mode.checks && working && _addrWork[i].selected)
			_script.setItemEnabled(mode.checks[r], true);
		Common::U32String text;
		uint32 id = addrShownId(i);
		if ((_script.readTextFile(id, text) || _script.getTextAsset(id, text)) && !text.empty())
			_script.setItemText(mode.rows[r], text, _pdaFontSize, false, 0, Script::kAlignLeft, false, 0);
		else
			_script.clearItemText(mode.rows[r]);
	}
}

void PdaHooks::displayAddrs(char c) {
	// 0x44aa00
	for (uint i = 0; i < _addrWork.size(); i++)
		_addrWork[i].selected = false;
	setAddrMode(c);
	_addrTop = 0;
	AddrModeInfo mode = addrModeInfo();
	for (uint r = 0; mode.checks && r < mode.count; r++)
		_script.setItemEnabled(mode.checks[r], false);
	drawAddrs();
}

void PdaHooks::addrSelected(int32 n) {
	// 0x42f7f0
	AddrModeInfo mode = addrModeInfo();
	if (!mode.checks || n < 0 || (uint)n >= mode.count || _addrTop + (uint)n >= _addrWork.size())
		return;
	AddrEntry &e = _addrWork[_addrTop + n];
	e.selected = !e.selected;
	_script.setItemEnabled(mode.checks[n], e.selected);
}

void PdaHooks::scrollAddrs(char c, int32 dir) {
	// 0x43fc30
	setAddrMode(c);
	AddrModeInfo mode = addrModeInfo();
	if (!mode.rows)
		return;
	uint count = addrShownCount(), rows = mode.count;
	if (count <= rows)
		return;
	if (dir == 1) {
		if (_addrTop >= count - rows)
			return;
		_addrTop++;
	} else {
		if (_addrTop == 0)
			return;
		_addrTop--;
	}
	for (uint r = 0; mode.checks && r < rows; r++)
		_script.setItemEnabled(mode.checks[r], false);
	drawAddrs();
}

void PdaHooks::createAddrList(char c) {
	// 0x474f40; the exe crashes with no destination, the port skips it
	setAddrMode(c);
	if (_addrDest) {
		_addrDest->clear();
		for (uint i = 0; i < _addrWork.size(); i++) {
			if (_addrWork[i].selected)
				_addrDest->push_back(_addrWork[i].id);
		}
	}
	if (c != 'D')
		_script.setItemEnabled(kMailSendButton, !_addrTo.empty() || !_addrCc.empty());
}

bool PdaHooks::mailRecipients(Common::Array<uint32> &ids) const {
	ids.clear();
	if (_addrTo.empty() && _addrPda.empty())
		return false;
	if (!_addrPda.empty()) {
		ids = _addrPda;
		return true;
	}
	ids = _addrTo;
	for (uint i = 0; i < _addrCc.size(); i++) {
		if (!listHas(_addrTo, _addrCc[i]))
			ids.push_back(_addrCc[i]);
	}
	return true;
}

void PdaHooks::showAddress() {
	Common::U32String text, entry;
	if (_addressIndex < _addressList.size() && _script.getTextAsset(_addressList[_addressIndex], entry)) {
		for (uint i = 0; i < entry.size(); i++) {
			if (entry[i] != '\r')
				text += entry[i];
		}
	}
	_script.setPDAText(kAddressItem, text, kAddressFontSize, true);
}

void PdaHooks::pdaInitAddr() {
	buildAddressList();
	if (_addressIndex >= _addressList.size())
		_addressIndex = 0;
	showAddress();
}

void PdaHooks::pdaFlipAddr(int32 n) {
	// Only with two or more entries
	uint count = _addressList.size();
	if (count <= 1)
		return;
	switch (n) {
	case 0:
		_addressIndex = (_addressIndex + 1) % count;
		showAddress();
		break;
	case 1:
		_addressIndex = (_addressIndex + count - 1) % count;
		showAddress();
		break;
	case 2:
	case 3:
		for (uint i = 0; i < 2; i++)
			_script.setItemEnabled(kAddressEnable[i], n == 2);
		break;
	case 4:
		buildAddressList();
		break;
	default:
		break;
	}
}

// -- Notepad ------------------------------------------------------------------

bool PdaHooks::hasNote(uint32 id) const {
	return listHas(_notes, id);
}

bool PdaHooks::dayHasNote(uint day, uint32 note) const {
	VCInterfaceItem item;
	Common::Array<uint32> members;
	if (day >= kNoteDayCount || !_data.getInterfaceItem(kNoteDayItems[day], item) || !_data.getMembers(item.assets, members))
		return false;
	return listHas(members, note);
}

void PdaHooks::addNote(uint32 id) {
	// The notes are a set the save writes sorted by ID (as the conversation history, Script::recordTopic())
	uint i = 0;
	while (i < _notes.size() && _notes[i] < id)
		i++;
	if (i == _notes.size() || _notes[i] != id)
		_notes.insert_at(i, id);
}

void PdaHooks::enablePDANote(uint32 id) {
	// 0x4e6660: a note of the next day opens that day
	addNote(id);
	if (_noteDays < kNoteDayCount && dayHasNote(noteDay(_noteDays), id))
		_noteDays++;
}

void PdaHooks::enableAllNotes() {
	// 0x4e6810: every note of every day, in order
	for (uint pos = 0; pos < kNoteDayCount; pos++) {
		uint day = noteDay(pos);
		VCInterfaceItem item;
		Common::Array<uint32> members;
		if (_data.getInterfaceItem(kNoteDayItems[day], item))
			_data.getMembers(item.assets, members);
		for (uint i = 0; i < members.size(); i++)
			addNote(members[i]);
	}
	_noteDays = kNoteDayCount;
}

Common::U32String PdaHooks::noteDayText() const {
	// 0x4e71a0: the scroll text starts with a blank, then each note of the day in list order
	VCInterfaceItem item;
	Common::Array<uint32> members;
	if (_noteDay < kNoteDayCount && _data.getInterfaceItem(kNoteDayItems[noteDay(_noteDay)], item))
		_data.getMembers(item.assets, members);
	Common::U32String text(" ");
	for (uint i = 0; i < _notes.size(); i++) {
		Common::U32String note;
		// 0x45f690: the asset's file first, else its text
		if (listHas(members, _notes[i]) && (_script.readTextFile(_notes[i], note) || _script.getTextAsset(_notes[i], note)))
			text += note;
	}
	return text;
}

void PdaHooks::showNoteDayText() {
	setScrollText(kNoteTextItem, noteDayText(), kNoteFontSize);
	// 0x4e71a0 also sets the record shown (0x6b9b94) to the day's item, which EmailSend 'N' reads
	_script.computer().setRecord(_noteDay < kNoteDayCount ? kNoteDayItems[noteDay(_noteDay)] : 0);
}

void PdaHooks::notesPreview(uint32 address) {
	// 0x4e6b90: after an address is picked for the notepad send, the day's header (item 40873) and body (the
	// scroll text) for that address, else "Field Notes" and the day's notes. The day is the raw shown page
	static const struct {
		uint day;
		uint32 address, body, header;
	} kPreviews[] = {
		{ 5, 45298, 61766, 67075 }, { 6, 45300, 61770, 67083 }, { 6, 45298, 61767, 67077 }, { 10, 45284, 61771, 67085 },
		{ 16, 45298, 61774, 67091 }, { 19, 45298, 61772, 67087 }, { 21, 45298, 61773, 67089 }
	};
	static const uint32 kHeaderItem = 40873, kWongChecked = 1980, kAddressCook = 45290;
	uint32 header = 0, body = 0;
	if (_noteDay == 1) {
		if (address == kAddressCook) {
			if (_script.getVariable(kWongChecked) == 0) {
				header = 67073;
				body = 61776;
			}
		}
	} else if (_noteDay == 4) {
		if (address == 45298) {
			header = 67079;
			body = hasNote(63288) ? 61769 : 61768;
		}
	} else {
		for (uint i = 0; i < ARRAYSIZE(kPreviews); i++) {
			if (kPreviews[i].day == _noteDay && kPreviews[i].address == address) {
				header = kPreviews[i].header;
				body = kPreviews[i].body;
				break;
			}
		}
	}
	if (header) {
		Common::U32String text;
		if (!_script.readTextFile(header, text))
			_script.getTextAsset(header, text);
		_script.setPDAText(kHeaderItem, text, kNoteFontSize, true);
		text.clear();
		if (!_script.readTextFile(body, text))
			_script.getTextAsset(body, text);
		replaceScrollText(text);
	} else {
		_script.setPDAText(kHeaderItem, Common::U32String("Field Notes"), kNoteFontSize, true);
		replaceScrollText(noteDayText());
		_script.computer().setRecord(_noteDay < kNoteDayCount ? kNoteDayItems[noteDay(_noteDay)] : 0);
	}
	// 0x5ceaa0: mail to Mark Cook counts for iNumWillmoreEMailsToCook; on page 1 it also sets the NSA flag
	if (address == kAddressCook) {
		if (_noteDay == 1 && _script.getCookEmailFlag())
			_script.setVariable(_script.getCookEmailFlag(), 1);
		if (_script.getCookEmailCount())
			_script.setVariable(_script.getCookEmailCount(), _script.getVariable(_script.getCookEmailCount()) + 1);
	}
}

void PdaHooks::addrSelPDA(int32 n) {
	// 0x4ec520: n is the row's index in the working list; it becomes the PDA list's only address
	if (n < 0 || (uint)n >= _addrWork.size())
		return;
	_addrWork[n].selected = true;
	setAddrMode('D');
	if (_addrDest) {
		_addrDest->clear();
		for (uint i = 0; i < _addrWork.size(); i++)
			if (_addrWork[i].selected)
				_addrDest->push_back(_addrWork[i].id);
	}
	for (uint i = 0; i < _addrWork.size(); i++)
		_addrWork[i].selected = false;
	setAddrMode('D');
	_addrTop = 0;
	drawAddrs();
	notesPreview(_addrWork[n].id);
}

void PdaHooks::showNoteMarker() {
	// The day label, and the marker: the day's key note is in the list (day 2 needs two more)
	_script.setPDAText(kNoteLabelItem, Common::U32String(Common::String::format("%u", _noteDay + 1)), kNoteFontSize, true);
	bool shown = _noteDay < kNoteDayCount && hasNote(kNoteKeys[noteDay(_noteDay)]);
	if (shown && _noteDay == kNoteMarkerDay)
		shown = hasNote(kNoteMarkerExtra[0]) && hasNote(kNoteMarkerExtra[1]);
	_script.setItemEnabled(kNoteMarkerItem, shown);
}

uint PdaHooks::noteDay(uint pos) const {
	return pos < kNoteDayCount ? _noteOrder[pos] : pos;
}

void PdaHooks::applyNoteOrder() {
	// The newer exe (0x4e80b0, database build 9) reorders the days from four variables, by name as IDs are per
	// database; the English exe has none. The table is only ever set, never put back (the exe's is process-wide)
	static const uint32 kVarWong = 5328, kVarTarakan = 5329, kVarCoroners = 5330, kVarHaulingYard = 5325;
	Common::U32String name;
	if (!_data.getVariableName(kVarCoroners, name) || name.encode() != "PDANOTES_bWentToCoronersBeforeFO")
		return;
	// 5..8 = 6,7,8,5 / 6,7,5 / 6,5 by the first variable that is 1
	static const uint kFirst[] = {6, 7, 8, 5};
	uint count = 0;
	if (_script.getVariable(kVarCoroners) == 1)
		count = 4;
	else if (_script.getVariable(kVarTarakan) == 1)
		count = 3;
	else if (_script.getVariable(kVarWong) == 1)
		count = 2;
	// 5 is the last entry of each list
	for (uint i = 0; i < count; i++)
		_noteOrder[5 + i] = i + 1 == count ? 5 : kFirst[i];
	if (_script.getVariable(kVarHaulingYard) == 1) {
		_noteOrder[13] = 14;
		_noteOrder[14] = 13;
	}
	bool identity = true;
	Common::String order;
	for (uint i = 0; i < kNoteDayCount; i++) {
		identity = identity && _noteOrder[i] == i;
		order += Common::String::format(" %u", _noteOrder[i]);
	}
	if (!identity)
		_script.logLine("      notepad: order" + order);
}

void PdaHooks::notePadInit() {
	// 0x4e6220: the days are those from the first on that each have a note, at least one
	applyNoteOrder();
	uint days = 0;
	while (days < kNoteDayCount) {
		bool any = false;
		for (uint i = 0; i < _notes.size() && !any; i++)
			any = dayHasNote(noteDay(days), _notes[i]);
		if (!any)
			break;
		days++;
	}
	_noteDays = MAX<uint>(1, days);
	showNoteDayText();
	showNoteMarker();
}

void PdaHooks::noteFlipDay(int32 n) {
	// 0x4e6a70
	uint day = _noteDay;
	if (n == 0 && _noteDay + 1 < _noteDays)
		day++;
	else if (n == 1 && _noteDay > 0)
		day--;
	if (day != _noteDay) {
		_noteDay = day;
		// Queue_Show on the scroll text's object: no enable change, no triggers
		_script.queueShowItem(kNoteTextItem, false);
		showNoteDayText();
		_script.queueShowItem(kNoteTextItem, true);
	}
	showNoteMarker();
}

} // End of namespace XFiles
