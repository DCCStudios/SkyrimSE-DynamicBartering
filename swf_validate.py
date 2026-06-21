"""Strict-ish SWF tag walker to catch malformed tag lengths.

Walks the tag stream of an (uncompressed FWS) SWF and reports every tag with its
type/length, flags any tag whose length runs past EOF, and confirms the stream
terminates on an End(0) tag. Used to compare a known-good SWF (BarterOffer.swf)
against a generated glyph SWF to find structural defects before loading in-game.
"""
import struct
import sys

TAG_NAMES = {
    0: "End", 1: "ShowFrame", 9: "SetBackgroundColor", 12: "DoAction",
    26: "PlaceObject2", 32: "DefineShape3", 36: "DefineBitsLossless2",
    37: "DefineEditText", 39: "DefineSprite", 56: "ExportAssets",
    69: "FileAttributes", 71: "ImportAssets2",
}


def read_rect(buf, pos):
    # nbits in top 5 bits of first byte; rect is nbits*4 fields. Bit-level.
    first = buf[pos]
    nbits = first >> 3
    total_bits = 5 + nbits * 4
    total_bytes = (total_bits + 7) // 8
    return pos + total_bytes


def walk(path):
    with open(path, 'rb') as f:
        data = f.read()
    sig = data[:3]
    if sig == b'CWS':
        print(f"{path}: COMPRESSED (CWS) - cannot walk without zlib decompress of body")
        return False
    if sig != b'FWS':
        print(f"{path}: not an SWF (sig={sig!r})")
        return False
    version = data[3]
    file_len = struct.unpack('<I', data[4:8])[0]
    print(f"{path}: FWS v{version} declared_len={file_len} actual_len={len(data)}")
    if file_len != len(data):
        print(f"  !! header file length {file_len} != actual {len(data)}")

    pos = 8
    pos = read_rect(data, pos)   # frame size rect
    pos += 2                      # frame rate (8.8)
    pos += 2                      # frame count

    ok = True
    saw_end = False
    n = 0
    while pos < len(data):
        if pos + 2 > len(data):
            print(f"  !! truncated tag header at {pos}")
            ok = False
            break
        rec = struct.unpack('<H', data[pos:pos + 2])[0]
        pos += 2
        tag_type = rec >> 6
        length = rec & 0x3F
        long_form = (length == 0x3F)
        if long_form:
            if pos + 4 > len(data):
                print(f"  !! truncated long length at {pos}")
                ok = False
                break
            length = struct.unpack('<I', data[pos:pos + 4])[0]
            pos += 4
        name = TAG_NAMES.get(tag_type, f"Unknown({tag_type})")
        end = pos + length
        flag = ""
        if end > len(data):
            flag = "  <<< LENGTH OVERRUNS EOF"
            ok = False
        if n < 60 or tag_type in (0,):
            print(f"  [{n:03}] {name:<20} type={tag_type:<3} len={length:<8} body=[{pos}:{end}]{flag}")
        pos = end
        n += 1
        if tag_type == 0:
            saw_end = True
            break
    if not saw_end:
        print("  !! no End(0) tag reached")
        ok = False
    print(f"  => {'OK' if ok else 'MALFORMED'} ({n} tags)\n")
    return ok


if __name__ == '__main__':
    for p in sys.argv[1:]:
        walk(p)
