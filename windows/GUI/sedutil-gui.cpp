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

/* sedutil-gui: a small Windows app to see, lock and unlock Opal drives.
 *
 * It links the same drive code as sedutil-cli, so the password is hashed
 * exactly as the boot screen (PBA) does it and never leaves this process.
 * Drive work runs on a worker thread so the window stays responsive.
 *
 * Like GParted, nothing happens when you click a drive: you queue operations,
 * review them, and only "Apply" runs them, asking for each drive's password
 * at that moment. Passwords are never stored while operations are pending.
 *
 * Safety rules:
 *  - The drive Windows runs from is never locked.
 *  - At Apply, each queued drive must still have the same disk number and
 *    serial number as when it was queued.
 *  - The queue stops at the first failure.
 *  - Before locking, every volume on the drive is locked, dismounted and taken
 *    offline. If any volume is in use, nothing is locked.
 *  - Only the global locking range (range 0) is changed, which is how sedutil
 *    sets drives up.
 */

#include "os.h"
#include <commctrl.h>
#include <windowsx.h>
#include <winioctl.h>
#include <string>
#include <vector>
#include <thread>
#include "DtaDevGeneric.h"
#include "DtaDevOpal1.h"
#include "DtaDevOpal2.h"
#include "DtaDevOpalite.h"
#include "DtaDevPyrite1.h"
#include "DtaDevPyrite2.h"
#include "DtaDevRuby1.h"
#include "DtaLexicon.h"
#include "DtaSecureZero.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

/* log.h needs this; sedutil-cli defines it in sedutil.cpp */
sedutiloutput outputFormat = sedutilReadable;

namespace {

const int MAX_DRIVES = 32;

enum {
	IDC_LIST = 100,
	IDC_REFRESH,
	IDC_STATUS,
};

struct Drive {
	int number = -1;
	std::string devref;
	std::string model, serial, firmware;
	bool opal = false;            // speaks Opal / Ruby / Pyrite
	bool lockingEnabled = false;  // set up with sedutil (or another tool)
	bool locked = false;
	bool isSystem = false;        // holds the running Windows
};

struct Result {
	std::vector<Drive> drives;
	std::string message;
	bool error = false;
};

HWND g_wnd, g_list, g_refresh, g_status;
std::vector<Drive> g_drives;
bool g_busy = false;

/* ---------- helpers ---------- */

std::string trim(const char *s)
{
	std::string r(s ? s : "");
	size_t b = r.find_first_not_of(' ');
	size_t e = r.find_last_not_of(' ');
	if (b == std::string::npos) return "";
	return r.substr(b, e - b + 1);
}

/* Create the right DtaDev subclass for a drive, as sedutil-cli does.
 * Returns NULL if the drive is missing or not an Opal-family drive. */
DtaDev *openOpal(const char *devref)
{
	DtaDevGeneric *temp = new DtaDevGeneric(devref);
	DtaDev *d = NULL;
	if (temp->isPresent() && temp->isAnySSC()) {
		if (temp->isRuby1()) d = new DtaDevRuby1(devref);
		else if (temp->isOpal2()) d = new DtaDevOpal2(devref);
		else if (temp->isOpalite()) d = new DtaDevOpalite(devref);
		else if (temp->isPyrite2()) d = new DtaDevPyrite2(devref);
		else if (temp->isPyrite1()) d = new DtaDevPyrite1(devref);
		else if (temp->isOpal1()) d = new DtaDevOpal1(devref);
	}
	delete temp;
	if (d) {
		d->no_hash_passwords = false; // always hash, exactly like the PBA
		d->output_format = sedutilReadable;
	}
	return d;
}

/* Disk numbers a volume lives on. */
std::vector<DWORD> volumeDisks(HANDLE hVol)
{
	std::vector<DWORD> disks;
	std::vector<char> buf(sizeof(VOLUME_DISK_EXTENTS) + 32 * sizeof(DISK_EXTENT));
	DWORD ret = 0;
	if (DeviceIoControl(hVol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0,
		buf.data(), (DWORD)buf.size(), &ret, NULL)) {
		VOLUME_DISK_EXTENTS *ext = (VOLUME_DISK_EXTENTS *)buf.data();
		for (DWORD i = 0; i < ext->NumberOfDiskExtents; i++)
			disks.push_back(ext->Extents[i].DiskNumber);
	}
	return disks;
}

/* Disk number(s) holding the Windows folder. */
std::vector<DWORD> systemDisks()
{
	char windir[MAX_PATH] = { 0 };
	if (!GetWindowsDirectoryA(windir, MAX_PATH) || windir[1] != ':') return {};
	char path[8] = "\\\\.\\C:";
	path[4] = windir[0];
	HANDLE h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) return {};
	std::vector<DWORD> disks = volumeDisks(h);
	CloseHandle(h);
	return disks;
}

/* Volume GUID paths ("\\?\Volume{...}") on a disk, without trailing slash. */
std::vector<std::string> volumesOnDisk(int disk)
{
	std::vector<std::string> vols;
	char name[MAX_PATH];
	HANDLE f = FindFirstVolumeA(name, MAX_PATH);
	if (f == INVALID_HANDLE_VALUE) return vols;
	do {
		std::string v(name);
		if (!v.empty() && v.back() == '\\') v.pop_back();
		HANDLE h = CreateFileA(v.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, 0, NULL);
		if (h == INVALID_HANDLE_VALUE) continue;
		for (DWORD n : volumeDisks(h)) {
			if ((int)n == disk) { vols.push_back(v); break; }
		}
		CloseHandle(h);
	} while (FindNextVolumeA(f, name, MAX_PATH));
	FindVolumeClose(f);
	return vols;
}

/* Ask Windows to re-read a disk's partition table. */
void rescanDisk(int disk)
{
	char path[32];
	sprintf_s(path, sizeof(path), "\\\\.\\PhysicalDrive%i", disk);
	HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	DWORD ret = 0;
	DeviceIoControl(h, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &ret, NULL);
	CloseHandle(h);
}

std::string opalError(uint8_t rc)
{
	switch (rc) {
	case OPALSTATUSCODE::NOT_AUTHORIZED:
		return "Wrong password.";
	case OPALSTATUSCODE::AUTHORITY_LOCKED_OUT:
		return "Too many wrong passwords. The drive refuses more attempts until it is powered off and on again.";
	case OPALSTATUSCODE::SP_BUSY:
		return "The drive is busy. Try again in a moment.";
	default: {
		char b[96];
		sprintf_s(b, sizeof(b), "The drive returned error 0x%02x.", (unsigned)rc);
		return b;
	}
	}
}

/* ---------- drive work (runs on the worker thread) ---------- */

std::vector<Drive> scanDrives()
{
	std::vector<Drive> out;
	std::vector<DWORD> sys = systemDisks();
	for (int i = 0; i < MAX_DRIVES; i++) {
		char devref[32];
		sprintf_s(devref, sizeof(devref), "\\\\.\\PhysicalDrive%i", i);
		DtaDevGeneric *g = new DtaDevGeneric(devref);
		if (!g->isPresent()) { delete g; break; }
		Drive d;
		d.number = i;
		d.devref = devref;
		d.model = trim(g->getModelNum());
		d.serial = trim(g->getSerialNum());
		d.firmware = trim(g->getFirmwareRev());
		d.opal = g->isAnySSC() && !g->isEprise();
		d.lockingEnabled = d.opal && g->LockingEnabled();
		d.locked = d.opal && g->Locked();
		for (DWORD n : sys) if ((int)n == i) d.isSystem = true;
		delete g;
		out.push_back(d);
	}
	return out;
}

Result doUnlock(Drive drive, std::vector<char> pw)
{
	Result r;
	DtaDev *d = openOpal(drive.devref.c_str());
	if (!d) {
		r.error = true;
		r.message = "Could not open " + drive.model + " as an Opal drive.";
	}
	else {
		uint8_t rc = 0;
		// A drive with a boot screen (shadow MBR) shows the boot image instead
		// of its data until MBRDone is set, so set it as the PBA does.
		if (d->MBREnabled() && !d->MBRDone())
			rc = d->setMBRDone(1, pw.data());
		if (rc == 0)
			rc = d->setLockingRange(0, OPAL_LOCKINGSTATE::READWRITE, pw.data());
		delete d;
		if (rc) {
			r.error = true;
			r.message = drive.model + ": " + opalError(rc);
		}
		else {
			rescanDisk(drive.number);
			Sleep(1500); // give Windows a moment to find the partitions
			for (const std::string &v : volumesOnDisk(drive.number)) {
				HANDLE h = CreateFileA(v.c_str(), GENERIC_READ | GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
				if (h == INVALID_HANDLE_VALUE) continue;
				DWORD ret = 0;
				DeviceIoControl(h, IOCTL_VOLUME_ONLINE, NULL, 0, NULL, 0, &ret, NULL);
				CloseHandle(h);
			}
			r.message = drive.model + " is unlocked.";
		}
	}
	DtaSecureZero(pw.data(), pw.size());
	r.drives = scanDrives();
	return r;
}

Result doLock(Drive drive, std::vector<char> pw)
{
	Result r;
	if (drive.isSystem) {
		// The UI already blocks this; check again so it can never happen.
		r.error = true;
		r.message = "Windows is running from this drive, so it can't be locked.";
		DtaSecureZero(pw.data(), pw.size());
		r.drives = scanDrives();
		return r;
	}
	// Take every volume on the drive away from Windows first. A volume that
	// can't be locked is in use, and locking the drive under it would lose data.
	std::vector<HANDLE> held;
	bool ok = true;
	for (const std::string &v : volumesOnDisk(drive.number)) {
		HANDLE h = CreateFileA(v.c_str(), GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (h == INVALID_HANDLE_VALUE) { ok = false; break; }
		FlushFileBuffers(h);
		DWORD ret = 0;
		BOOL locked = FALSE;
		for (int attempt = 0; attempt < 5 && !locked; attempt++) {
			locked = DeviceIoControl(h, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &ret, NULL);
			if (!locked) Sleep(300);
		}
		if (!locked) { CloseHandle(h); ok = false; break; }
		held.push_back(h);
	}
	if (!ok) {
		for (HANDLE h : held) {
			DWORD ret = 0;
			DeviceIoControl(h, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &ret, NULL);
			CloseHandle(h);
		}
		r.error = true;
		r.message = "A program is using a volume on " + drive.model +
			". Close any open files or windows on it and try again. Nothing was locked.";
		DtaSecureZero(pw.data(), pw.size());
		r.drives = scanDrives();
		return r;
	}
	for (HANDLE h : held) {
		DWORD ret = 0;
		DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &ret, NULL);
		DeviceIoControl(h, IOCTL_VOLUME_OFFLINE, NULL, 0, NULL, 0, &ret, NULL);
	}

	uint8_t rc = DTAERROR_OBJECT_CREATE_FAILED;
	DtaDev *d = openOpal(drive.devref.c_str());
	bool opened = (d != NULL);
	if (d) {
		rc = d->setLockingRange(0, OPAL_LOCKINGSTATE::LOCKED, pw.data());
		delete d;
	}
	DtaSecureZero(pw.data(), pw.size());

	for (HANDLE h : held) {
		DWORD ret = 0;
		if (rc) // locking failed: give the volumes back to Windows
			DeviceIoControl(h, IOCTL_VOLUME_ONLINE, NULL, 0, NULL, 0, &ret, NULL);
		DeviceIoControl(h, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &ret, NULL);
		CloseHandle(h);
	}
	if (rc) {
		r.error = true;
		r.message = drive.model + ": " + (opened ? opalError(rc) : std::string("could not open the drive.")) +
			" The drive was not locked.";
	}
	else {
		rescanDisk(drive.number);
		r.message = drive.model + " is locked.";
	}
	r.drives = scanDrives();
	return r;
}

/* ---------- pending operations ---------- */

enum class Action { Unlock, Lock };

struct PendingOp {
	Action action;
	Drive drive; // drive as it was when queued; checked again at Apply
};

std::vector<PendingOp> g_queue;

std::string describe(const PendingOp &op)
{
	return std::string(op.action == Action::Unlock ? "Unlock " : "Lock ") +
		op.drive.model + " (Disk " + std::to_string(op.drive.number) +
		", serial " + op.drive.serial + ")";
}

struct ApplyResult {
	std::vector<Drive> drives;
	std::vector<std::string> lines; // one per operation that ran
	bool failed = false;
};

/* Runs the queue in order on the worker thread and stops at the first failure.
 * Each password is wiped as soon as its operation finishes. */
ApplyResult runQueue(std::vector<PendingOp> ops, std::vector<std::vector<char>> pws)
{
	ApplyResult out;
	std::vector<Drive> now = scanDrives();
	for (size_t i = 0; i < ops.size(); i++) {
		const PendingOp &op = ops[i];
		// The disk numbers Windows gives can change when drives are plugged
		// in or out. Only act if the same drive is still at the same number.
		const Drive *cur = NULL;
		for (const Drive &d : now)
			if (d.number == op.drive.number && d.serial == op.drive.serial) cur = &d;
		Result r;
		if (!cur) {
			r.error = true;
			r.message = op.drive.model + " is no longer Disk " +
				std::to_string(op.drive.number) + ". Click Refresh and queue it again.";
		}
		else if (op.action == Action::Unlock) r = doUnlock(*cur, pws[i]);
		else r = doLock(*cur, pws[i]);
		DtaSecureZero(pws[i].data(), pws[i].size());
		out.lines.push_back((r.error ? "FAILED: " : "Done: ") + describe(op) +
			(r.error ? " - " + r.message : ""));
		if (r.error) {
			out.failed = true;
			for (size_t j = i + 1; j < ops.size(); j++) {
				DtaSecureZero(pws[j].data(), pws[j].size());
				out.lines.push_back("Not run: " + describe(ops[j]));
			}
			break;
		}
		now = r.drives;
	}
	out.drives = scanDrives();
	return out;
}

/* ---------- password dialog (built in memory, no resource file) ---------- */

struct PwDialogData {
	std::string prompt;
	std::vector<char> pw;
};

enum { IDC_PW_EDIT = 200, IDC_PW_TEXT };

INT_PTR CALLBACK pwDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_INITDIALOG: {
		SetWindowLongPtr(dlg, DWLP_USER, lp);
		PwDialogData *data = (PwDialogData *)lp;
		SetDlgItemTextA(dlg, IDC_PW_TEXT, data->prompt.c_str());
		SendDlgItemMessage(dlg, IDC_PW_EDIT, EM_LIMITTEXT, 255, 0);
		SetFocus(GetDlgItem(dlg, IDC_PW_EDIT));
		return FALSE;
	}
	case WM_COMMAND:
		if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
			PwDialogData *data = (PwDialogData *)GetWindowLongPtr(dlg, DWLP_USER);
			HWND edit = GetDlgItem(dlg, IDC_PW_EDIT);
			int len = GetWindowTextLengthA(edit);
			if (LOWORD(wp) == IDOK) {
				if (len == 0) { MessageBeep(MB_ICONWARNING); return TRUE; }
				data->pw.assign((size_t)len + 1, 0);
				GetWindowTextA(edit, data->pw.data(), len + 1);
			}
			// Overwrite the edit control's text before the dialog goes away.
			std::string blank((size_t)len, ' ');
			SetWindowTextA(edit, blank.c_str());
			SetWindowTextA(edit, "");
			EndDialog(dlg, LOWORD(wp));
			return TRUE;
		}
		break;
	}
	return FALSE;
}

void addWords(std::vector<WORD> &t, const wchar_t *s)
{
	do t.push_back((WORD)*s); while (*s++);
}

void addItem(std::vector<WORD> &t, DWORD style, short x, short y, short cx, short cy,
	WORD id, WORD cls, const wchar_t *text)
{
	while (t.size() % 2) t.push_back(0); // DWORD align
	DLGITEMTEMPLATE it = {};
	it.style = style | WS_CHILD | WS_VISIBLE;
	it.x = x; it.y = y; it.cx = cx; it.cy = cy; it.id = id;
	const WORD *w = (const WORD *)&it;
	t.insert(t.end(), w, w + sizeof(it) / sizeof(WORD));
	t.push_back(0xFFFF); t.push_back(cls); // predefined class
	addWords(t, text);
	t.push_back(0); // no creation data
}

/* Ask for one password. Returns an empty vector if cancelled. */
std::vector<char> askPassword(HWND owner, const std::string &prompt)
{
	std::vector<WORD> t;
	DLGTEMPLATE dt = {};
	dt.style = DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU;
	dt.cdit = 4;
	dt.cx = 260; dt.cy = 82;
	const WORD *w = (const WORD *)&dt;
	t.insert(t.end(), w, w + sizeof(dt) / sizeof(WORD));
	t.push_back(0); t.push_back(0); // no menu, default class
	addWords(t, L"Drive password");
	t.push_back(9); addWords(t, L"Segoe UI");
	addItem(t, SS_LEFT, 8, 8, 244, 26, IDC_PW_TEXT, 0x0082, L"");
	addItem(t, ES_PASSWORD | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 8, 38, 244, 14, IDC_PW_EDIT, 0x0081, L"");
	addItem(t, BS_DEFPUSHBUTTON | WS_TABSTOP, 146, 60, 50, 14, IDOK, 0x0080, L"OK");
	addItem(t, BS_PUSHBUTTON | WS_TABSTOP, 202, 60, 50, 14, IDCANCEL, 0x0080, L"Cancel");
	PwDialogData data;
	data.prompt = prompt;
	INT_PTR rc = DialogBoxIndirectParamA(GetModuleHandle(NULL), (LPCDLGTEMPLATEA)t.data(),
		owner, pwDlgProc, (LPARAM)&data);
	if (rc != IDOK) { DtaSecureZero(data.pw.data(), data.pw.size()); return {}; }
	return data.pw;
}

/* ---------- main window ---------- */

enum {
	IDC_QUEUE = 300,
	IDC_Q_UNLOCK,
	IDC_Q_LOCK,
	IDC_Q_REMOVE,
	IDC_Q_CLEAR,
	IDC_APPLY,
	IDC_QLABEL,
};

HWND g_queueList, g_qUnlock, g_qLock, g_qRemove, g_qClear, g_apply;

void setStatus(const std::string &s)
{
	SetWindowTextA(g_status, s.c_str());
}

const Drive *selectedDrive()
{
	int i = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
	if (i < 0 || i >= (int)g_drives.size()) return NULL;
	return &g_drives[i];
}

bool queued(const Drive &d)
{
	for (const PendingOp &op : g_queue)
		if (op.drive.number == d.number && op.drive.serial == d.serial) return true;
	return false;
}

void updateButtons()
{
	const Drive *d = selectedDrive();
	bool usable = !g_busy && d && d->opal && d->lockingEnabled && !queued(*d);
	EnableWindow(g_qUnlock, usable && d->locked);
	EnableWindow(g_qLock, usable && !d->locked && !d->isSystem);
	EnableWindow(g_refresh, !g_busy && g_queue.empty());
	bool qsel = ListBox_GetCurSel(g_queueList) != LB_ERR;
	EnableWindow(g_qRemove, !g_busy && qsel);
	EnableWindow(g_qClear, !g_busy && !g_queue.empty());
	EnableWindow(g_apply, !g_busy && !g_queue.empty());
}

void fillQueue()
{
	ListBox_ResetContent(g_queueList);
	for (const PendingOp &op : g_queue)
		ListBox_AddString(g_queueList, describe(op).c_str());
	updateButtons();
}

void fillList()
{
	int keep = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
	ListView_DeleteAllItems(g_list);
	for (size_t i = 0; i < g_drives.size(); i++) {
		const Drive &d = g_drives[i];
		std::string cols[6];
		cols[0] = std::to_string(d.number);
		cols[1] = d.model;
		cols[2] = d.serial;
		cols[3] = d.firmware;
		if (!d.opal) cols[4] = "No Opal";
		else if (!d.lockingEnabled) cols[4] = "Not set up";
		else cols[4] = d.locked ? "Locked" : "Unlocked";
		cols[5] = d.isSystem ? "Windows runs from this drive" : "";
		LVITEMA it = {};
		it.mask = LVIF_TEXT;
		it.iItem = (int)i;
		it.pszText = (LPSTR)cols[0].c_str();
		ListView_InsertItem(g_list, &it);
		for (int c = 1; c < 6; c++)
			ListView_SetItemText(g_list, (int)i, c, (LPSTR)cols[c].c_str());
	}
	if (keep >= 0 && keep < (int)g_drives.size())
		ListView_SetItemState(g_list, keep, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	updateButtons();
}

void setBusy(const char *what)
{
	g_busy = true;
	updateButtons();
	setStatus(what);
	SetCursor(LoadCursor(NULL, IDC_WAIT));
}

/* Posted with a heap-allocated ApplyResult or Result. */
const UINT WM_SCAN_DONE = WM_APP + 2;
const UINT WM_APPLY_DONE = WM_APP + 3;

void startScan()
{
	setBusy("Looking for drives...");
	HWND wnd = g_wnd;
	std::thread([wnd]() {
		Result *r = new Result();
		r->drives = scanDrives();
		r->message = r->drives.empty()
			? "No drives found. Is the app running as administrator?"
			: "Select a drive, queue what you want to do, then click Apply.";
		PostMessage(wnd, WM_SCAN_DONE, 0, (LPARAM)r);
	}).detach();
}

void queueSelected(Action a)
{
	const Drive *d = selectedDrive();
	if (!d || g_busy || queued(*d)) return;
	if (a == Action::Lock && d->isSystem) return; // never queued
	g_queue.push_back({ a, *d });
	fillQueue();
	setStatus("Queued. Nothing changes until you click Apply.");
}

void onApply()
{
	if (g_busy || g_queue.empty()) return;
	std::string summary = "These operations will run in this order:\n\n";
	for (size_t i = 0; i < g_queue.size(); i++)
		summary += std::to_string(i + 1) + ". " + describe(g_queue[i]) + "\n";
	bool anyLock = false;
	for (const PendingOp &op : g_queue) if (op.action == Action::Lock) anyLock = true;
	if (anyLock)
		summary += "\nLocked drives disappear from Windows until unlocked. "
		"Save and close anything open on them first.";
	summary += "\n\nYou'll be asked for each drive's password. Continue?";
	if (MessageBoxA(g_wnd, summary.c_str(), "Apply pending operations",
		MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
		return;

	std::vector<std::vector<char>> pws;
	for (const PendingOp &op : g_queue) {
		std::vector<char> pw = askPassword(g_wnd, "Password for " + describe(op) + ":");
		if (pw.empty()) {
			for (auto &p : pws) DtaSecureZero(p.data(), p.size());
			setStatus("Cancelled. Nothing was changed.");
			return;
		}
		pws.push_back(std::move(pw));
	}
	setBusy("Applying...");
	std::vector<PendingOp> ops = g_queue;
	HWND wnd = g_wnd;
	std::thread([wnd, ops, pws]() mutable {
		ApplyResult *r = new ApplyResult(runQueue(ops, std::move(pws)));
		PostMessage(wnd, WM_APPLY_DONE, 0, (LPARAM)r);
	}).detach();
}

HWND makeButton(HWND wnd, const char *text, int id, HINSTANCE inst)
{
	return CreateWindowExA(0, "BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
		0, 0, 0, 0, wnd, (HMENU)(INT_PTR)id, inst, NULL);
}

void createControls(HWND wnd)
{
	HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	NONCLIENTMETRICSA ncm = {};
	ncm.cbSize = sizeof(ncm);
	if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
		font = CreateFontIndirectA(&ncm.lfMessageFont);
	HINSTANCE inst = GetModuleHandle(NULL);

	g_list = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
		0, 0, 0, 0, wnd, (HMENU)(INT_PTR)IDC_LIST, inst, NULL);
	ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	const char *heads[6] = { "Disk", "Model", "Serial number", "Firmware", "Status", "Note" };
	int widths[6] = { 45, 230, 170, 80, 90, 190 };
	for (int c = 0; c < 6; c++) {
		LVCOLUMNA col = {};
		col.mask = LVCF_TEXT | LVCF_WIDTH;
		col.pszText = (LPSTR)heads[c];
		col.cx = widths[c];
		ListView_InsertColumn(g_list, c, &col);
	}
	g_qUnlock = makeButton(wnd, "Queue &unlock", IDC_Q_UNLOCK, inst);
	g_qLock = makeButton(wnd, "Queue &lock", IDC_Q_LOCK, inst);
	g_refresh = makeButton(wnd, "&Refresh", IDC_REFRESH, inst);
	HWND qlabel = CreateWindowExA(0, "STATIC", "Pending operations (nothing happens until you click Apply):",
		WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, wnd, (HMENU)(INT_PTR)IDC_QLABEL, inst, NULL);
	g_queueList = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
		0, 0, 0, 0, wnd, (HMENU)(INT_PTR)IDC_QUEUE, inst, NULL);
	g_qRemove = makeButton(wnd, "Re&move", IDC_Q_REMOVE, inst);
	g_qClear = makeButton(wnd, "&Clear all", IDC_Q_CLEAR, inst);
	g_apply = makeButton(wnd, "&Apply", IDC_APPLY, inst);
	g_status = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
		0, 0, 0, 0, wnd, (HMENU)(INT_PTR)IDC_STATUS, inst, NULL);
	HWND all[] = { g_list, g_qUnlock, g_qLock, g_refresh, qlabel, g_queueList,
		g_qRemove, g_qClear, g_apply, g_status };
	for (HWND h : all) SendMessage(h, WM_SETFONT, (WPARAM)font, TRUE);
}

void layout(HWND wnd)
{
	RECT rc;
	GetClientRect(wnd, &rc);
	int w = rc.right, h = rc.bottom, m = 10, bh = 26, bw = 110;
	int statusY = h - m - 20;
	int queueH = 90;
	int qBtnY = statusY - m - bh;
	int queueY = qBtnY - m - queueH;
	int qLabelY = queueY - 20;
	int dBtnY = qLabelY - m - bh;
	MoveWindow(g_list, m, m, w - 2 * m, dBtnY - 2 * m, TRUE);
	MoveWindow(g_qUnlock, m, dBtnY, bw, bh, TRUE);
	MoveWindow(g_qLock, m + bw + m, dBtnY, bw, bh, TRUE);
	MoveWindow(g_refresh, w - m - bw, dBtnY, bw, bh, TRUE);
	MoveWindow(GetDlgItem(wnd, IDC_QLABEL), m, qLabelY, w - 2 * m, 18, TRUE);
	MoveWindow(g_queueList, m, queueY, w - 2 * m, queueH, TRUE);
	MoveWindow(g_qRemove, m, qBtnY, bw, bh, TRUE);
	MoveWindow(g_qClear, m + bw + m, qBtnY, bw, bh, TRUE);
	MoveWindow(g_apply, w - m - bw, qBtnY, bw, bh, TRUE);
	MoveWindow(g_status, m, statusY, w - 2 * m, 20, TRUE);
}

LRESULT CALLBACK wndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_CREATE:
		g_wnd = wnd;
		createControls(wnd);
		startScan();
		return 0;
	case WM_SIZE:
		layout(wnd);
		return 0;
	case WM_GETMINMAXINFO:
		((MINMAXINFO *)lp)->ptMinTrackSize.x = 720;
		((MINMAXINFO *)lp)->ptMinTrackSize.y = 420;
		return 0;
	case WM_COMMAND:
		switch (LOWORD(wp)) {
		case IDC_Q_UNLOCK: queueSelected(Action::Unlock); return 0;
		case IDC_Q_LOCK: queueSelected(Action::Lock); return 0;
		case IDC_REFRESH: if (!g_busy && g_queue.empty()) startScan(); return 0;
		case IDC_Q_REMOVE: {
			int i = ListBox_GetCurSel(g_queueList);
			if (!g_busy && i >= 0 && i < (int)g_queue.size()) {
				g_queue.erase(g_queue.begin() + i);
				fillQueue();
			}
			return 0;
		}
		case IDC_Q_CLEAR:
			if (!g_busy) { g_queue.clear(); fillQueue(); setStatus("Queue cleared."); }
			return 0;
		case IDC_APPLY: onApply(); return 0;
		case IDC_QUEUE:
			if (HIWORD(wp) == LBN_SELCHANGE) updateButtons();
			return 0;
		}
		break;
	case WM_NOTIFY:
		if (((NMHDR *)lp)->idFrom == (UINT_PTR)IDC_LIST && ((NMHDR *)lp)->code == LVN_ITEMCHANGED)
			updateButtons();
		break;
	case WM_SETCURSOR:
		if (g_busy) { SetCursor(LoadCursor(NULL, IDC_WAIT)); return TRUE; }
		break;
	case WM_SCAN_DONE: {
		Result *r = (Result *)lp;
		g_drives = r->drives;
		g_busy = false;
		fillList();
		setStatus(r->message);
		delete r;
		return 0;
	}
	case WM_APPLY_DONE: {
		ApplyResult *r = (ApplyResult *)lp;
		g_drives = r->drives;
		g_queue.clear();
		g_busy = false;
		fillList();
		fillQueue();
		std::string text;
		for (const std::string &l : r->lines) text += l + "\n";
		setStatus(r->failed ? "Stopped at a failure. See the report." : "All operations finished.");
		MessageBoxA(wnd, text.c_str(), r->failed ? "Stopped at a failure" : "Finished",
			MB_OK | (r->failed ? MB_ICONWARNING : MB_ICONINFORMATION));
		delete r;
		return 0;
	}
	case WM_CLOSE:
		if (g_busy) {
			// Closing mid-operation could leave volumes offline; wait it out.
			setStatus("Please wait for the current operation to finish.");
			return 0;
		}
		if (!g_queue.empty() &&
			MessageBoxA(wnd, "There are pending operations that haven't been applied. Quit anyway?",
				"sedutil", MB_YESNO | MB_ICONQUESTION) != IDYES)
			return 0;
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcA(wnd, msg, wp, lp);
}

} // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show)
{
	// Silence the drive code's console logging; this app has no console.
	CLog::Level() = CLog::FromInt(0);
	RCLog::Level() = RCLog::FromInt(0);

	INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
	InitCommonControlsEx(&icc);

	WNDCLASSA wc = {};
	wc.lpfnWndProc = wndProc;
	wc.hInstance = inst;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wc.hIcon = LoadIcon(NULL, IDI_SHIELD);
	wc.lpszClassName = "SedutilGui";
	RegisterClassA(&wc);

	HWND wnd = CreateWindowExA(0, wc.lpszClassName, "sedutil - Opal drives",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 420,
		NULL, NULL, inst, NULL);
	ShowWindow(wnd, show);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		if (!IsDialogMessage(wnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return (int)msg.wParam;
}
