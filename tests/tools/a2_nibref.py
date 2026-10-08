#!/usr/bin/env python3
"""Independent Apple II 5.25" nibble/WOZ reference for rdedisktool tests.

Written from DOS 3.3 RWTS semantics and the WOZ 1/2 references, not from the
C++ sources. The 6-and-2 model reproduces the data-field nibbles of a real
Applesauce capture byte for byte (544/544 sectors) and of a disk written by
DOS 3.3 INIT under sa2 (558/560; the two others differ only in bits that no
reader uses).

Subcommands (exit 0 = pass, 1 = check failed, 2 = usage/IO error):
  make-dsk <out> <seed>                  random 143,360-byte DOS-order image
  make-id-dsk <out>                      every sector filled with its (track, sector) id
  check-woz <woz> <expected.dsk> [--standard]
  check-nib <nib> <expected.dsk> [--standard]
  make-woz <kind> <src.dsk> <out>        kinds: standard, reallike, sync8, woz1,
                                         flux, bad-bitcount, bad-block, bad-tmap
  make-nib <kind> <src.dsk> <out>        kinds: standard, rotated, nb2, dataepi, addrepi, fixedbit
  sparse-woz <src.woz> <out> <tracks>    keep only the listed whole tracks (e.g. 0-2,17)
  woz-info <woz>                         print INFO/TMAP/TRK summary as key=value
  order-check <kind> <dsk> <image>       kind po: verify DO->PO sector mapping
  make-13 <kind> <src.d13> <out>         13-sector (DOS 3.2) image; kinds: nib, nibrot, nb2, woz, woz1,
                                         nibbad (track 5 / sector 3: data epilogue DE AA -> DE AB),
                                         nibmix (track 7 is a 16-sector DOS 3.3 track)
  make-d13-fs <out.d13> <seed> <dir>     synthetic DOS 3.2 disk (VTOC, catalog, 2 files);
                                         writes the expected files and free count into <dir>
  make-dos33-partial <out.do> <seed> <std|mirrored> <dir>
                                         DOS 3.3 disk with partially used tracks (files take
                                         sectors from the top, as DOS does); bitmap in the
                                         standard or the old reversed in-byte order. Writes the
                                         file bodies and the number of in-use sectors the
                                         bitmap shows free (standard reading) into <dir>
  dos33-check <before.do> <after.do> [<new-file>]
                                         after an add (new file given): new sectors were free
                                         and unused before, nothing else changed in the bitmap
                                         except in-use sectors now marked used, no sector is
                                         shared, every in-use sector is marked used. Without
                                         <new-file>: the two bitmaps must be byte-identical
"""
import random
import struct
import sys
import zlib

# DOS 3.3 6-and-2 write translate table
WT = [0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
      0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
      0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
      0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF]
RT = {v: i for i, v in enumerate(WT)}

# logical -> physical sector (RWTS INTRLEAV; ProDOS)
DOS_L2P = [0x0, 0xD, 0xB, 0x9, 0x7, 0x5, 0x3, 0x1, 0xE, 0xC, 0xA, 0x8, 0x6, 0x4, 0x2, 0xF]
PRODOS_L2P = [0x0, 0x2, 0x4, 0x6, 0x8, 0xA, 0xC, 0xE, 0x1, 0x3, 0x5, 0x7, 0x9, 0xB, 0xD, 0xF]
DOS_P2L = [DOS_L2P.index(p) for p in range(16)]

GAP1, GAP2, GAP3 = 85, 6, 11
STANDARD_BITS = 16 * (14 * 8 + 349 * 8) + 16 * (GAP2 + GAP3) * 10 + GAP1 * 10   # 50,034
STANDARD_SYNC_UNITS = 16 * (GAP2 + GAP3) + GAP1                                  # 357


def swap2(b):
    return ((b & 1) << 1) | ((b >> 1) & 1)


def encode62(data):
    """DOS 3.3 RWTS PRENIB16 + WRITE16 for one 256-byte sector -> 343 nibbles."""
    assert len(data) == 256
    nbuf2 = [0] * 86
    for x in range(86):
        nbuf2[x] = (swap2(data[(1 - x) & 255]) << 4) | (swap2(data[171 - x]) << 2) | swap2(data[85 - x])
    seq = [nbuf2[x] for x in range(85, -1, -1)] + [d >> 2 for d in data]
    out, prev = [], 0
    for v in seq:
        out.append(WT[v ^ prev])
        prev = v
    out.append(WT[prev])
    return bytes(out)


def decode62(nibs):
    """343 nibbles -> (256 bytes, checksum_ok). KeyError on an invalid nibble."""
    seq, prev = [], 0
    for i in range(342):
        prev = RT[nibs[i]] ^ prev
        seq.append(prev)
    ok = RT[nibs[342]] == prev
    nbuf2 = [0] * 86
    for k in range(86):
        nbuf2[85 - k] = seq[k]
    out = []
    for y in range(256):
        if y <= 85:
            x, sh = 85 - y, 0
        elif y <= 171:
            x, sh = 171 - y, 2
        else:
            x, sh = (1 - y) & 255, 4
        out.append(((seq[86 + y] << 2) | swap2((nbuf2[x] >> sh) & 3)) & 0xFF)
    return bytes(out), ok


def enc44(v):
    return bytes([(v >> 1) | 0xAA, v | 0xAA])


def dec44(a, b):
    return ((a << 1) | 1) & b


def sector(dsk, t, logical):
    o = (t * 16 + logical) * 256
    return dsk[o:o + 256]


# ---------------------------------------------------------------- 5-and-3 (DOS 3.2, 13 sectors)
# The bit layout was derived from a real DOS 3.2.1 disk (.nib and its .d13:
# every one of the 2048 data bits matched exactly one value bit) and then
# confirmed on a different real capture (DOS 3.2 System Master .woz: 455/455
# checksums, sensible catalog). Translate table = the 32 nibbles seen on disk.
WT53 = [0xAB, 0xAD, 0xAE, 0xAF, 0xB5, 0xB6, 0xB7, 0xBA, 0xBB, 0xBD, 0xBE, 0xBF, 0xD6, 0xD7, 0xDA, 0xDB,
        0xDD, 0xDE, 0xDF, 0xEA, 0xEB, 0xED, 0xEE, 0xEF, 0xF5, 0xF6, 0xF7, 0xFA, 0xFB, 0xFD, 0xFE, 0xFF]
RT53 = {v: i for i, v in enumerate(WT53)}


def map53():
    """(value j, bit k) -> (data byte, bit) or None (always 0); 410 values x 5 bits."""
    m = {(0, 0): (255, 0), (0, 1): (255, 1), (0, 2): (255, 2), (0, 3): None, (0, 4): None}
    for third in range(3):
        for g in range(1, 52):
            j = third * 51 + g
            m[(j, 0)] = (5 * g - 1, third)
            m[(j, 1)] = (5 * g - 2, third)
            for k in range(3):
                m[(j, 2 + k)] = (5 * g - 3 - third, k)
    for r in range(5):
        for i in range(51):
            for k in range(5):
                m[(154 + 51 * r + i, k)] = (250 + r - 5 * i, 3 + k)
    for k in range(5):
        m[(409, k)] = (255, 3 + k)
    return m


MAP53 = map53()


def encode53(data):
    assert len(data) == 256
    vals = [0] * 410
    for (j, k), src in MAP53.items():
        if src and (data[src[0]] >> src[1]) & 1:
            vals[j] |= 1 << k
    out, prev = [], 0
    for v in vals:
        out.append(WT53[v ^ prev])
        prev = v
    out.append(WT53[prev])
    return bytes(out)


def decode53(nibs):
    """411 nibbles -> (256 bytes, checksum_ok). KeyError on an invalid nibble."""
    vals, prev = [], 0
    for i in range(410):
        prev = RT53[nibs[i]] ^ prev
        vals.append(prev)
    ok = RT53[nibs[410]] == prev
    out = bytearray(256)
    for (j, k), dst in MAP53.items():
        if dst and (vals[j] >> k) & 1:
            out[dst[0]] |= 1 << dst[1]
    return bytes(out), ok


def track_units13(d13, t, vol=254, gap1=89, gap2=6, gap3=27, sync_bits=10):
    """13-sector DOS 3.2 track (physical order 0..12 = .d13 order)."""
    units = [(0xFF, sync_bits)] * gap1
    for p in range(13):
        units += [(x, 8) for x in b'\xd5\xaa\xb5' + enc44(vol) + enc44(t) + enc44(p) + enc44(vol ^ t ^ p) + b'\xde\xaa\xeb']
        units += [(0xFF, sync_bits)] * gap2
        units += [(x, 8) for x in b'\xd5\xaa\xad' + encode53(d13[(t * 13 + p) * 256:(t * 13 + p + 1) * 256]) + b'\xde\xaa\xeb']
        units += [(0xFF, sync_bits)] * gap3
    return units


# Measured on the Applesauce capture "DOS 3.2 System Master.woz" (35 tracks
# alike): 16 nine-bit syncs, then per sector in this physical order address
# field + DE AA EB, 14 syncs, data field + DE AA EB, 28 syncs; 49,882 bits.
ORDER13_REAL = [0, 10, 7, 4, 1, 11, 8, 5, 2, 12, 9, 6, 3]


def track_units13_real(d13, t, vol=254, gap1=16, sync_bits=9):
    """13-sector track laid out like real DOS 3.2 writes it (see ORDER13_REAL)."""
    units = [(0xFF, sync_bits)] * gap1
    for p in ORDER13_REAL:
        units += [(x, 8) for x in b'\xd5\xaa\xb5' + enc44(vol) + enc44(t) + enc44(p) + enc44(vol ^ t ^ p) + b'\xde\xaa\xeb']
        units += [(0xFF, sync_bits)] * 14
        units += [(x, 8) for x in b'\xd5\xaa\xad' + encode53(d13[(t * 13 + p) * 256:(t * 13 + p + 1) * 256]) + b'\xde\xaa\xeb']
        units += [(0xFF, sync_bits)] * 28
    return units


def parse_stream13(nibs, track):
    """{physical: dict(vol, data, raw, dp)} for DOS 3.2 sectors, same validity rules as 16-sector
    (dp = index of the data prologue D5 of D5 AA AD)."""
    res, i, n = {}, 0, len(nibs)
    while i + 14 <= n:
        if nibs[i:i + 3] != [0xD5, 0xAA, 0xB5] or nibs[i + 11:i + 13] != [0xDE, 0xAA]:
            i += 1
            continue
        a = nibs[i + 3:i + 11]
        vol, trk, sec, cs = dec44(a[0], a[1]), dec44(a[2], a[3]), dec44(a[4], a[5]), dec44(a[6], a[7])
        if vol ^ trk ^ sec != cs or trk != track or sec > 12:
            i += 1
            continue
        j, dp = i + 13, -1
        while j + 3 <= n and j < i + 13 + 32:
            if nibs[j] == 0xD5 and nibs[j + 1] == 0xAA:
                dp = j if nibs[j + 2] == 0xAD else -1
                break
            j += 1
        if dp < 0 or dp + 3 + 411 + 2 > n or nibs[dp + 414:dp + 416] != [0xDE, 0xAA]:
            i += 1
            continue
        try:
            data, ok = decode53(nibs[dp + 3:dp + 414])
        except KeyError:
            i += 1
            continue
        if ok and sec not in res:
            res[sec] = dict(vol=vol, data=data, raw=bytes(nibs[dp + 3:dp + 414]), dp=dp)
        i = dp + 416 if ok else i + 1
    return res


# ---------------------------------------------------------------- track model
def make_13(kind, d13, out):
    """13-sector NIB/NB2/WOZ of a .d13 image (sectors in physical order)."""
    assert len(d13) == 35 * 13 * 256
    if kind in ('nib', 'nibrot', 'nb2', 'nibbad', 'nibmix'):
        size = 6384 if kind == 'nb2' else 6656
        rnd = random.Random(13)
        res = bytearray()
        for t in range(35):
            tr = bytearray(v for v, _ in track_units13(d13, t))
            tr += b'\xff' * (size - len(tr))
            if kind == 'nibmix' and t == 7:
                r16 = random.Random(16)
                dsk = bytes(r16.randrange(256) for _ in range(143360))
                tr = bytearray(v for v, _ in track_units(dsk, t, gap1=size - 16 * 380))
            if kind == 'nibbad' and t == 5:
                hdr = bytes([0xD5, 0xAA, 0xB5]) + enc44(254) + enc44(5) + enc44(3)
                a = bytes(tr).index(hdr)
                d = bytes(tr).index(bytes([0xD5, 0xAA, 0xAD]), a)
                assert tr[d + 414:d + 416] == b'\xde\xaa'
                tr[d + 415] = 0xAB
            if kind == 'nibrot':
                r = rnd.randrange(1, size)
                tr = tr[r:] + tr[:r]
            res += tr
        open(out, 'wb').write(res)
        return 0
    if kind in ('realnib', 'realnb2'):
        size = 6384 if kind == 'realnb2' else 6656
        res = bytearray()
        for t in range(35):
            body = bytes(v for v, _ in track_units13_real(d13, t, gap1=0))
            res += b'\xff' * (size - len(body)) + body          # gap 1 takes what is left
        open(out, 'wb').write(res)
        return 0
    if kind == 'realwoz':
        info = bytearray(info_chunk(largest=13))
        info[38] = 2
        open(out, 'wb').write(woz2([units_to_bits(track_units13_real(d13, t)) for t in range(35)],
                                   standard_tmap(35), info=bytes(info)))
        return 0
    tracks = [units_to_bits(track_units13(d13, t)) for t in range(35)]
    if kind == 'woz':
        info = bytearray(info_chunk(largest=13))
        info[38] = 2                                   # boot sector format: 13-sector
        open(out, 'wb').write(woz2(tracks, standard_tmap(35), info=bytes(info)))
    elif kind == 'woz1':
        open(out, 'wb').write(woz1(tracks, standard_tmap(35)))
    else:
        return 2
    return 0


def make_d13_fs(out, seed, expdir):
    """DOS 3.2 layout: VTOC T17/S0, catalog T17/S12..1, 13 sectors per track.
    Free-sector bitmap: bytes 0-1 = big-endian word, sector s = bit s+3."""
    rnd = random.Random(seed)
    img = bytearray(rnd.randrange(256) for _ in range(35 * 13 * 256))
    put = lambda t, s, b: img.__setitem__(slice((t * 13 + s) * 256, (t * 13 + s + 1) * 256), b.ljust(256, b'\0'))
    used = {(t, s) for t in (0, 1, 2, 17) for s in range(13)}
    files = []
    # B file: 4-byte header (address, length) + body
    bbody = bytes(rnd.randrange(256) for _ in range(700))
    braw = struct.pack('<HH', 0x0803, len(bbody)) + bbody
    files.append((b'HELLO', 0x04, braw, 18))
    # T file: text up to the first $00
    tbody = b''.join(bytes([c | 0x80]) for c in b'TEXT LINE 1\rTEXT LINE 2\r')
    files.append((b'NOTES', 0x00, tbody + b'\0', 19))
    cat = [bytearray(256) for _ in range(13)]
    for s in range(12, 0, -1):                         # catalog chain 12 -> 1
        if s > 1:
            cat[s][1], cat[s][2] = 17, s - 1
    for n, (name, typ, raw, tr) in enumerate(files):
        secs = [raw[i:i + 256] for i in range(0, len(raw), 256)]
        ts = bytearray(256)
        for i in range(len(secs)):
            put(tr, i, secs[i])
            ts[0x0C + 2 * i], ts[0x0D + 2 * i] = tr, i
            used.add((tr, i))
        put(tr, 12, bytes(ts))
        used.add((tr, 12))
        e = 0x0B + n * 35
        cat[12][e], cat[12][e + 1], cat[12][e + 2] = tr, 12, typ
        cat[12][e + 3:e + 33] = bytes(c | 0x80 for c in name.ljust(30))
        cat[12][e + 33] = len(secs) + 1
    for s in range(1, 13):
        put(17, s, bytes(cat[s]))
    v = bytearray(256)
    v[1], v[2], v[3], v[6], v[0x27] = 17, 12, 2, 254, 122
    v[0x34], v[0x35] = 35, 13
    struct.pack_into('<H', v, 0x36, 256)
    free = 0
    for t in range(35):
        word = 0
        for s in range(13):
            if (t, s) not in used:
                word |= 1 << (s + 3)
                free += t not in (0, 17)
        v[0x38 + 4 * t], v[0x39 + 4 * t] = word >> 8, word & 0xFF
    put(17, 0, bytes(v))
    open(out, 'wb').write(img)
    open(expdir + '/HELLO', 'wb').write(bbody)
    open(expdir + '/NOTES', 'wb').write(tbody)
    open(expdir + '/free', 'w').write(str(free * 256))
    return 0


# ---------------------------------------------------------------- DOS 3.3 VTOC bitmap
# Standard layout (checked against 47 disks written by real DOS 3.3): track
# entry byte 0 bit k = sector 8+k, byte 1 bit k = sector k, 1 = free.
def bm_pos(s, mirrored=False):
    return ((15 - s) // 8, (15 - s) % 8) if mirrored else ((0 if s >= 8 else 1), s % 8)


def dos33_in_use(img):
    """{(t, s): owner} for the VTOC, the catalog chain and every live file."""
    sec = lambda t, s: img[(t * 16 + s) * 256:(t * 16 + s + 1) * 256]
    owners = {(17, 0): ['VTOC']}
    v = sec(17, 0)
    t, s, n = v[1], v[2], 0
    while (t or s) and n < 560:
        n += 1
        owners.setdefault((t, s), []).append('CATALOG')
        c = sec(t, s)
        for e in range(7):
            o = 0x0B + e * 35
            tt, ts = c[o], c[o + 1]
            if (tt == 0 and ts == 0) or tt == 0xFF:
                continue
            name = bytes(x & 0x7F for x in c[o + 3:o + 33]).decode('ascii', 'replace').rstrip()
            m = 0
            while (tt or ts) and m < 560:
                m += 1
                owners.setdefault((tt, ts), []).append(name)
                lst = sec(tt, ts)
                for i in range(0x0C, 256, 2):
                    if lst[i] or lst[i + 1]:
                        owners.setdefault((lst[i], lst[i + 1]), []).append(name)
                tt, ts = lst[1], lst[2]
        t, s = c[1], c[2]
    return owners


def bm_free(img, t, s):
    b, k = bm_pos(s)
    return (img[17 * 16 * 256 + 0x38 + 4 * t + b] >> k) & 1 == 1


def make_dos33_partial(out, seed, mirrored, expdir):
    rnd = random.Random(seed)
    img = bytearray(rnd.randrange(256) for _ in range(143360))

    def put(t, s, b):
        img[(t * 16 + s) * 256:(t * 16 + s + 1) * 256] = b.ljust(256, b'\0')
    used = {(t, s) for t in (0, 1, 2) for s in range(16)} | {(17, s) for s in range(16)}
    # (name, track, sectors): first = T/S list, the rest = data
    files = [(b'ALPHA', 18, [15, 14, 13]), (b'BRAVO', 18, [12, 11, 10, 9]),
             (b'CHARLIE', 20, [15, 14]), (b'DELTA', 25, [15, 14, 13, 12, 11, 10, 9, 8, 7])]
    cat = [bytearray(256) for _ in range(16)]
    for s in range(15, 1, -1):
        cat[s][1], cat[s][2] = 17, s - 1
    for n, (name, t, secs) in enumerate(files):
        body = bytes(rnd.randrange(256) for _ in range((len(secs) - 1) * 256 - 4 - rnd.randrange(1, 200)))
        raw = struct.pack('<HH', 0x2000, len(body)) + body
        ts = bytearray(256)
        for i, s in enumerate(secs[1:]):
            put(t, s, raw[i * 256:(i + 1) * 256])
            ts[0x0C + 2 * i], ts[0x0D + 2 * i] = t, s
        put(t, secs[0], bytes(ts))
        used |= {(t, s) for s in secs}
        e = 0x0B + n * 35
        cat[15][e:e + 3] = bytes([t, secs[0], 0x04])
        cat[15][e + 3:e + 33] = bytes(c | 0x80 for c in name.ljust(30))
        cat[15][e + 33] = len(secs)
        open(expdir + '/' + name.decode(), 'wb').write(body)
    for s in range(1, 16):
        put(17, s, bytes(cat[s]))
    v = bytearray(256)
    v[1], v[2], v[3], v[6], v[0x27], v[0x30], v[0x31] = 17, 15, 3, 254, 122, 18, 1
    v[0x34], v[0x35] = 35, 16
    struct.pack_into('<H', v, 0x36, 256)
    for t in range(35):
        for s in range(16):
            if (t, s) not in used:
                b, k = bm_pos(s, mirrored)
                v[0x38 + 4 * t + b] |= 1 << k
    put(17, 0, bytes(v))
    open(out, 'wb').write(img)
    referenced = set(dos33_in_use(img))
    shown_free = sum(1 for (t, s) in referenced if bm_free(img, t, s))
    open(expdir + '/shown_free', 'w').write(str(shown_free))
    return 0


def dos33_check(before_p, after_p, new=None):
    a, b = open(before_p, 'rb').read(), open(after_p, 'rb').read()
    va, vb = a[17 * 16 * 256:17 * 16 * 256 + 256], b[17 * 16 * 256:17 * 16 * 256 + 256]
    ua, ub = dos33_in_use(a), dos33_in_use(b)
    errs = []
    if new is None:
        if va[0x38:0x38 + 4 * 35] != vb[0x38:0x38 + 4 * 35]:
            errs.append('bitmaps differ')
    else:
        mine = {k for k, o in ub.items() if new in o}
        if not mine:
            errs.append('new file not found')
        if mine & set(ua):
            errs.append('new file uses sectors in use before: %s' % sorted(mine & set(ua)))
        if any(not bm_free(a, t, s) for (t, s) in mine):
            errs.append('new file uses sectors marked used before')
        for t in range(35):
            for s in range(16):
                fa, fb = bm_free(a, t, s), bm_free(b, t, s)
                if fa == fb:
                    continue
                if not (fa and not fb and ((t, s) in mine or (t, s) in ua)):
                    errs.append('bitmap T%d S%d changed %d->%d' % (t, s, fa, fb))
        for (t, s), o in ua.items():
            if (t, s) != (17, 0) and 'CATALOG' not in o and \
                    a[(t * 16 + s) * 256:(t * 16 + s + 1) * 256] != b[(t * 16 + s) * 256:(t * 16 + s + 1) * 256]:
                errs.append('sector T%d S%d of %s changed' % (t, s, o))
    shared = {k: o for k, o in ub.items() if len(o) > 1}
    if shared:
        errs.append('shared sectors: %s' % shared)
    free_in_use = sorted(k for k in ub if bm_free(b, *k))
    if free_in_use:
        errs.append('in-use sectors marked free: %s' % free_in_use)
    for e in errs:
        print('dos33-check:', e)
    return 1 if errs else 0


def track_units(dsk, t, vol=254, gap1=GAP1, gap2=GAP2, gap3=GAP3, sync_bits=10,
                addr_epilogue=b'\xde\xaa\xeb', gap3_bits=None):
    """List of (nibble, bit_length) for a standard DOS 3.3 track (physical order 0..15)."""
    units = [(0xFF, sync_bits)] * gap1
    for p in range(16):
        units += [(x, 8) for x in b'\xd5\xaa\x96' + enc44(vol) + enc44(t) + enc44(p) + enc44(vol ^ t ^ p)]
        units += [(x, 8) for x in addr_epilogue]
        units += [(0xFF, sync_bits)] * gap2
        units += [(x, 8) for x in b'\xd5\xaa\xad' + encode62(sector(dsk, t, DOS_P2L[p])) + b'\xde\xaa\xeb']
        if gap3_bits:
            units += [(0xFF, gap3_bits[i % len(gap3_bits)]) for i in range(gap3)]
        else:
            units += [(0xFF, sync_bits)] * gap3
    return units


def units_to_bits(units):
    bits = []
    for v, n in units:
        bits += [(v >> (7 - i)) & 1 for i in range(8)] + [0] * (n - 8)
    return bits


def pack(bits):
    out = bytearray((len(bits) + 7) // 8)
    for i, b in enumerate(bits):
        if b:
            out[i >> 3] |= 0x80 >> (i & 7)
    return bytes(out)


def unpack(raw, nbits):
    return [(raw[i >> 3] >> (7 - (i & 7))) & 1 for i in range(nbits)]


def lss(bits, revs):
    """Idealised Disk II latch: list of (nibble, index_of_last_bit) over `revs` revolutions."""
    n, reg, out = len(bits), 0, []
    for k in range(n * revs):
        reg = ((reg << 1) | bits[k % n]) & 0xFF
        if reg & 0x80:
            out.append((reg, k))
            reg = 0
    return out


def parse_stream(nibs, track):
    """Return {physical: dict(vol, data, ok, at, data_at)} using RWTS-like validity rules."""
    res, i, n = {}, 0, len(nibs)
    while i + 14 <= n:
        if nibs[i:i + 3] != [0xD5, 0xAA, 0x96]:
            i += 1
            continue
        a = nibs[i + 3:i + 11]
        if nibs[i + 11:i + 13] != [0xDE, 0xAA]:
            i += 1
            continue
        vol, trk, sec, cs = dec44(a[0], a[1]), dec44(a[2], a[3]), dec44(a[4], a[5]), dec44(a[6], a[7])
        if vol ^ trk ^ sec != cs or trk != track or sec > 15:
            i += 1
            continue
        j, dp = i + 13, -1
        while j + 3 <= n and j < i + 13 + 32:
            if nibs[j] == 0xD5 and nibs[j + 1] == 0xAA:
                dp = j if nibs[j + 2] == 0xAD else -1
                break
            j += 1
        if dp < 0 or dp + 3 + 343 + 2 > n or nibs[dp + 346:dp + 348] != [0xDE, 0xAA]:
            i += 1
            continue
        try:
            data, ok = decode62(nibs[dp + 3:dp + 346])
        except KeyError:
            i += 1
            continue
        if ok and sec not in res:
            res[sec] = dict(vol=vol, data=data, raw=bytes(nibs[dp + 3:dp + 346]), at=i)
        i = dp + 348 if ok else i + 1
    return res


# ---------------------------------------------------------------- WOZ files
def chunk(cid, body):
    return cid + struct.pack('<I', len(body)) + body


def info_chunk(version=2, creator=b'a2_nibref', largest=13, wp=0, flux_block=0, largest_flux=0):
    b = bytearray(60)
    b[0], b[1], b[2], b[3], b[4] = version, 1, wp, 0, 1
    b[5:37] = creator.ljust(32, b' ')
    if version >= 2:
        b[37], b[38], b[39] = 1, 1, 32
        struct.pack_into('<HHH', b, 40, 0, 0, largest)
    if version >= 3:
        struct.pack_into('<HH', b, 46, flux_block, largest_flux)
    return bytes(b)


def standard_tmap(ntracks):
    tm = bytearray([0xFF] * 160)
    for t in range(ntracks):
        for q in (4 * t - 1, 4 * t, 4 * t + 1):
            if 0 <= q < 160:
                tm[q] = t
    return bytes(tm)


def woz2(track_bits, tmap, info=None, extra=b''):
    """track_bits: list of bit lists (index = TRKS entry)."""
    entries, data, block = bytearray(1280), bytearray(), 3
    largest = 0
    for i, bits in enumerate(track_bits):
        if bits is None:
            continue
        raw = pack(bits)
        nblk = (len(raw) + 511) // 512
        largest = max(largest, nblk)
        struct.pack_into('<HHI', entries, i * 8, block, nblk, len(bits))
        data += raw.ljust(nblk * 512, b'\0')
        block += nblk
    body = chunk(b'INFO', info or info_chunk(largest=largest)) + chunk(b'TMAP', tmap) + \
        chunk(b'TRKS', bytes(entries) + bytes(data)) + extra
    return b'WOZ2\xff\x0a\x0d\x0a' + struct.pack('<I', zlib.crc32(body) & 0xFFFFFFFF) + body


def woz1(track_bits, tmap, splice=0x1234):
    recs = bytearray()
    for bits in track_bits:
        raw = pack(bits)
        rec = bytearray(6656)
        rec[:len(raw)] = raw
        struct.pack_into('<HHHBBH', rec, 6646, len(raw), len(bits), splice, 0xFF, 10, 0)
        recs += rec
    body = chunk(b'INFO', info_chunk(version=1)) + chunk(b'TMAP', tmap) + chunk(b'TRKS', bytes(recs))
    return b'WOZ1\xff\x0a\x0d\x0a' + struct.pack('<I', zlib.crc32(body) & 0xFFFFFFFF) + body


def read_woz(path):
    d = open(path, 'rb').read()
    if d[:4] not in (b'WOZ1', b'WOZ2') or d[4:8] != b'\xff\x0a\x0d\x0a':
        raise ValueError('not a WOZ file')
    w = dict(ver=d[3] - 0x30, raw=d, crc_ok=struct.unpack('<I', d[8:12])[0] == (zlib.crc32(d[12:]) & 0xFFFFFFFF))
    pos, chunks = 12, []
    while pos + 8 <= len(d):
        cid, sz = d[pos:pos + 4], struct.unpack('<I', d[pos + 4:pos + 8])[0]
        chunks.append((cid, pos + 8, d[pos + 8:pos + 8 + sz]))
        pos += 8 + sz
    w['chunks'] = chunks
    get = {c[0]: c for c in chunks}
    w['info'] = get[b'INFO'][2]
    w['tmap'] = list(get[b'TMAP'][2][:160])
    w['trks_off'] = get[b'TRKS'][1]
    trks = get[b'TRKS'][2]
    w['tracks'] = {}
    if w['ver'] == 2:
        for i in range(160):
            sb, bc, nb = struct.unpack('<HHI', trks[i * 8:i * 8 + 8])
            if sb or bc:
                w['tracks'][i] = dict(bits=nb, start=sb, blocks=bc, raw=d[sb * 512:(sb + bc) * 512])
    else:
        for i in range(len(trks) // 6656):
            rec = trks[i * 6656:(i + 1) * 6656]
            used, nb, sp, sn, sbc = struct.unpack('<HHHBB', rec[6646:6654])
            w['tracks'][i] = dict(bits=nb, used=used, splice=sp, raw=rec[:6646])
    return w


def flux_bits(raw, timing=32):
    """WOZ 2.1 FLUX bytes -> bits: ticks (125 ns) per transition, 255 adds to
    the next byte; n = ticks / timing rounded half up (>= 1): n-1 zeros, a one."""
    bits, acc = [], 0
    for b in raw:
        acc += b
        if b == 255:
            continue
        n = max(1, (2 * acc + timing) // (2 * timing))
        bits += [0] * (n - 1) + [1]
        acc = 0
    return bits


def flux_decode(path, out):
    """16-sector WOZ with FLUX and/or bit tracks -> DOS-order sectors (all must read)."""
    w = read_woz(path)
    d, info = w['raw'], w['info']
    timing = info[39] or 32
    get = {c[0]: c for c in w['chunks']}
    fm = list(get[b'FLUX'][2][:160]) if b'FLUX' in get and info[0] >= 3 else [0xFF] * 160
    trks = get[b'TRKS'][2]
    res = bytearray(35 * 16 * 256)
    for t in range(35):
        if fm[4 * t] != 0xFF:
            sb, bc, nb = struct.unpack('<HHI', trks[fm[4 * t] * 8:fm[4 * t] * 8 + 8])
            bits = flux_bits(d[sb * 512:sb * 512 + nb], timing)
        else:
            bits = woz_track(w, t)
        f = parse_stream([v for v, _ in lss(bits, 2)], t)
        if len(f) != 16:
            print('track %d: %d sectors' % (t, len(f)))
            return 1
        for p, s in f.items():
            L = DOS_P2L[p]
            res[(t * 16 + L) * 256:(t * 16 + L + 1) * 256] = s['data']
    open(out, 'wb').write(res)
    return 0


def woz_track(w, t):
    idx = w['tmap'][4 * t]
    if idx == 0xFF or idx not in w['tracks']:
        return None
    tr = w['tracks'][idx]
    return unpack(tr['raw'], tr['bits'])


def sync_units(bits):
    """Self-sync units in one revolution: an $FF nibble followed by exactly two 0 bits."""
    n = len(bits)
    nl = lss(bits, 3)
    count = 0
    for (v, k), (_, k2) in zip(nl, nl[1:]):
        # nibble ends inside revolution 2; its successor may lie in revolution 3
        if n <= k < 2 * n and v == 0xFF and k2 - k == 10:
            count += 1
    return count


def phys_order(bits, track):
    n = len(bits)
    nl = lss(bits, 3)
    nibs = [v for v, _ in nl]
    order = []
    for i in range(len(nl) - 14):
        if n <= nl[i][1] < 2 * n and nibs[i:i + 3] == [0xD5, 0xAA, 0x96]:
            order.append(dec44(nibs[i + 7], nibs[i + 8]))
    return order


# ---------------------------------------------------------------- checks
class Check:
    def __init__(self):
        self.fails = []
        self.count = 0

    def ok(self, cond, msg):
        self.count += 1
        if not cond:
            self.fails.append(msg)

    def finish(self, label):
        print(f'{label}: {self.count - len(self.fails)}/{self.count} checks passed')
        for f in self.fails[:20]:
            print('  FAIL', f)
        return 0 if not self.fails else 1


def check_woz(path, dsk_path, standard):
    c = Check()
    w = read_woz(path)
    dsk = open(dsk_path, 'rb').read()
    c.ok(w['crc_ok'], 'CRC32')
    info = w['info']
    if standard:
        c.ok(w['ver'] == 2 and info[0] == 2, 'WOZ2 / INFO v2')
        c.ok(info[1] == 1, 'disk type 5.25')
        cr = info[5:37]
        c.ok(cr.rstrip(b' ') == cr.rstrip(b' \0') and b'\0' not in cr and len(cr.rstrip(b' ')) > 0,
             f'creator space padded: {cr!r}')
        c.ok(struct.unpack('<H', info[40:42])[0] == 0, 'compatible hardware = 0')
        c.ok(info[38] == 1 and info[39] == 32, 'boot format 16-sector, timing 32')
        c.ok(w['tmap'] == list(standard_tmap(35)), 'TMAP: 4t-1, 4t, 4t+1 -> t')
        c.ok(w['trks_off'] == 256, 'TRKS data at offset 256')
        largest = max(tr['blocks'] for tr in w['tracks'].values())
        c.ok(struct.unpack('<H', info[44:46])[0] == largest, 'largest track blocks')
        c.ok(all(tr['start'] >= 3 for tr in w['tracks'].values()), 'BITS start at block >= 3')
    for t in range(35):
        bits = woz_track(w, t)
        if bits is None:
            c.ok(False, f'T{t} missing')
            continue
        nibs = [v for v, _ in lss(bits, 2)]
        secs = parse_stream(nibs, t)
        for logical in range(16):
            p = DOS_L2P[logical]
            exp = sector(dsk, t, logical)
            c.ok(p in secs and secs[p]['data'] == exp, f'T{t} L{logical} (P{p}) data')
            if standard and p in secs:
                c.ok(secs[p]['raw'] == encode62(exp), f'T{t} P{p} exact RWTS encoding')
        if standard:
            c.ok(len(bits) == STANDARD_BITS, f'T{t} bit count {len(bits)} == {STANDARD_BITS}')
            su = sync_units(bits)
            c.ok(su == STANDARD_SYNC_UNITS, f'T{t} sync units {su} == {STANDARD_SYNC_UNITS}')
            order = phys_order(bits, t)
            c.ok(order == list(range(16)), f'T{t} physical order {order}')
    return c.finish(f'check-woz {path}')


def check_nib(path, dsk_path, standard):
    c = Check()
    d = open(path, 'rb').read()
    dsk = open(dsk_path, 'rb').read()
    size = {232960: 6656, 223440: 6384}.get(len(d))
    if size is None:
        print('check-nib: bad size', len(d))
        return 1
    for t in range(35):
        tr = list(d[t * size:(t + 1) * size])
        secs = parse_stream(tr + tr, t)
        for logical in range(16):
            p = DOS_L2P[logical]
            exp = sector(dsk, t, logical)
            c.ok(p in secs and secs[p]['data'] == exp, f'T{t} L{logical} (P{p}) data')
            if standard and p in secs:
                c.ok(secs[p]['raw'] == encode62(exp), f'T{t} P{p} exact RWTS encoding')
        if standard:
            order = [dec44(tr[i + 7], tr[i + 8]) for i in range(size - 14) if tr[i:i + 3] == [0xD5, 0xAA, 0x96]]
            c.ok(order == list(range(16)), f'T{t} physical order {order}')
    return c.finish(f'check-nib {path}')


# ---------------------------------------------------------------- fixtures
def make_woz(kind, dsk, out):
    rnd = random.Random(1234)
    tracks = []
    for t in range(35):
        if kind in ('standard', 'woz1', 'flux', 'flux-even', 'bad-bitcount', 'bad-block', 'bad-tmap'):
            bits = units_to_bits(track_units(dsk, t))
        elif kind == 'sync8':
            # 8-bit syncs and a bit count that is not a multiple of 8
            bits = units_to_bits(track_units(dsk, t, sync_bits=8)) + [0, 0]
        elif kind == 'reallike':
            # like a disk written by DOS INIT on a slow drive: long gap3, 9- and
            # 10-bit syncs, address epilogue third byte not $EB, and the
            # bitstream rotated so a sector straddles the index point
            units = track_units(dsk, t, gap1=40, gap3=32, addr_epilogue=b'\xde\xaa\xff',
                                gap3_bits=[10, 10, 9, 10])
            bits = units_to_bits(units)
            bits += [0] * (53248 - len(bits)) if len(bits) < 53248 else []
            r = rnd.randrange(1, len(bits))
            bits = bits[r:] + bits[:r]
        else:
            raise SystemExit(f'unknown kind {kind}')
        tracks.append(bits)
    tmap = standard_tmap(35)
    if kind == 'woz1':
        data = woz1(tracks, tmap)
    elif kind == 'flux-even':
        # Like the Applesauce sample "ProDOS User's Disk": even tracks as FLUX
        # (WOZ 2.1), odd tracks as bits. Flux bytes = ticks (125 ns) between
        # 1 bits: cells * 32 + jitter of up to +-10 ticks; 255 continues into
        # the next byte: on every FLUX track the last gap-2 sync before the
        # first data field carries 12 more zero bits (480 ticks to the D5:
        # 255 + 225), so reading 255 as an interval of its own puts a 1 there
        # and the data prologue is framed wrong
        jr = random.Random(77)
        def to_flux(bits):
            ones = [i for i, b in enumerate(bits) if b]
            out = bytearray()
            for k, pos in enumerate(ones):
                cells = pos - ones[k - 1] if k else pos + len(bits) - ones[-1]
                ticks = cells * 32 + jr.randint(-10, 10)
                while ticks >= 255:
                    out.append(255); ticks -= 255
                out.append(ticks)
            return bytes(out)
        bit_tracks, flux_tracks = [], []
        for t in range(35):
            if t % 2 == 0:
                u = track_units(dsk, t)
                j = next(i for i in range(len(u)) if [v for v, _ in u[i:i + 3]] == [0xD5, 0xAA, 0xAD])
                assert u[j - 1][0] == 0xFF
                u[j - 1] = (0xFF, u[j - 1][1] + 12)
                flux_tracks.append((t, units_to_bits(u)))
            else:
                bit_tracks.append((t, list(tracks[t])))
        tm = [0xFF] * 160
        fl = [0xFF] * 160
        entries, blobs, block = bytearray(1280), bytearray(), 3
        for i, (t, b) in enumerate(bit_tracks + flux_tracks):
            raw = pack(b) if t % 2 else to_flux(b)
            n = (len(raw) + 511) // 512
            struct.pack_into('<HHI', entries, i * 8, block, n, len(b) if t % 2 else len(raw))
            blobs += raw + bytes(n * 512 - len(raw))
            block += n
            for q in (4 * t - 1, 4 * t, 4 * t + 1):
                if 0 <= q < 160:
                    (tm if t % 2 else fl)[q] = i
        trks = bytes(entries) + bytes(blobs)
        largest_flux = max((len(to_flux(b)) + 511) // 512 for _, b in flux_tracks)
        flux_offset = 256 + len(trks)
        assert flux_offset % 512 == 0
        info = info_chunk(version=3, largest=13, flux_block=flux_offset // 512, largest_flux=largest_flux)
        body = chunk(b'INFO', info) + chunk(b'TMAP', bytes(tm)) + chunk(b'TRKS', trks) + chunk(b'FLUX', bytes(fl))
        data = b'WOZ2\xff\x0a\x0d\x0a' + struct.pack('<I', zlib.crc32(body) & 0xFFFFFFFF) + body
    elif kind == 'flux':
        # INFO v3 + FLUX map: quarter track 0 (track 0) is flux entry 35
        flux_bytes = bytes(rnd.randrange(1, 255) for _ in range(512))
        base = woz2(tracks, tmap)
        entries = bytearray(base[256:256 + 1280])
        bit_data = base[1536:]
        struct.pack_into('<HHI', entries, 35 * 8, 3 + len(bit_data) // 512, 1, len(flux_bytes))
        trks = bytes(entries) + bit_data + flux_bytes
        fl = bytearray([0xFF] * 160)
        fl[0] = 35
        flux_offset = 256 + len(trks)                      # FLUX chunk header position
        info = info_chunk(version=3, largest=13, flux_block=flux_offset // 512, largest_flux=1)
        body = chunk(b'INFO', info) + chunk(b'TMAP', tmap) + chunk(b'TRKS', trks) + chunk(b'FLUX', bytes(fl))
        data = b'WOZ2\xff\x0a\x0d\x0a' + struct.pack('<I', zlib.crc32(body) & 0xFFFFFFFF) + body
    else:
        data = bytearray(woz2(tracks, tmap))
        if kind == 'bad-bitcount':
            struct.pack_into('<I', data, 256 + 5 * 8 + 4, 13 * 4096 + 1)   # more bits than blocks
        elif kind == 'bad-block':
            struct.pack_into('<H', data, 256 + 7 * 8, 1)                   # inside the header
        elif kind == 'bad-tmap':
            data[80 + 4 * 3] = 77                                           # unused TRKS entry
        if kind.startswith('bad-'):
            struct.pack_into('<I', data, 8, zlib.crc32(bytes(data[12:])) & 0xFFFFFFFF)
        data = bytes(data)
    open(out, 'wb').write(data)
    return 0


BAD_TRACK, BAD_PHYS = 3, 5     # sector damaged by the dataepi/addrepi/fixedbit kinds


def damage(tr, kind):
    """Damage physical sector BAD_PHYS of one nibble track in place."""
    for i in range(len(tr) - 14):
        if tr[i:i + 3] == b'\xd5\xaa\x96' and dec44(tr[i + 7], tr[i + 8]) == BAD_PHYS:
            if kind == 'addrepi':
                tr[i + 11] = 0xDF                        # DE AA -> DF AA
            elif kind == 'dataepi':
                j = tr.index(b'\xd5\xaa\xad', i)
                tr[j + 3 + 343 + 1] = 0xAB               # DE AA -> DE AB
            elif kind == 'fixedbit':
                # clear a fixed 1 bit (not bit 7) of an even address byte where
                # the decoded value stays the same: DOS 3.3 still reads it
                for k in range(4):
                    odd, even = tr[i + 3 + 2 * k], tr[i + 4 + 2 * k]
                    for bit in (0x20, 0x08, 0x02):
                        ne = even & ~bit & 0xFF
                        if ne != even and dec44(odd, ne) == dec44(odd, even):
                            tr[i + 4 + 2 * k] = ne
                            return
                raise SystemExit('no fixed bit to clear')
            return
    raise SystemExit('sector not found')


def make_nib(kind, dsk, out):
    """kinds: standard, rotated, nb2 (6384-byte tracks), dataepi, addrepi, fixedbit"""
    rnd = random.Random(99)
    size = 6384 if kind == 'nb2' else 6656
    res = bytearray()
    for t in range(35):
        units = track_units(dsk, t, gap1=size - 16 * 380)
        tr = bytearray(v for v, _ in units)
        assert len(tr) == size
        if kind == 'rotated':
            r = rnd.randrange(1, size)
            tr = tr[r:] + tr[:r]
        if kind in ('dataepi', 'addrepi', 'fixedbit') and t == BAD_TRACK:
            damage(tr, kind)
        res += tr
    open(out, 'wb').write(res)
    return 0


def parse_tracks(spec):
    out = set()
    for part in spec.split(','):
        if '-' in part:
            a, b = part.split('-')
            out.update(range(int(a), int(b) + 1))
        elif part:
            out.add(int(part))
    return out


def sparse_woz(src, out, spec):
    w = read_woz(src)
    keep = parse_tracks(spec)
    tracks = []
    for t in range(35):
        tracks.append(woz_track(w, t) if t in keep else None)
    tm = bytearray(standard_tmap(35))
    for q in range(160):
        if tm[q] != 0xFF and tm[q] not in keep:
            tm[q] = 0xFF
    open(out, 'wb').write(woz2(tracks, bytes(tm)))
    return 0


def woz_info(path):
    w = read_woz(path)
    info = w['info']
    print(f"version={w['ver']} info_version={info[0]} crc_ok={int(w['crc_ok'])}")
    print(f"write_protected={info[2]} synchronized={info[3]} cleaned={info[4]}")
    print(f"creator={info[5:37].decode('latin-1')!r}")
    if info[0] >= 2:
        print(f"compat=0x{struct.unpack('<H', info[40:42])[0]:04x} largest={struct.unpack('<H', info[44:46])[0]}")
    print('tmap=' + ','.join('%d' % v if v != 0xFF else '-' for v in w['tmap'][:12]))
    for i in sorted(w['tracks'])[:3]:
        tr = w['tracks'][i]
        extra = f" splice=0x{tr['splice']:04x}" if 'splice' in tr else ''
        print(f"trk{i}_bits={tr['bits']}{extra}")
    print('chunks=' + ','.join(c[0].decode('latin-1') for c in w['chunks']))
    return 0


def order_check(kind, dsk_path, img_path):
    """kind 'po': image slot s of track t must hold the DOS sector on the same physical sector."""
    dsk = open(dsk_path, 'rb').read()
    img = open(img_path, 'rb').read()
    c = Check()
    if kind != 'po':
        raise SystemExit('unknown order-check kind')
    for t in range(35):
        for s in range(16):
            logical = DOS_P2L[PRODOS_L2P[s]]
            c.ok(img[(t * 16 + s) * 256:(t * 16 + s + 1) * 256] == sector(dsk, t, logical),
                 f'T{t} PO slot {s} != DOS sector {logical}')
    return c.finish(f'order-check {img_path}')


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd, args = argv[1], argv[2:]
    if cmd == 'make-dsk':
        rnd = random.Random(int(args[1]))
        open(args[0], 'wb').write(bytes(rnd.randrange(256) for _ in range(143360)))
        return 0
    if cmd == 'make-id-dsk':
        out = bytearray()
        for t in range(35):
            for s in range(16):
                out += bytes([t, s]) * 128
        open(args[0], 'wb').write(out)
        return 0
    if cmd == 'check-woz':
        return check_woz(args[0], args[1], '--standard' in args)
    if cmd == 'check-nib':
        return check_nib(args[0], args[1], '--standard' in args)
    if cmd == 'make-woz':
        return make_woz(args[0], open(args[1], 'rb').read(), args[2])
    if cmd == 'make-nib':
        return make_nib(args[0], open(args[1], 'rb').read(), args[2])
    if cmd == 'sparse-woz':
        return sparse_woz(args[0], args[1], args[2])
    if cmd == 'woz-info':
        return woz_info(args[0])
    if cmd == 'order-check':
        return order_check(args[0], args[1], args[2])
    if cmd == 'flux-decode':
        return flux_decode(args[0], args[1])
    if cmd == 'make-13':
        return make_13(args[0], open(args[1], 'rb').read(), args[2])
    if cmd == 'make-dos33-partial':
        return make_dos33_partial(args[0], int(args[1]), args[2] == 'mirrored', args[3])
    if cmd == 'dos33-check':
        return dos33_check(args[0], args[1], args[2] if len(args) > 2 else None)
    if cmd == 'make-d13-fs':
        return make_d13_fs(args[0], int(args[1]), args[2])
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
