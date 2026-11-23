#!/usr/bin/env python3
"""Boot a sedutil UEFI image in QEMU and check that it works.

    qemu_boot_test.py pba|rescue IMAGE [OVMF_CODE OVMF_VARS]

The image is attached as an NVMe drive (as it is when it sits in a drive's
shadow MBR), next to an empty SATA drive. A USB keyboard is attached and
keys are typed through the QEMU monitor. The PS/2 controller is turned off,
so the keys can only arrive through the USB keyboard driver. Results are
read from the first serial port, where the images write their status lines.

pba:    waits for the password prompt, tries Ctrl+C and Ctrl+\\ (must not
        escape), types a password and Enter, then expects both drives to be
        scanned and the machine to reboot.
rescue: waits for the shell, runs `sedutil-cli --scan`, expects both drives
        listed, then reboots.
QEMU's drives have no Opal support, so "not OPAL" is the expected result.
"""
import os, socket, subprocess, sys, tempfile, time, shutil

KEYS = {
    ' ': 'spc', '-': 'minus', '/': 'slash', '>': 'shift-dot', '.': 'dot',
    ';': 'semicolon', '\n': 'ret', '_': 'shift-minus', '|': 'shift-backslash',
}

def keyname(ch):
    if ch in KEYS:
        return KEYS[ch]
    if ch.isupper():
        return 'shift-' + ch.lower()
    return ch

def main():
    kind, image = sys.argv[1], sys.argv[2]
    code = sys.argv[3] if len(sys.argv) > 3 else '/usr/share/OVMF/OVMF_CODE_4M.fd'
    vars_src = sys.argv[4] if len(sys.argv) > 4 else '/usr/share/OVMF/OVMF_VARS_4M.fd'
    work = tempfile.mkdtemp(prefix='pba-test-')
    vars_fd = os.path.join(work, 'vars.fd')
    shutil.copy(vars_src, vars_fd)
    boot = os.path.join(work, 'boot.img')
    shutil.copy(image, boot)
    sata = os.path.join(work, 'sata.img')
    with open(sata, 'wb') as f:
        f.truncate(64 * 1024 * 1024)
    serial = os.path.join(work, 'serial.log')
    mon = os.path.join(work, 'monitor.sock')
    accel = 'kvm' if os.access('/dev/kvm', os.R_OK | os.W_OK) else 'tcg'
    cmd = [
        'qemu-system-x86_64', '-machine', 'q35,i8042=off,accel=' + accel, '-m', '512',
        '-drive', 'if=pflash,format=raw,readonly=on,file=' + code,
        '-drive', 'if=pflash,format=raw,file=' + vars_fd,
        '-drive', 'if=none,id=nv0,format=raw,file=' + boot,
        '-device', 'nvme,drive=nv0,serial=PBATEST0,bootindex=0',
        '-drive', 'if=none,id=sata0,format=raw,file=' + sata,
        '-device', 'ide-hd,drive=sata0,bus=ide.0',
        '-device', 'qemu-xhci', '-device', 'usb-kbd',
        '-display', 'none', '-serial', 'file:' + serial,
        '-monitor', 'unix:%s,server,nowait' % mon, '-no-reboot',
    ]
    print('+', ' '.join(cmd), flush=True)
    qemu = subprocess.Popen(cmd)

    def log():
        try:
            with open(serial, errors='replace') as f:
                return f.read()
        except FileNotFoundError:
            return ''

    def wait_for(text, timeout):
        end = time.time() + timeout
        while time.time() < end:
            if text in log():
                return True
            if qemu.poll() is not None:
                return text in log()
            time.sleep(1)
        return False

    m = socket.socket(socket.AF_UNIX)
    for _ in range(100):
        if qemu.poll() is not None:
            print('FAIL: QEMU exited at start')
            sys.exit(1)
        try:
            m.connect(mon)
            break
        except OSError:
            time.sleep(0.1)

    def send(keys):
        for k in keys:
            m.sendall(('sendkey %s\n' % k).encode())
            time.sleep(0.08)

    def type_text(text):
        send([keyname(c) for c in text])

    ok = True
    timeout = 180 if accel == 'tcg' else 60
    if not wait_for('sedutil-%s: ready' % kind, timeout):
        print('FAIL: image did not reach its prompt')
        ok = False
    else:
        time.sleep(2)
        if kind == 'pba':
            send(['ctrl-c', 'ctrl-backslash', 'ctrl-z'])
            type_text('notthepassword\n')
            expect = ['/dev/nvme0', '/dev/sda']
        else:
            type_text('sedutil-cli --scan >/dev/ttyS0 2>&1; reboot -f\n')
            expect = ['nvme0', 'sda']
        try:
            qemu.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            print('FAIL: no reboot after input')
            ok = False
        out = log()
        for e in expect:
            if e not in out:
                print('FAIL: %s not found by the scan' % e)
                ok = False
        if kind == 'pba' and out.count('sedutil-pba: ready') != 1:
            print('FAIL: the PBA restarted instead of rebooting')
            ok = False
    if qemu.poll() is None:
        qemu.kill()
    print('---- serial log ----')
    print(log())
    print('PASS' if ok else 'FAILED')
    sys.exit(0 if ok else 1)

if __name__ == '__main__':
    main()
