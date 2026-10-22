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
#include <iostream>
#include <iomanip>
#include "DtaHashPwd.h"
#include "DtaLexicon.h"
#include "DtaOptions.h"
#include "DtaDev.h"
#include "log.h"

extern "C" {
#include "pbkdf2.h"
#include "sha2.h"
}
using namespace std;

void DtaHashPassword(vector<uint8_t> &hash, char * password, vector<uint8_t> salt,
	unsigned int iter, uint8_t hashsize)
{
	LOG(D1) << " Entered DtaHashPassword";
	// if the hashsize can be > 255 the token overhead logic needs to be fixed
	assert(1 == sizeof(hashsize));
	if (253 < hashsize) { LOG(E) << "Hashsize > 253 incorrect token generated"; }
	
	hash.clear();
	// don't hash the devault OPAL password ''
	if (0 == strnlen(password, 32)) {
		goto exit;
	}
	hash.reserve(hashsize + 2); // hope this will prevent reallocation
	for (uint16_t i = 0; i < hashsize; i++) {
		hash.push_back(' ');
	}
	
	cf_pbkdf2_hmac((uint8_t *)password, strnlen(password, 256),
		salt.data(), salt.size(),
		iter,
		hash.data(), hash.size(),
		&cf_sha512);

//	gc_pbkdf2_sha1(password, strnlen(password, 256), (const char *)salt.data(), salt.size(), iter,
//		(char *)hash.data(), hash.size());
exit:	// add the token overhead
	hash.insert(hash.begin(), (uint8_t)hash.size());
	hash.insert(hash.begin(), 0xd0);
}

void DtaHashPwd(vector<uint8_t> &hash, char * password, DtaDev * d)
{
    LOG(D1) << " Entered DtaHashPwd";
    char *serNum;

    if (d->no_hash_passwords) {
        hash.clear();
	for (uint16_t i = 0; i < strnlen(password, 32); i++)
		hash.push_back(password[i]);
	// add the token overhead
	hash.insert(hash.begin(), (uint8_t)hash.size());
	hash.insert(hash.begin(), 0xd0);
	LOG(D1) << " Exit DtaHashPwd";
	return;
    }
    serNum = d->getSerialNum();
    vector<uint8_t> salt(serNum, serNum + 20);
    //	vector<uint8_t> salt(DEFAULTSALT);
    DtaHashPassword(hash, password, salt);
    LOG(D1) << " Exit DtaHashPwd"; // log for hash timing
}

struct PBKDF_TestTuple
{
    uint8_t hashlen;
    unsigned int iterations;
    const char *Password, *Salt, *hexDerivedKey;
};

int testresult(std::vector<uint8_t> &result, const char * expected, size_t len) {
	char work[64];
	if (len > sizeof(work)) return 1;
	int p = 0;
	printf("Expected Result: %s\nActual Result  : ", expected);
	for (uint32_t i = 0; i < len; i++) { printf("%02x", result[i + 2]); }; printf("\n");
	for (uint32_t i = 0; i < len * 2; i += 2) {
		work[p] = expected[i] & 0x40 ? 16 * ((expected[i] & 0xf) + 9) : 16 * (expected[i] & 0xf);
		work[p] += expected[i + 1] & 0x40 ? (expected[i + 1] & 0xf) + 9 : expected[i + 1] & 0xf;
		p++;
	}
	return memcmp(result.data()+2, work, len);
}

int Testsedutil(const PBKDF_TestTuple *testSet, unsigned int testSetSize)
{
    int pass = 1;
    std::vector<uint8_t> hash, seaSalt;

    for (unsigned int i = 0; i < testSetSize; i++) {
        const PBKDF_TestTuple &tuple = testSet[i];
        hash.clear();
        seaSalt.clear();
        for (uint16_t j = 0; j < strnlen(tuple.Salt, 255); j++) {
            seaSalt.push_back(tuple.Salt[j]);
        }
		printf("Password %s Salt %s Iterations %i Length %i\n", (char *)tuple.Password,
			(char *) tuple.Salt, tuple.iterations, tuple.hashlen);
		DtaHashPassword(hash, (char *) tuple.Password, seaSalt, tuple.iterations, tuple.hashlen);
		int fail = (testresult(hash, tuple.hexDerivedKey, tuple.hashlen) == 0);
        pass = pass & fail;
    }

    return pass;
}
int TestPBKDF2()
{
    int pass = 1;
    // from draft-ietf-smime-password-03.txt, at http://www.imc.org/draft-ietf-smime-password
    // PBKDF2-HMAC-SHA512 test vectors, the same algorithm used to hash
    // drive passwords. Checked against Python's hashlib.pbkdf2_hmac('sha512').
    PBKDF_TestTuple testSet[] = {
        { 32, 1, "password", "salt",
            "867f70cf1ade02cff3752599a3a53dc4af34c7a669815ae5d513554e1c8cf252"},
        { 32, 2, "password", "salt",
            "e1d9c16aa681708a45f5c7c4e215ceb66e011a2e9f0040713f18aefdb866d53c"},
        { 32, 4096, "password", "salt",
            "d197b1b33db0143e018b12f3d1d1479e6cdebdcc97c5c0f87f6902e072f457b5"},
        { 40, 4096, "passwordPASSWORDpassword", "saltSALTsaltSALTsaltSALTsaltSALTsalt",
            "8c0511f4c6e597c6ac6315d8f0362e225f3c501495ba23b868c005174dc4ee71115b59f9e60cd953"},
        // sedutil's real settings: 500,000 iterations, 32 byte key, 20 byte serial-number salt
        { 32, 500000, "correct horse battery staple", "S3RIALNUMBER12345678",
            "48b6be0f2cdc254aa158363b4f6f4584fc3661810067eedcd11d92bdaf16a302"},
    };

    cout << "\nPKCS #5 PBKDF2 validation suite running ... \n\n";
    pass = Testsedutil(testSet, sizeof (testSet) / sizeof (testSet[0])) && pass;
    cout << "\nPKCS #5 PBKDF2 validation suite ... ";
    if (pass)
        cout << "passed\n";
    else
        cout << "**FAILED**\n";
    return pass ? 0 : 1;
}

