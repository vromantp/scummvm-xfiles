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

#ifndef XFILES_SCRIPT_TRACE_H
#define XFILES_SCRIPT_TRACE_H

#include "common/str.h"
#include "common/ustr.h"

namespace XFiles {

class VCData;

/*
 * The readable text of the script's objects and actions: the interpreter's
 * trace lines and the debugger's dumps use the same strings, which match the
 * research tools (neoaccess.py, vc_classes.py).
 */

/** Names of the eight view directions, indexed by VCView facing (0 = none). */
extern const char *const kDirections[9];

/** Name from a table, or the number if it is out of range. */
template<size_t N>
static Common::String tableName(const char *const (&table)[N], uint value) {
	return value < N ? Common::String(table[value]) : Common::String::format("%u", value);
}

/** A UTF-32 string as UTF-8. */
Common::String utf8(const Common::U32String &str);

/** Class name, ID and name of an .hdb object, e.g. `VCNode#1234 "Node 1: Setup"`. */
Common::String getObjectLabel(const VCData &data, uint32 id);

/**
 * One-line readable form of a VCAction. event is the type of the trigger
 * that runs it (to name Event:: operands), or -1. withCondition = false
 * leaves out the "if ...: " prefix.
 */
Common::String describeAction(const VCData &data, uint32 id, int event = -1, bool withCondition = true);

/** Text of a VCName as UTF-8, or "None" like the Python tools. */
Common::String nameOrNone(const VCData &data, uint32 id);

/** Python's repr() of a string, as UTF-8. */
Common::String pythonRepr(const Common::U32String &str);

} // End of namespace XFiles

#endif
