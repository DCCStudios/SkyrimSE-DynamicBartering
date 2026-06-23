"""Verify the milestone quest FormIDs against Skyrim.esm.

Walks the plugin's record/group tree, finds the records matching the target
FormIDs, and prints each one's signature (should be QUST) + editor id (EDID).
Read-only; does not modify the ESM.
"""
import struct
import zlib
import sys

ESM = r"E:\Skyrim Animation\SKSE\PluginTemplate\Skyrim.esm"

# FormID -> the label we use in MilestoneManager.cpp
TARGETS = {
    0x0001F258: "Arch-Mage (College of Winterhold)",
    0x000D7D69: "Thieves Guild Master",
    0x0001CEF6: "Companions Harbinger",
    0x00053511: "Bards College",
}

COMPRESSED = 0x00040000


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def parse_fields(rd):
    """Return (edid, has_full) from a record's (decompressed) data block."""
    p = 0
    edid = None
    has_full = False
    big_next = None
    n = len(rd)
    while p + 6 <= n:
        ftype = rd[p:p + 4]
        fsize = u16(rd, p + 4)
        p += 6
        if ftype == b"XXXX":
            big_next = u32(rd, p)
            p += fsize
            continue
        if big_next is not None:
            fsize = big_next
            big_next = None
        fdata = rd[p:p + fsize]
        p += fsize
        if ftype == b"EDID":
            edid = fdata.split(b"\x00", 1)[0].decode("ascii", "replace")
        elif ftype == b"FULL":
            has_full = True
    return edid, has_full


def main():
    with open(ESM, "rb") as f:
        data = f.read()
    end = len(data)
    found = {}

    # Iterative walk over the top-level then descend groups via a stack of (pos, end).
    stack = [(0, end)]
    while stack:
        pos, region_end = stack.pop()
        while pos + 24 <= region_end:
            sig = data[pos:pos + 4]
            size = u32(data, pos + 4)
            if sig == b"GRUP":
                # size includes the 24-byte header; descend into its contents.
                stack.append((pos + 24, pos + size))
                pos += size
            else:
                flags = u32(data, pos + 8)
                formid = u32(data, pos + 12)
                if formid in TARGETS and formid not in found:
                    rd = data[pos + 24:pos + 24 + size]
                    if flags & COMPRESSED:
                        try:
                            rd = zlib.decompress(rd[4:])
                        except Exception as e:  # noqa
                            rd = b""
                    edid, has_full = parse_fields(rd)
                    found[formid] = (sig.decode("ascii", "replace"), edid, has_full)
                    if len(found) == len(TARGETS):
                        stack.clear()
                        break
                pos += 24 + size

    print(f"Parsed {ESM} ({end:,} bytes)\n")
    print(f"{'FormID':>10}  {'Sig':<5} {'EDID':<28} {'has FULL':<9} expected")
    print("-" * 90)
    for fid, label in TARGETS.items():
        if fid in found:
            sig, edid, has_full = found[fid]
            ok = "QUST" if sig == "QUST" else f"** {sig} (NOT QUST) **"
            print(f"0x{fid:08X}  {sig:<5} {str(edid):<28} {str(has_full):<9} {label}")
        else:
            print(f"0x{fid:08X}  {'----':<5} {'<NOT FOUND>':<28} {'-':<9} {label}")


if __name__ == "__main__":
    main()
