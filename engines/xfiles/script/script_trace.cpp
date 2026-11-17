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

#include "xfiles/script/script_trace.h"
#include "xfiles/script/vcdata.h"

namespace XFiles {

// The engine's own display tables (XFiles.exe VA 0x6b4270, 0x6b42e8, 0x6b4740, 0x6b4590)
static const char *const kConditionOps[] = { "==", "!=", ">", "<", ">=", "<=", "and", "or" };
static const char *const kStatementOps[] = { "=", "++", "--", "+=", "-=", "*=", "/=", "%=" };
static const char *const kAssetModes[] = { "preload", "play_once", "play_loop", "stop", "unload" };
static const char *const kTimerCommands[] = { "start", "stop" };
const char *const kDirections[] = {
	"", "North", "NorthEast", "East", "SouthEast", "South", "SouthWest", "West", "NorthWest"
};

// Names of Event::n operands per event type (XFiles.exe 0x5d0a90)
static const char *const kPointerFields[] = { "XCoord", "YCoord", "IsDragging", "DragObjClass", "DragObjID", nullptr };
static const char *const kClickFields[] = { "XCoord", "YCoord", nullptr };
static const char *const kKeyFields[] = { "Key", "IsControlDown", "IsAltDown", "IsOptionDown", nullptr };
static const char *const kDropFields[] = { "XCoord", "YCoord", "DropObjClass", "DropObjID", nullptr };
static const char *const kTimerFields[] = { "TimerID", nullptr };
static const char *const kObjectFields[] = { "ObjClass", "ObjID", nullptr };
static const char *const *const kEventFields[] = {
	kClickFields, kClickFields, kPointerFields, kKeyFields, kPointerFields, kPointerFields, kDropFields,
	kTimerFields, kObjectFields, kObjectFields
};

Common::String utf8(const Common::U32String &str) {
	return str.encode(Common::kUtf8);
}

Common::String nameOrNone(const VCData &data, uint32 id) {
	Common::U32String name;
	return data.getName(id, name) ? utf8(name) : Common::String("None");
}


Common::String pythonRepr(const Common::U32String &str) {
	bool hasSingle = false, hasDouble = false;
	for (uint i = 0; i < str.size(); i++) {
		hasSingle |= str[i] == '\'';
		hasDouble |= str[i] == '"';
	}
	Common::u32char_type_t quote = (hasSingle && !hasDouble) ? '"' : '\'';
	Common::U32String out;
	out += quote;
	for (uint i = 0; i < str.size(); i++) {
		Common::u32char_type_t c = str[i];
		if (c == quote || c == '\\') {
			out += '\\';
			out += c;
		} else if (c == '\t') {
			out += Common::U32String("\\t");
		} else if (c == '\n') {
			out += Common::U32String("\\n");
		} else if (c == '\r') {
			out += Common::U32String("\\r");
		} else if (c < 0x20 || (c >= 0x7f && c <= 0xa0) || c == 0xad) {
			out += Common::U32String(Common::String::format("\\x%02x", c));
		} else {
			out += c;
		}
	}
	out += quote;
	return utf8(out);
}
Common::String getObjectLabel(const VCData &data, uint32 id) {
	uint32 classId, mark;
	if (!data.lookup(id, classId, mark))
		return Common::String::format("?#%u", id);
	const char *className = getClassName(classId);
	Common::String text = className ? Common::String::format("%s#%u", className, id) : Common::String::format("%u#%u", classId, id);

	Common::String name;
	if (classId == kClassView) {
		VCView view;
		data.getView(id, view);
		Common::U32String text32;
		if (view.facing < ARRAYSIZE(kDirections))
			name = kDirections[view.facing];
		else if (data.getName(view.facing, text32))
			name = utf8(text32);
	} else {
		// The scene classes start with their child list
		uint32 offset = 6;
		if (classId == kClassTitle)
			offset = 60;
		else if (classId == kClassNode || classId == kClassLocation)
			offset = 32;
		else if (classId == kClassViewPoint)
			offset = 36;
		Common::U32String text32;
		if (data.getName(data.getHdb().readUint32(mark + offset), text32))
			name = utf8(text32);
	}
	return name.empty() ? text : text + " \"" + name + "\"";
}

static Common::String describeOperand(const VCData &data, const VCOperand &operand, int event) {
	Common::U32String name;
	switch (operand.kind) {
	case kOperandVariable:
		if (data.getVariableName(operand.value, name) && !name.empty())
			return utf8(name);
		return Common::String::format("var#%u", operand.value);
	case kOperandLocal:
		return Common::String::format("local#%u", operand.value);
	case kOperandChar:
		if (operand.value >= 32 && operand.value < 127)
			return pythonRepr(Common::U32String(Common::String((char)operand.value)));
		return Common::String::format("char#%u", operand.value);
	case kOperandEvent:
		if (event >= 0 && event < (int)ARRAYSIZE(kEventFields)) {
			const char *const *fields = kEventFields[event];
			for (uint i = 0; fields[i]; i++) {
				if (i == operand.value)
					return Common::String("Event::") + fields[i];
			}
		}
		return Common::String::format("Event::%u", operand.value);
	case kOperandObject:
		return getObjectLabel(data, operand.value);
	default:
		return Common::String::format("%u", operand.value);
	}
}

static Common::String describeExpression(const VCData &data, const VCExpression &e, const Common::String &op, int event) {
	return describeOperand(data, e.lhs, event) + " " + op + " " + describeOperand(data, e.rhs, event);
}

Common::String describeAction(const VCData &data, uint32 id, int event, bool withCondition) {
	VCAction a;
	if (!data.getAction(id, a))
		return Common::String::format("<bad action #%u>", id);

	Common::String text;
	if (a.conditional && withCondition)
		text = "if " + describeExpression(data, a.condition, tableName(kConditionOps, a.condition.op), event) + ": ";

	switch (a.type) {
	case kActionStatement:
		text += describeExpression(data, a.statement, tableName(kStatementOps, a.statement.op), event);
		break;
	case kActionEnable: {
		uint32 target;
		bool enabled;
		text += a.value ? "enable " : "disable ";
		if (data.getEnabled(a.target, target, enabled) && target)
			text += getObjectLabel(data, target);
		else
			text += Common::String::format("<missing VCEnabled #%u>", a.target);
		break;
	}
	case kActionSetView:
		text += "set_view ";
		for (int i = 0; i < 4; i++)
			text += (i ? " / " : "") + getObjectLabel(data, a.path[i]);
		break;
	case kActionFunction:
		text += Common::U32String(a.function, Common::kISO8859_1).encode(Common::kUtf8) + "(";
		for (uint i = 0; i < a.args.size(); i++)
			text += (i ? ", " : "") + describeOperand(data, a.args[i], event);
		text += ")";
		break;
	case kActionAsset:
		text += tableName(kAssetModes, a.mode) + " " + getObjectLabel(data, a.assetList);
		break;
	case kActionSound3D:
		text += tableName(kAssetModes, a.mode) + " " + getObjectLabel(data, a.assetList) +
			Common::String::format(" at (%d, %d)", a.x, a.y);
		break;
	case kActionInterface:
		text += (a.mode ? "unshow " : "display ") + getObjectLabel(data, a.layout);
		break;
	case kActionTimer:
		text += Common::String::format("timer %u ", a.timer) + tableName(kTimerCommands, a.command);
		if (a.command == 0)
			text += Common::String::format(" %u ms", a.ms) + (a.periodic ? " periodic" : "");
		break;
	default:
		text += Common::String::format("%u {'raw': '", a.type);
		for (uint i = 0; i < a.raw.size(); i++)
			text += Common::String::format("%02x", a.raw[i]);
		text += "'}";
		break;
	}
	return text;
}

} // End of namespace XFiles
