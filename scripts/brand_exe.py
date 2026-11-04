"""Replace the icon and version resources of a staged sol.exe.

The build copies CEF's bootstrap.exe byte-for-byte to sol.exe (see
apps/desktop/CMakeLists.txt), so it carries CEF's default icon and the
"CEF Bootstrap Application" description. Packaging stamps a *staged copy* with
Red's icon and version info through the Win32 UpdateResource API; the build
output itself is never modified. The exe is unsigned, so nothing verifies its
resources; if Red is ever signed, stamp before signing.

Usage: python brand_exe.py <exe> <ico> <version a.b.c[.d]>
"""
import ctypes
from ctypes import wintypes
from pathlib import Path
import struct
import sys

RT_ICON, RT_GROUP_ICON, RT_VERSION = 3, 14, 16
LANG_EN_US = 0x0409
GROUP_ID = 32512  # Bootstrap's only icon group; kept so its ordinal stays first.

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.LoadLibraryExW.restype = wintypes.HMODULE
kernel32.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
kernel32.FreeLibrary.argtypes = [wintypes.HMODULE]
kernel32.BeginUpdateResourceW.restype = wintypes.HANDLE
kernel32.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
kernel32.UpdateResourceW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, wintypes.WORD,
                                     ctypes.c_void_p, wintypes.DWORD]
kernel32.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
NameProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p)
LangProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p,
                              wintypes.WORD, ctypes.c_void_p)
kernel32.EnumResourceNamesW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, NameProc, ctypes.c_void_p]
kernel32.EnumResourceLanguagesW.argtypes = [wintypes.HMODULE, ctypes.c_void_p, ctypes.c_void_p, LangProc,
                                            ctypes.c_void_p]


def existing(path, types):
    """[(type, integer name, language)] for the given resource types."""
    module = kernel32.LoadLibraryExW(str(path), None, 0x2 | 0x20)  # datafile + image resource
    if not module:
        raise ctypes.WinError(ctypes.get_last_error())
    found = []
    try:
        for kind in types:
            names = []
            kernel32.EnumResourceNamesW(module, kind, NameProc(lambda m, t, n, l: names.append(n) or True), None)
            for name in names:
                if name >= 0x10000:
                    raise RuntimeError(f"unexpected string-named resource of type {kind}")
                kernel32.EnumResourceLanguagesW(module, kind, name, LangProc(
                    lambda m, t, n, lang, l, name=name, kind=kind: found.append((kind, name, lang)) or True), None)
    finally:
        kernel32.FreeLibrary(module)
    return found


def icon_resources(ico: bytes):
    """Split an .ico file into RT_ICON payloads and the RT_GROUP_ICON directory."""
    reserved, kind, count = struct.unpack_from("<HHH", ico)
    if reserved or kind != 1 or not count:
        raise RuntimeError("not an .ico file")
    group, images = struct.pack("<HHH", 0, 1, count), []
    for index in range(count):
        w, h, colors, _, planes, bits, size, offset = struct.unpack_from("<BBBBHHII", ico, 6 + 16 * index)
        images.append(ico[offset:offset + size])
        group += struct.pack("<BBBBHHIH", w, h, colors, 0, planes, bits, size, index + 1)
    return images, group


def _block(key, value=b"", text=False, children=b""):
    """One VS_VERSIONINFO node: header, key, aligned value, aligned children."""
    def pad(data):
        return data + b"\0" * (-len(data) % 4)
    encoded_key = (key + "\0").encode("utf-16-le")
    value_length = len(value) // 2 if text else len(value)
    body = pad(struct.pack("<HHH", 0, value_length, 1 if text else 0) + encoded_key) + pad(value) + children
    body = struct.pack("<H", len(body)) + body[2:]
    return pad(body)


def version_resource(version: str):
    parts = [int(p) for p in version.split(".")]
    if not 3 <= len(parts) <= 4 or any(not 0 <= p <= 0xFFFF for p in parts):
        raise RuntimeError("version must be a.b.c[.d] with 16-bit parts")
    a, b, c, d = (parts + [0])[:4]
    fixed = struct.pack("<13I", 0xFEEF04BD, 0x00010000, a << 16 | b, c << 16 | d, a << 16 | b, c << 16 | d,
                        0x3F, 0, 0x40004, 1, 0, 0, 0)  # VOS_NT_WINDOWS32, VFT_APP
    strings = {
        "CompanyName": "Sol", "FileDescription": "Sol", "FileVersion": version,
        "InternalName": "sol", "LegalCopyright": "Sol browser",
        "OriginalFilename": "sol.exe", "ProductName": "Sol", "ProductVersion": version,
    }
    table = b"".join(_block(k, (v + "\0").encode("utf-16-le"), True) for k, v in strings.items())
    string_info = _block("StringFileInfo", children=_block("040904B0", children=table))
    var_info = _block("VarFileInfo", children=_block("Translation", struct.pack("<HH", LANG_EN_US, 1200)))
    return _block("VS_VERSION_INFO", fixed, children=string_info + var_info)


def brand(exe: Path, ico: Path, version: str):
    images, group = icon_resources(ico.read_bytes())
    stale = existing(exe, (RT_ICON, RT_GROUP_ICON, RT_VERSION))
    handle = kernel32.BeginUpdateResourceW(str(exe), False)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())

    def update(kind, name, lang, data):
        buffer = ctypes.create_string_buffer(data, len(data)) if data else None
        if not kernel32.UpdateResourceW(handle, kind, name, lang, buffer, len(data) if data else 0):
            error = ctypes.get_last_error()
            kernel32.EndUpdateResourceW(handle, True)
            raise ctypes.WinError(error)

    for kind, name, lang in stale:
        update(kind, name, lang, None)
    for index, image in enumerate(images, 1):
        update(RT_ICON, index, LANG_EN_US, image)
    update(RT_GROUP_ICON, GROUP_ID, LANG_EN_US, group)
    update(RT_VERSION, 1, LANG_EN_US, version_resource(version))
    if not kernel32.EndUpdateResourceW(handle, False):
        raise ctypes.WinError(ctypes.get_last_error())


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    brand(Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3])
