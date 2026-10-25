#!/usr/bin/env python3
"""Extract original Palm tAIB launcher icons into independent Android apps.

Requires Pillow. PRC files remain local; only the extracted launcher art is
written to Android resources. Handles the raw/scanline 8-bit bitmap families
used by the supplied OmniRemote and NoviiRemote PRCs.
"""
import argparse
from pathlib import Path
import struct
import zipfile
from PIL import Image, PalmImagePlugin


def icon_resource(prc):
    count = struct.unpack_from('>H', prc, 76)[0]
    entries = [struct.unpack_from('>4sHI', prc, 78 + i*10) for i in range(count)]
    for i, (kind, number, offset) in enumerate(entries):
        if kind == b'tAIB' and number == 1000:
            end = entries[i+1][2] if i+1 < count else len(prc)
            return prc[offset:end]
    raise ValueError('PRC has no tAIB 1000 launcher icon')


def decode_icon(data):
    offset = 0
    candidates = []
    palette_bytes = PalmImagePlugin.Palm8BitColormapImage.getpalette()
    default_palette = [tuple(palette_bytes[i:i+3]) for i in range(0, 768, 3)]
    while offset + 16 <= len(data):
        width, height, row, flags = struct.unpack_from('>HHHH', data, offset)
        depth, version = data[offset+8:offset+10]
        # Palm's sentinel introduces a version 3 high-density bitmap.
        if width == height == 0 and depth == 255 and version == 1:
            offset += 16
            continue
        if version == 3:
            header = data[offset+10]
            compression = data[offset+13]
            transparent = struct.unpack_from('>I', data, offset+16)[0]
            next_offset = struct.unpack_from('>I', data, offset+20)[0]
        else:
            header = 16
            transparent, compression = data[offset+12:offset+14]
            next_offset = struct.unpack_from('>H', data, offset+10)[0] * 4
        if depth == 8:
            cursor = offset + header
            palette = default_palette.copy()
            if flags & 0x4000:
                count = struct.unpack_from('>H', data, cursor)[0]
                cursor += 2
                for _ in range(count):
                    index, red, green, blue = data[cursor:cursor+4]
                    palette[index] = (red, green, blue)
                    cursor += 4
            if flags & 0x8000:
                if compression != 0:
                    raise ValueError('Unsupported Palm bitmap compression')
                cursor += 4 if version == 3 else 2
                raw = bytearray()
                previous = bytearray(row)
                for _ in range(height):
                    current = bytearray(previous)
                    for start in range(0, row, 8):
                        mask = data[cursor]
                        cursor += 1
                        for bit in range(min(8, row-start)):
                            if mask & (0x80 >> bit):
                                current[start+bit] = data[cursor]
                                cursor += 1
                    raw.extend(current)
                    previous = current
            else:
                raw = data[cursor:cursor + row*height]
            if len(raw) != row*height:
                raise ValueError('Truncated Palm icon bitmap')
            image = Image.new('RGBA', (width, height))
            image.putdata([
                (0, 0, 0, 0) if flags & 0x2000 and raw[y*row+x] == transparent
                else (*palette[raw[y*row+x]], 255)
                for y in range(height) for x in range(width)
            ])
            candidates.append(image)
        if not next_offset:
            break
        offset += next_offset
    if not candidates:
        raise ValueError('No supported color launcher icon')
    return max(candidates, key=lambda image: image.width*image.height)


def render(icon, size, content_size, background):
    scale = content_size / max(icon.size)
    artwork = icon.resize((round(icon.width*scale), round(icon.height*scale)), Image.Resampling.NEAREST)
    canvas = Image.new('RGBA', (size, size), background)
    canvas.alpha_composite(artwork, ((size-artwork.width)//2, (size-artwork.height)//2))
    return canvas


def write_android(icon, module):
    # Palm resources can carry asymmetric transparent padding. Center the
    # visible artwork, rather than the original resource canvas.
    bounds = icon.getchannel('A').getbbox()
    if bounds is None:
        raise ValueError('Launcher icon is entirely transparent')
    icon = icon.crop(bounds)
    resources = module/'src/main/res'
    drawable = resources/'drawable-nodpi'
    drawable.mkdir(parents=True, exist_ok=True)
    render(icon, 432, 256, (0, 0, 0, 0)).save(drawable/'ic_launcher_foreground.png')
    for density, size in [('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192)]:
        directory = resources/f'mipmap-{density}'
        directory.mkdir(parents=True, exist_ok=True)
        render(icon, size, round(size*.8), (0, 0, 0, 0)).save(directory/'ic_launcher.png')
    adaptive = resources/'mipmap-anydpi-v26'
    adaptive.mkdir(parents=True, exist_ok=True)
    (adaptive/'ic_launcher.xml').write_text('''<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@android:color/transparent" />
    <foreground android:drawable="@drawable/ic_launcher_foreground" />
</adaptive-icon>
''')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--omni', type=Path, required=True)
    parser.add_argument('--novii', type=Path, required=True, help='Novii PRC or supplied zip')
    args = parser.parse_args()
    novii = args.novii.read_bytes()
    if zipfile.is_zipfile(args.novii):
        with zipfile.ZipFile(args.novii) as archive:
            novii = archive.read('Install/NoviiRemote Deluxe V3.73D.prc')
    root = Path(__file__).resolve().parents[1]
    for module, prc in [('omniremote', args.omni.read_bytes()), ('noviiremote', novii)]:
        icon = decode_icon(icon_resource(prc))
        write_android(icon, root/module)
        print(f'{module}: extracted original {icon.width}×{icon.height} Palm icon')
