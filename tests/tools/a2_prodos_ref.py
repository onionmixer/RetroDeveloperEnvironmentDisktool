#!/usr/bin/env python3
"""Independent ProDOS volume reader for rdedisktool tests.

Written from the ProDOS 8 Technical Reference, Appendix B (workspace copy:
resource/AppleII/prodos/technical_reference_manual/Contents_09_File_Organization.md),
not from the C++ sources. Two points of that text contradict each other and were
settled by measurement on a real 800K ProDOS volume (A2 DeskTop 1.5, 139 entries):
  - index blocks: low bytes of the pointers in bytes 0-255, high bytes in 256-511
    (B.3.7). The "0-127 / 128-255" wording of B.3.3/B.3.4 gave 252 shared blocks
    and 35 out-of-range pointers; 0-255/256-511 gave none.
  - seedling/sapling/tree is taken from storage_type only (B.4.2.1 says seedling
    EOF <= 256, B.3.2 says <= 512); nothing here guesses it from EOF.
A zero pointer in an index block is an unallocated (sparse) block and reads as zeros.

Images: raw ProDOS-order blocks (.po) or 2MG (header parsed; only format 1 =
ProDOS order is accepted).

Subcommands (exit 0 = ok, 1 = check failed, 2 = usage / unreadable image):
  ls <image>                 one line per entry: path, storage, type, eof, blocks_used,
                             key, access, aux (tab separated, depth-first, directory order)
  cat <image> <path> <out>   write a standard file's EOF bytes to <out>
  check <image>              structural consistency (see check())
  info <image>               volume header fields and 2MG header fields (key=value)
  add-sparse <image> <name> <kind> <seed> <expected-out>
                             write a sparse file into the root (raw ProDOS-order image):
                             kind hole   = sapling, data blocks 2 and 5 unallocated
                             kind tail   = sapling whose EOF ends inside an unallocated block
                             kind master = tree, EOF 200,000, master index pointer 1 = 0
                             writes the file's expected bytes to <expected-out>
"""
import struct
import sys

BLOCK = 512


class ProDOSError(Exception):
    pass


def load(path):
    """Return (blocks bytes, info dict). 2MG: only format 1 (ProDOS order)."""
    raw = open(path, 'rb').read()
    info = {}
    if raw[:4] == b'2IMG':
        (creator, hsize, ver, fmt, flags, nblocks, doff, dlen,
         coff, clen, xoff, xlen) = struct.unpack_from('<4sHHIIIIIIIII', raw, 4)
        info.update({'2mg.creator': creator.decode('latin-1'), '2mg.header_size': hsize,
                     '2mg.version': ver, '2mg.format': fmt, '2mg.flags': flags,
                     '2mg.blocks': nblocks, '2mg.data_offset': doff, '2mg.data_length': dlen,
                     '2mg.comment_offset': coff, '2mg.comment_length': clen,
                     '2mg.creator_offset': xoff, '2mg.creator_length': xlen})
        if fmt != 1:
            raise ProDOSError('2MG format %d is not ProDOS order' % fmt)
        if doff + dlen > len(raw) or dlen != nblocks * BLOCK:
            raise ProDOSError('2MG data range / block count inconsistent')
        data = raw[doff:doff + dlen]
    else:
        data = raw
    if len(data) % BLOCK:
        raise ProDOSError('image size is not a multiple of 512')
    return data, info


class Volume:
    def __init__(self, data):
        self.d = data
        self.nblocks = len(data) // BLOCK
        h = self.block(2)
        if h[4] >> 4 != 0xF:
            raise ProDOSError('block 2 is not a volume directory key block')
        self.name = h[5:5 + (h[4] & 15)].decode('latin-1')
        self.entry_length = h[0x23]
        self.entries_per_block = h[0x24]
        self.file_count = struct.unpack_from('<H', h, 0x25)[0]
        self.bitmap_pointer = struct.unpack_from('<H', h, 0x27)[0]
        self.total_blocks = struct.unpack_from('<H', h, 0x29)[0]
        if self.entry_length != 0x27 or self.entries_per_block != 0x0D:
            raise ProDOSError('unexpected entry_length / entries_per_block')

    def block(self, n):
        if not 0 <= n < self.nblocks:
            raise ProDOSError('block %d outside the image (%d blocks)' % (n, self.nblocks))
        return self.d[n * BLOCK:(n + 1) * BLOCK]

    def dir_blocks(self, key):
        """Blocks of a directory file, following the next pointers."""
        out, n, prev = [], key, 0
        while n:
            if n in out:
                raise ProDOSError('directory loop at block %d' % n)
            b = self.block(n)
            if struct.unpack_from('<H', b, 0)[0] != prev:
                raise ProDOSError('directory block %d: previous pointer %d, expected %d'
                                  % (n, struct.unpack_from('<H', b, 0)[0], prev))
            out.append(n)
            prev, n = n, struct.unpack_from('<H', b, 2)[0]
        return out

    def entries(self, key, path=''):
        """Depth-first list of dicts for every active entry under directory <key>."""
        res = []
        for bi, n in enumerate(self.dir_blocks(key)):
            b = self.block(n)
            for i in range(self.entries_per_block):
                if bi == 0 and i == 0:
                    continue                      # header entry
                o = 4 + i * self.entry_length
                st = b[o] >> 4
                if st == 0:
                    continue
                e = {'path': path + '/' + b[o + 1:o + 1 + (b[o] & 15)].decode('latin-1'),
                     'storage': st, 'type': b[o + 0x10],
                     'key': struct.unpack_from('<H', b, o + 0x11)[0],
                     'blocks_used': struct.unpack_from('<H', b, o + 0x13)[0],
                     'eof': b[o + 0x15] | b[o + 0x16] << 8 | b[o + 0x17] << 16,
                     'access': b[o + 0x1E],
                     'aux': struct.unpack_from('<H', b, o + 0x1F)[0],
                     'header_pointer': struct.unpack_from('<H', b, o + 0x25)[0],
                     'dir_block': n}
                res.append(e)
                if st == 0xD:
                    res += self.entries(e['key'], e['path'])
        return res

    def index(self, n, count):
        b = self.block(n)
        return [b[i] | b[256 + i] << 8 for i in range(count)]

    def file_blocks(self, e):
        """(index blocks incl. master, data block list in file order; 0 = sparse)."""
        st, key = e['storage'], e['key']
        if st == 1:
            return [], [key]
        if st == 2:
            return [key], self.index(key, 256)
        if st == 3:
            idx, data = [key], []
            for ib in self.index(key, 128):
                if ib:
                    idx.append(ib)
                    data += self.index(ib, 256)
                else:
                    data += [0] * 256
            return idx, data
        raise ProDOSError('%s: storage type %d not supported' % (e['path'], st))

    def read(self, e):
        _, data = self.file_blocks(e)
        need = (e['eof'] + BLOCK - 1) // BLOCK
        out = bytearray()
        for n in data[:need]:
            out += self.block(n) if n else bytes(BLOCK)
        return bytes(out[:e['eof']])

    def bitmap_free(self, n):
        byte = self.block(self.bitmap_pointer + n // 4096)[(n % 4096) // 8]
        return (byte >> (7 - n % 8)) & 1 == 1

    def check(self):
        """Problems found (empty list = consistent):
        total_blocks fits the image; every directory's file_count = active entries;
        each subdirectory header ($E) points back to its parent entry; every file's
        blocks_used = blocks it references; no block referenced twice or outside the
        volume; every referenced block (plus 0-1, the volume directory and the
        bitmap) is marked used and every other block is marked free; a subdirectory
        header's parent_entry_number is its entry's slot in that block + 1 (slot 0
        of a key block is the header = entry 1; B.2.4, and so on all 8
        subdirectories of A2 DeskTop 1.5) and parent_entry_length is $27."""
        errs = []
        if not 0 < self.total_blocks <= self.nblocks:
            errs.append('total_blocks %d does not fit the image (%d blocks)'
                        % (self.total_blocks, self.nblocks))
            return errs
        owner = {}

        def own(n, who):
            if not 0 <= n < self.total_blocks:
                errs.append('%s: block %d outside the volume' % (who, n))
                return
            owner.setdefault(n, []).append(who)

        own(0, 'loader'); own(1, 'loader')
        for n in self.dir_blocks(2):
            own(n, '/')
        nbm = (self.total_blocks + 4095) // 4096
        for i in range(nbm):
            own(self.bitmap_pointer + i, 'bitmap')

        def count_active(key):
            return sum(1 for bi, n in enumerate(self.dir_blocks(key))
                       for i in range(self.entries_per_block)
                       if not (bi == 0 and i == 0) and self.block(n)[4 + i * 0x27] >> 4)
        if count_active(2) != self.file_count:
            errs.append('/: file_count %d, active entries %d' % (self.file_count, count_active(2)))
        for e in self.entries(2):
            if e['storage'] == 0xD:
                blocks = self.dir_blocks(e['key'])
                for n in blocks:
                    own(n, e['path'])
                h = self.block(e['key'])
                if h[4] >> 4 != 0xE:
                    errs.append('%s: key block is not a subdirectory header' % e['path'])
                fc = struct.unpack_from('<H', h, 0x25)[0]
                if count_active(e['key']) != fc:
                    errs.append('%s: file_count %d, active entries %d'
                                % (e['path'], fc, count_active(e['key'])))
                if struct.unpack_from('<H', h, 0x27)[0] != e['dir_block']:
                    errs.append('%s: parent_pointer does not point at its entry block' % e['path'])
                slot = next(i for i in range(self.entries_per_block)
                            if self.block(e['dir_block'])[4 + i * 0x27:4 + i * 0x27 + 0x13][0x11:]
                            == struct.pack('<H', e['key'])
                            and self.block(e['dir_block'])[4 + i * 0x27] >> 4 == 0xD)
                if h[0x29] != slot + 1 or h[0x2A] != 0x27:
                    errs.append('%s: parent_entry_number %d / length $%02X, entry is slot %d '
                                '(number %d) of block %d' % (e['path'], h[0x29], h[0x2A], slot,
                                                              slot + 1, e['dir_block']))
                if len(blocks) != e['blocks_used']:
                    errs.append('%s: blocks_used %d, directory blocks %d'
                                % (e['path'], e['blocks_used'], len(blocks)))
                continue
            idx, data = self.file_blocks(e)
            refs = idx + [n for n in data if n]
            for n in refs:
                own(n, e['path'])
            if len(refs) != e['blocks_used']:
                errs.append('%s: blocks_used %d, referenced %d' % (e['path'], e['blocks_used'], len(refs)))
        for n, who in owner.items():
            if len(who) > 1:
                errs.append('block %d shared by %s' % (n, ', '.join(who)))
        for n in range(self.total_blocks):
            free = self.bitmap_free(n)
            if n in owner and free:
                errs.append('block %d in use by %s but marked free' % (n, owner[n][0]))
            if n not in owner and not free:
                errs.append('block %d marked used but not referenced' % n)
        return errs


def add_sparse(path, name, kind, seed, expected_out):
    import random
    rnd = random.Random(seed)
    d = bytearray(open(path, 'rb').read())
    vol = Volume(bytes(d))

    def free_blocks():
        return [n for n in range(vol.total_blocks) if vol.bitmap_free(n)]
    pool = free_blocks()

    def take():
        n = pool.pop(0)
        bm = vol.bitmap_pointer * BLOCK + n // 8
        d[bm] &= ~(0x80 >> (n % 8)) & 0xFF            # 0 = used
        return n

    def put(n, data):
        d[n * BLOCK:(n + 1) * BLOCK] = data.ljust(BLOCK, b'\0')

    if kind in ('hole', 'tail'):
        eof = 7 * BLOCK if kind == 'hole' else 5 * BLOCK + 100      # tail: last block (5) absent
        present = [0, 1, 3, 4, 6] if kind == 'hole' else [0, 1, 2, 3, 4]
        storage, key = 2, take()
        index = bytearray(BLOCK)
        expected = bytearray(eof)
        for i in present:
            n = take(); blk = bytes(rnd.randrange(256) for _ in range(BLOCK))
            put(n, blk); index[i], index[256 + i] = n & 0xFF, n >> 8
            expected[i * BLOCK:(i + 1) * BLOCK] = blk[:max(0, min(BLOCK, eof - i * BLOCK))]
        put(key, bytes(index)); used = 1 + len(present)
    elif kind == 'master':
        eof = 200000
        storage, key = 3, take()
        master = bytearray(BLOCK); idx0 = take(); master[0], master[256] = idx0 & 0xFF, idx0 >> 8
        index = bytearray(BLOCK); expected = bytearray(eof); used = 2
        for i in (0, 7, 255):                          # three data blocks under index 0
            n = take(); blk = bytes(rnd.randrange(256) for _ in range(BLOCK))
            put(n, blk); index[i], index[256 + i] = n & 0xFF, n >> 8
            expected[i * BLOCK:(i + 1) * BLOCK] = blk; used += 1
        put(idx0, bytes(index)); put(key, bytes(master))   # master pointer 1 stays 0
    else:
        raise ProDOSError('unknown sparse kind ' + kind)

    # first free root entry
    blk, first = 2, True
    while blk:
        b = vol.block(blk)
        for i in range(13):
            if first and i == 0:
                continue
            o = blk * BLOCK + 4 + i * 0x27
            if d[o] >> 4 == 0:
                e = bytearray(0x27)
                e[0] = storage << 4 | len(name); e[1:1 + len(name)] = name.encode()
                e[0x10] = 0x06; struct.pack_into('<HH', e, 0x11, key, used)
                e[0x15:0x18] = eof.to_bytes(3, 'little'); e[0x1E] = 0xE3
                struct.pack_into('<H', e, 0x25, 2)
                d[o:o + 0x27] = e
                struct.pack_into('<H', d, 2 * BLOCK + 0x25, vol.file_count + 1)
                open(path, 'wb').write(d)
                open(expected_out, 'wb').write(bytes(expected))
                return 0
        blk, first = struct.unpack_from('<H', b, 2)[0], False
    raise ProDOSError('root directory full')


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd, img = argv[1], argv[2]
    try:
        data, info = load(img)
        vol = Volume(data)
        if cmd == 'ls':
            for e in vol.entries(2):
                print('\t'.join(str(e[k]) for k in
                                ('path', 'storage', 'type', 'eof', 'blocks_used', 'key', 'access', 'aux')))
            return 0
        if cmd == 'cat' and len(argv) == 5:
            want = argv[3] if argv[3].startswith('/') else '/' + argv[3]
            for e in vol.entries(2):
                if e['path'].upper() == want.upper() and e['storage'] in (1, 2, 3):
                    open(argv[4], 'wb').write(vol.read(e))
                    return 0
            print('not found: ' + want, file=sys.stderr)
            return 1
        if cmd == 'check':
            errs = vol.check()
            for x in errs:
                print('check: ' + x)
            return 1 if errs else 0
        if cmd == 'add-sparse' and len(argv) == 7:
            return add_sparse(img, argv[3], argv[4], int(argv[5]), argv[6])
        if cmd == 'info':
            for k, v in info.items():
                print('%s=%s' % (k, v))
            for k in ('name', 'file_count', 'bitmap_pointer', 'total_blocks', 'nblocks'):
                print('volume.%s=%s' % (k, getattr(vol, k)))
            return 0
    except (ProDOSError, OSError) as x:
        print('error: %s' % x, file=sys.stderr)
        return 2
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
