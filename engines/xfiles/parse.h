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

#ifndef XFILES_PARSE_H
#define XFILES_PARSE_H

#include "common/scummsys.h"
#include "common/util.h"

namespace XFiles {

/**
 * Reads a decimal integer (leading blanks and a sign allowed) from the start of text, the way atoi does but
 * saying whether there was one. Returns the position after the number, or nullptr when there are no digits.
 * A value beyond 32 bits is clamped.
 */
inline const char *parseInt(const char *text, int32 &value) {
	while (Common::isSpace((unsigned char)*text))
		text++;
	bool negative = *text == '-';
	if (*text == '-' || *text == '+')
		text++;
	if (!Common::isDigit((unsigned char)*text))
		return nullptr;
	int64 number = 0;
	for (; Common::isDigit((unsigned char)*text); text++) {
		if (number <= 0x7FFFFFFF)
			number = number * 10 + (*text - '0');
	}
	number = negative ? -number : number;
	value = (int32)CLIP<int64>(number, -0x80000000LL, 0x7FFFFFFFLL);
	return text;
}

/** parseInt() for a value that may be missing: atoi's 0 then. */
inline int32 parseIntOr0(const char *text) {
	int32 value = 0;
	return parseInt(text, value) ? value : 0;
}

} // End of namespace XFiles

#endif
