"""Build an EditorID -> (signature, FormID) index from Skyrim.esm for selected
record types, so milestone/hold detection FormIDs can be verified instead of guessed.

Read-only. Prints:
  * exact lookups for a candidate name list,
  * filtered dumps (LCTN holds, CW/hold keywords, civil-war + crime factions).
"""
import struct
import zlib

ESM = r"E:\Skyrim Animation\SKSE\PluginTemplate\Skyrim.esm"

WANT_SIGS = {b"QUST", b"GLOB", b"FACT", b"KYWD", b"LCTN"}
COMPRESSED = 0x00040000


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def get_edid(rd):
    p = 0
    n = len(rd)
    big_next = None
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
        if ftype == b"EDID":
            return rd[p:p + fsize].split(b"\x00", 1)[0].decode("ascii", "replace")
        # EDID is always first for these record types; bail early.
        return None
    return None


def main():
    with open(ESM, "rb") as f:
        data = f.read()
    end = len(data)

    # edid_lower -> (sig, formid, edid_original)
    index = {}
    stack = [(0, end)]
    while stack:
        pos, region_end = stack.pop()
        while pos + 24 <= region_end:
            sig = data[pos:pos + 4]
            size = u32(data, pos + 4)
            if sig == b"GRUP":
                stack.append((pos + 24, pos + size))
                pos += size
            else:
                if sig in WANT_SIGS:
                    flags = u32(data, pos + 8)
                    formid = u32(data, pos + 12)
                    rd = data[pos + 24:pos + 24 + size]
                    if flags & COMPRESSED:
                        try:
                            rd = zlib.decompress(rd[4:])
                        except Exception:
                            rd = b""
                    edid = get_edid(rd)
                    if edid:
                        index[edid.lower()] = (sig.decode(), formid, edid)
                pos += 24 + size

    def show(name):
        v = index.get(name.lower())
        if v:
            sig, fid, edid = v
            print(f"  {edid:<28} {sig}  0x{fid:08X}")
        else:
            print(f"  {name:<28} <NOT FOUND>")

    print("== Thane / favor quests ==")
    for n in ["FavorJarlsMakeFriends", "FreeformRiftenThane",
              "Favor250", "Favor251", "Favor252", "Favor253", "Favor254",
              "Favor255", "Favor256", "Favor257", "Favor258"]:
        show(n)

    print("\n== Civil war + crime factions ==")
    for n in ["CWImperialFaction", "CWSonsofSkyrimFaction", "CWSonsOfSkyrim",
              "SonsOfSkyrimFaction", "CWPotentialAllyFaction", "GovImperial",
              "Stormcloaks", "SonsOfSkyrim", "ImperialLegion", "CWSons",
              "CWStormcloakFaction", "CWFactionImperial", "CWFactionSons"]:
        show(n)

    print("\n== All FACT with cw/stormcloak/sons/legion/imperial in editor id ==")
    for k, (sig, fid, edid) in sorted(index.items()):
        if sig == "FACT" and any(t in k for t in
                                 ("stormcloak", "sonsofskyrim", "imperiallegion")) \
                or (sig == "FACT" and k.startswith("cw")):
            print(f"  {edid:<40} 0x{fid:08X}")

    print("\n== Keywords ==")
    for n in ["CWOwner", "LocTypeHold"]:
        show(n)

    print("\n== All KYWD containing 'cw' or starting 'loctype' ==")
    for k, (sig, fid, edid) in sorted(index.items()):
        if sig == "KYWD" and ("cw" in k or k.startswith("loctype")):
            print(f"  {edid:<32} 0x{fid:08X}")

    print("\n== LCTN with 'hold' in editor id ==")
    for k, (sig, fid, edid) in sorted(index.items()):
        if sig == "LCTN" and "hold" in k:
            print(f"  {edid:<40} 0x{fid:08X}")

    print("\n== FACT with 'crimefaction' in editor id ==")
    for k, (sig, fid, edid) in sorted(index.items()):
        if sig == "FACT" and "crimefaction" in k:
            print(f"  {edid:<40} 0x{fid:08X}")

    print(f"\n(indexed {len(index)} named records of types {sorted(s.decode() for s in WANT_SIGS)})")


if __name__ == "__main__":
    main()
