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
#include "UnlockSEDs.h"
#include "DtaDevGeneric.h"
#include "DtaDevOpal1.h"
#include "DtaDevOpal2.h"
#include "DtaDevOpalite.h"
#include "DtaDevPyrite1.h"
#include "DtaDevPyrite2.h"
#include "DtaDevRuby1.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <dirent.h>
#include <fnmatch.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <algorithm>

using namespace std;

/* Print a status line on the console, and copy it to the first serial port
 * when one exists. The copy lets the CI boot test (and anyone debugging on
 * a serial console) see which drives were found. Status lines never contain
 * the password or its hash. */
static void pbaStatus(const char *fmt, ...)
{
    char line[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fputs(line, stdout);
    fflush(stdout);
    int fd = open("/dev/ttyS0", O_WRONLY | O_NOCTTY | O_NONBLOCK);
    if (fd >= 0) {
        if (write(fd, "sedutil-pba: ", 13) < 0 || write(fd, line, strlen(line)) < 0) {
            /* nothing to do, the copy is best effort */
        }
        close(fd);
    }
}

/* Names of the drives sedutil can talk to: SATA/SAS as sdX (sda..sdz,
 * sdaa..sdzz) and NVMe controllers as nvmeN. */
static vector<string> listDrives()
{
    vector<string> devices;
    DIR *dir = opendir("/dev");
    if (dir == NULL)
        return devices;
    struct dirent *dirent;
    while ((dirent = readdir(dir)) != NULL) {
        if ((!fnmatch("sd[a-z]", dirent->d_name, 0)) ||
            (!fnmatch("sd[a-z][a-z]", dirent->d_name, 0)) ||
            (!fnmatch("nvme[0-9]", dirent->d_name, 0)) ||
            (!fnmatch("nvme[0-9][0-9]", dirent->d_name, 0))) {
            devices.push_back(dirent->d_name);
        }
    }
    closedir(dir);
    std::sort(devices.begin(), devices.end());
    return devices;
}

/* NVMe and USB devices can appear a few seconds after the kernel starts
 * init. Wait until the list of drives has stopped changing for 1.5 seconds,
 * or 10 seconds in total, so a slow drive is not skipped. */
static vector<string> waitForDrives()
{
    const int pollMs = 250, stableMs = 1500, maxMs = 10000;
    vector<string> last = listDrives();
    int stableFor = 0;
    for (int waited = 0; waited < maxMs; waited += pollMs) {
        struct timespec ts = { 0, pollMs * 1000000L };
        nanosleep(&ts, NULL);
        vector<string> now = listDrives();
        if (now == last) {
            stableFor += pollMs;
            if (stableFor >= stableMs && !now.empty())
                break;
        }
        else {
            stableFor = 0;
            last = now;
        }
    }
    return last;
}

uint8_t UnlockSEDs(char * password) {
    char devref[25];
    int failed = 0;
    DtaDev *tempDev;
    DtaDev *d;
    LOG(D4) << "Enter UnlockSEDs";
    printf("\nScanning....\n");
    vector<string> devices = waitForDrives();
    if (devices.empty())
        pbaStatus("No drives found\n");
    for (size_t i = 0; i < devices.size(); i++) {
        snprintf(devref, sizeof(devref), "/dev/%s", devices[i].c_str());
        tempDev = new DtaDevGeneric(devref);
        if (!tempDev->isPresent()) {
            // Skip this one and keep going: a missing or unreadable device
            // must not stop the drives after it from being unlocked.
            pbaStatus("Drive %-10s not responding, skipped\n", devref);
            delete tempDev;
            continue;
        }
        if ((!tempDev->isOpal1()) && (!tempDev->isOpal2()) &&
            (!tempDev->isOpalite()) && (!tempDev->isPyrite1()) &&
            (!tempDev->isPyrite2()) && (!tempDev->isRuby1())) {
            pbaStatus("Drive %-10s %-40s not OPAL\n", devref, tempDev->getModelNum());
            delete tempDev;
            continue;
        }
        if (tempDev->isRuby1())
            d = new DtaDevRuby1(devref);
        else if (tempDev->isOpal2())
            d = new DtaDevOpal2(devref);
        else if (tempDev->isOpalite())
            d = new DtaDevOpalite(devref);
        else if (tempDev->isPyrite2())
            d = new DtaDevPyrite2(devref);
        else if (tempDev->isPyrite1())
            d = new DtaDevPyrite1(devref);
        else
            d = new DtaDevOpal1(devref);
        delete tempDev;
        d->no_hash_passwords = false;
        failed = 0;
        if (d->Locked()) {
            if (d->MBREnabled()) {
                if (d->setMBRDone(1, password)) {
                    failed = 1;
                }
            }
            if (d->setLockingRange(0, OPAL_LOCKINGSTATE::READWRITE, password)) {
                failed = 1;
            }
            if (failed)
                pbaStatus("Drive %-10s %-40s is OPAL Failed\n", devref, d->getModelNum());
            else
                pbaStatus("Drive %-10s %-40s is OPAL Unlocked\n", devref, d->getModelNum());
        }
        else {
            pbaStatus("Drive %-10s %-40s is OPAL NOT LOCKED\n", devref, d->getModelNum());
        }
        delete d;
    }
    return 0x00;
};
