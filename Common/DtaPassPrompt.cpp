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
#include <stdio.h>
#include "DtaPassPrompt.h"
#include "DtaSecureZero.h"

#ifdef _WIN32
#include <conio.h>

static void dropLastChar(std::string &s)
{
	if (s.empty()) return;
	s[s.size() - 1] = 0;
	s.resize(s.size() - 1);
}
#else
#include <termios.h>
#include <unistd.h>
#endif

bool DtaReadHiddenPassword(const char *prompt, std::string &out)
{
	DtaSecureZero(&out[0], out.size());
	out.clear();
	out.reserve(256); // avoid reallocations leaving copies of the password
	fprintf(stderr, "%s", prompt);
	fflush(stderr);
	bool gotLine = false;
#ifdef _WIN32
	for (;;) {
		int ch = _getch();
		if (ch == '\r' || ch == '\n') { gotLine = true; break; }
		if (ch == 3) break;                      // Ctrl+C: cancel
		if (ch == 0 || ch == 0xE0) { _getch(); continue; } // arrow/function keys
		if (ch == '\b') { dropLastChar(out); continue; }
		if (ch < 32) continue;                   // ignore other control keys
		out += (char)ch;
	}
#else
	struct termios oldt, newt;
	bool tty = (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &oldt) == 0);
	if (tty) {
		newt = oldt;
		newt.c_lflag &= ~ECHO;
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &newt);
	}
	for (;;) {
		int ch = getchar();
		if (ch == EOF) break;
		if (ch == '\n' || ch == '\r') { gotLine = true; break; }
		out += (char)ch;
	}
	if (tty) tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
#endif
	fprintf(stderr, "\n");
	if (!gotLine) {
		DtaSecureZero(&out[0], out.size());
		out.clear();
	}
	return gotLine;
}
