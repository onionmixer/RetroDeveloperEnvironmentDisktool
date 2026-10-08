# tests/emu — Apple II images checked in a real emulator (sa2 / AppleWin)

Manual checks; not part of the `tests/test_*.sh` run (they need sa2, Xvfb,
xdotool and unprivileged user namespaces, and take a few minutes).

```bash
tests/emu/emu_apple_boot_check.sh     # exit 0 pass, 1 mismatch, 3 not judged
tests/emu/emu_apple_800k_check.sh     # 800K ProDOS on an Apple //c Plus (3.5" drive)
```

It converts `diskwork/bootdisk/AppleII/dos33.dsk` (and `ProDOS_2_4_3.po`) to
WOZ / NIB / NB2 with rdedisktool and checks in an Apple //e Enhanced that each
boots to the same CATALOG / ProDOS screen as the original, and that a DOS
`SAVE` writes the same sectors as on the DSK. A broken encoder (for example the
pre-2026-10 6-and-2 encoder) fails all checks. Check 4 adds a file with
rdedisktool to a blank data disk and lets real DOS `BSAVE` onto the same,
partially used track: with the pre-2026-10 VTOC bit order real DOS overwrote 3
sectors of the added file.

Checks 5-8 need images that are not committed (each is skipped, "not judged",
when missing): the Asimov DOS 3.2 masters in `resource/AppleII/dos32` (DOS32_DIR)
and the Applesauce WOZ 2.1 FLUX sample in `resource/AppleII/woz_flux` (FLUX_WOZ).
They boot the DOS 3.2 System Master and check that real DOS 3.2 reads (BLOAD)
files rdedisktool added to `.d13`, 13-sector NIB and WOZ images - also into
never-written sectors and on a volume made by `create --fs dos32` - and writes
(BSAVE) next to them; that a master converted from `.d13` to WOZ boots; and
that the FLUX sample, converted to `.po`, boots to its menu and runs FILER
(sa2 itself does not read FLUX tracks).

`emu_apple_800k_check.sh` runs an Apple //c Plus (ROM 05 in
`resource/AppleII/rom/iicp_rom05.bin`, user-supplied, never committed) with an
800K image in its built-in 3.5" drive (slot 5, drive 1): A2 DeskTop 1.5 with a
file added by rdedisktool boots from it and writes its settings; ProDOS 2.4.3
(booted from 5.25") lists a volume made by `create -f 800po` / `-f 800mg`,
BASIC `SAVE`s to it and `CAT` shows the expected block counts; the image ProDOS
wrote reads the same with rdedisktool and `tests/tools/a2_prodos_ref.py`, and
the 2MG header is unchanged. It replaces the MAME checks of the 800K plan (S7).

`a2run.sh <image> <boot-seconds> [type:TEXT|key:KEYS|wait:N|mem:ADDR,LEN|shot:NAME ...]`
boots one image and leaves `screen.txt`, the images, memory dumps and
screenshots in a run directory. `A2RUN_MODEL=iicp` runs an Apple //c Plus
(`IIC_ROM`), `A2RUN_DISK35=<800K .po/.2mg>` puts a copy in its 3.5" drive
(left as `d35.*` in the run directory).
sa2's debug server has fixed ports (64501-64505), so sa2 runs in a private
network namespace (`unshare`); the script refuses to start sa2 otherwise.
Xvfb picks its own display; the user's display is never used. Everything it
starts is stopped on exit, Ctrl-C or SIGTERM.
