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
#include <stddef.h>

/** Overwrite secret data (passwords, password hashes) with zeros.
 *
 * Writes go through a volatile pointer so the compiler cannot drop them
 * as dead stores, which a plain memset() before free is allowed to be.
 */
static inline void DtaSecureZero(void *buf, size_t len)
{
	if (buf == NULL) return;
	volatile unsigned char *p = (volatile unsigned char *)buf;
	while (len--) *p++ = 0;
}

#ifdef __cplusplus
#include <vector>
#include <stdint.h>

/** Wipes a byte vector when it goes out of scope, on every return path.
 *
 * Declare one right after a vector that will hold a password or a password
 * hash:  std::vector<uint8_t> hash; DtaWipeOnExit wipeHash(hash);
 */
class DtaWipeOnExit {
public:
	explicit DtaWipeOnExit(std::vector<uint8_t> &v) : vec(v) {}
	~DtaWipeOnExit() { DtaSecureZero(vec.data(), vec.capacity()); }
	DtaWipeOnExit(const DtaWipeOnExit &) = delete;
	DtaWipeOnExit &operator=(const DtaWipeOnExit &) = delete;
private:
	std::vector<uint8_t> &vec;
};
#endif
