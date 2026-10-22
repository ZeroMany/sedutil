/* C:B**************************************************************************
This file is part of sedutil.

sedutil is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

sedutil is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with sedutil.  If not, see <http://www.gnu.org/licenses/>.

 * C:E********************************************************************** */
#pragma once
#include <string>

/** Read a password from the console without showing it.
 *
 * Works on Windows and Linux. Reads from the keyboard even when stdin is
 * redirected on Windows; on Linux echo is turned off on stdin when it is a
 * terminal.
 *
 * @param prompt text printed before reading (to stderr)
 * @param out    receives the password
 * @return true if a line was read, false on end of input
 */
bool DtaReadHiddenPassword(const char *prompt, std::string &out);
