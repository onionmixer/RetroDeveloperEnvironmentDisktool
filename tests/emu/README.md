# tests/emu — Apple II images checked in a real emulator (sa2 / AppleWin)

Manual checks; not part of the `tests/test_*.sh` run (they need sa2, Xvfb,
xdotool and unprivileged user namespaces, and take a few minutes).

```bash
tests/emu/emu_apple_boot_check.sh     # exit 0 pass, 1 mismatch, 3 not judged
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

`a2run.sh <image> <boot-seconds> [type:TEXT|wait:N|mem:ADDR,LEN ...]` boots one
image and leaves `screen.txt`, the images and memory dumps in a run directory.
sa2's debug server has fixed ports (64501-64505), so sa2 runs in a private
network namespace (`unshare`); the script refuses to start sa2 otherwise.
Xvfb picks its own display; the user's display is never used. Everything it
starts is stopped on exit, Ctrl-C or SIGTERM.
