#!/usr/bin/env python3
"""Extract and adapt the supplied NoviiRemote 3.73D archive for our IR bridge."""
import hashlib
from pathlib import Path
import sys
import struct
import zipfile

EXPECTED_SHA256 = '9f6f984ca60946fecaf0af5724b8430f7dcf39c78837286cd23af23a8e8a90d0'
FILES = [
    'Install/NoviiRemote Deluxe V3.73D.prc',
    'Install/NR-IR-Archive.pdb',
    'Skins/LowRes/NR-Skin-Classic-LoRes.prc',
    'Skins/LowRes/NR-Skin-System-LoRes.prc',
    'Start Profile/EN/NR-Main-Profile.pdb',
    *[f'Start Profile/EN/Remote{i}.pdb' for i in range(1, 6)],
]


def prepare(archive, destination, e2=False):
    with zipfile.ZipFile(archive) as source:
        app = bytearray(source.read(FILES[0]))
        if hashlib.sha256(app).hexdigest() != EXPECTED_SHA256:
            raise ValueError('Unsupported Novii PRC: expected the English 3.73D build 20080411b')
        # Preserve each routine's C calling convention. Duplicate arguments,
        # prepend NRIR, invoke private emulator trap, discard copies, return.
        patches = [
            (0x12874, '4e5600002f0a246e0008',
             '2f2f00042f3c4e5249524e4fa3ec504f4e75'),
            (0x127ea, '4e56000048e71820246e0008',
             '3f2f00082f2f00062f3c4e5249524e4fa3ed4fef000a4e75'),
        ]
        if e2:
            patches = []
            start = 0x14ce8
            native_calls = 0
            while True:
                offset = app.find(bytes.fromhex('4e4fa45a'), start, 0x15536)
                if offset < 0:
                    break
                app[offset + 2:offset + 4] = bytes.fromhex('a3ee')
                start = offset + 4
                native_calls += 1
            if native_calls != 7:
                raise ValueError('Unexpected Novii ARM IR call count')
            for i in range(struct.unpack_from('>H', app, 76)[0]):
                kind, resource_id, offset = struct.unpack_from('>4sHI', app, 78 + 10*i)
                if kind == b'ARMC':
                    app[offset:offset + 4] = b'NRIR'
        for offset, original, replacement in patches:
            expected, stub = bytes.fromhex(original), bytes.fromhex(replacement)
            if app[offset:offset + len(expected)] != expected:
                raise ValueError('Unexpected Novii IR routine signature')
            app[offset:offset + len(stub)] = stub
        destination = Path(destination)
        destination.mkdir(parents=True, exist_ok=True)
        files = FILES
        if e2:
            files = [entry for entry in FILES if not entry.startswith('Skins/')]
            files += ['Skins/HiRes/EN/NR-Skin-Classic-HiRes.prc',
                      'Skins/HiRes/EN/NR-Skin-System-HiRes.prc']
        for entry in files:
            (destination / Path(entry).name).write_bytes(app if entry == FILES[0] else source.read(entry))


if __name__ == '__main__':
    args = sys.argv[1:]
    e2 = '--e2' in args
    if e2: args.remove('--e2')
    if len(args) != 2:
        sys.exit('Usage: prepare-novii.py [--e2] NOVII_ZIP OUTPUT_DIRECTORY')
    prepare(*args, e2=e2)
