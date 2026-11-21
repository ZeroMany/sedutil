# sedutil-gui

A small Windows app to see, lock and unlock Opal self-encrypting drives.
It uses the same drive code as `sedutil-cli`, so a password is hashed exactly
as the boot screen (PBA) hashes it, and it never leaves the app.

## How it works

Nothing happens when you click a drive. As in GParted, you queue operations,
review them, then click **Apply**:

1. Select a drive and click **Queue unlock** or **Queue lock**.
2. Review the pending operations. Remove any you don't want.
3. Click **Apply**. You see a summary, then you're asked for each drive's
   password. The operations run in order and stop at the first failure.

Passwords are asked for only at Apply, and are wiped from memory as soon as
each operation finishes.

## Safety checks

- The drive Windows runs from can't be queued for locking.
- Before a drive is locked, every volume on it is locked, dismounted and taken
  offline. If a program is using one, nothing is locked.
- At Apply, each drive must still have the same disk number and serial number
  as when it was queued. This catches drives being plugged in or out.
- Closing the window is blocked while an operation runs, and asks first if
  operations are still pending.
- Only the global locking range (range 0) is changed. That is how sedutil sets
  up drives.

The app needs administrator rights and asks for them when it starts.

## Building

Open `GUI.sln` in Visual Studio 2022 and build Release x64, or run:

    msbuild windows\GUI\GUI.sln /p:Configuration=Release /p:Platform=x64

The app is written to `windows\GUI\x64\Release\sedutil-gui.exe`.
