/* C:B**************************************************************************
This software is Copyright 2014-2017 Bright Plaza Inc. <drivetrust@drivetrust.com>

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

#include "os.h"
#include "GetPassPhrase.h"
#include "DtaSecureZero.h"
#include <string>
#include <termios.h>
#include <stdio.h>
using namespace std;

/* Read a password from the console without echoing it.
 *
 * Canonical mode, echo and keyboard signals (Ctrl+C, Ctrl+\, Ctrl+Z) are
 * turned off for the whole prompt, so the prompt cannot be interrupted
 * into a shell. Settings are restored before returning.
 */
string GetPassPhrase(const char *prompt, bool show_asterisk)
{
  const int BACKSPACE = 127;
  const int CTRL_H = 8;
  struct termios tiosold, tiosnew;
  bool have_tty = (tcgetattr(0, &tiosold) == 0);
  string password;
  password.reserve(256); // avoid reallocations leaving copies of the password
  LOG(D4) << "Enter GetPassPhrase" << endl;
  if (have_tty) {
    tiosnew = tiosold;
    tiosnew.c_lflag &= ~(ICANON | ECHO | ISIG);
    tiosnew.c_cc[VMIN] = 1;
    tiosnew.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &tiosnew);
  }
  printf("\n\n%s", prompt);
  fflush(stdout);
  for (;;) {
    int ch = getchar();
    if (ch == EOF || ch == '\n' || ch == '\r')
      break;
    if (ch == BACKSPACE || ch == CTRL_H) {
      if (password.length() != 0) {
        if (show_asterisk)
          printf("\b \b");
        password[password.length() - 1] = 0;
        password.resize(password.length() - 1);
      }
    }
    else if (ch >= 32) { // ignore Escape and other control keys
      password += (char)ch;
      if (show_asterisk)
        printf("*");
    }
    fflush(stdout);
  }
  if (have_tty)
    tcsetattr(0, TCSANOW, &tiosold);
  return password;
}
