"""One-off generator for the crafted zip fixtures test_zip_installer.cpp consumes.

Run manually whenever a fixture needs to change:

    python tests/cpp_fixtures/generate_fixtures.py

The resulting .zip files are committed to the repo as binary fixtures --
test_zip_installer.cpp does not regenerate them, so it has no Python
dependency at C++ test-run time.
"""

import io
import struct
import zipfile
from pathlib import Path

FIXTURES_DIR = Path(__file__).resolve().parent


def make_valid_plugin_zip() -> None:
    path = FIXTURES_DIR / "valid_plugin.zip"
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("manifest.json", '{"id": "valid_plugin"}')
        zf.writestr("main.py", "EXPOSED_FUNCTIONS = {}\n")
        zf.writestr("subdir/helper.py", "def helper():\n    return 1\n")


def make_path_traversal_zip() -> None:
    path = FIXTURES_DIR / "path_traversal.zip"
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("manifest.json", '{"id": "evil_plugin"}')
        zf.writestr("../evil.txt", "escaped the staging dir\n")


def make_symlink_zip() -> None:
    path = FIXTURES_DIR / "symlink_entry.zip"
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("manifest.json", '{"id": "evil_plugin"}')
        info = zipfile.ZipInfo("link_to_etc_passwd")
        # Unix mode in the upper 16 bits of external_attr; S_IFLNK (0o120000)
        # marks this entry as a symlink rather than a regular file.
        info.external_attr = (0o120777 << 16)
        zf.writestr(info, "/etc/passwd")


def make_oversized_entry_zip() -> None:
    # An entry whose HONEST header already exceeds the per-entry cap used in
    # the corresponding test (see test_zip_installer.cpp) -- exercises the
    # cheap header-based pre-check.
    path = FIXTURES_DIR / "oversized_entry.zip"
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("manifest.json", '{"id": "big_plugin"}')
        zf.writestr("big.bin", b"\x00" * (2 * 1024 * 1024))  # 2 MiB, honestly reported


def make_lying_header_zip() -> None:
    # A zip-bomb-shaped entry (highly compressible) whose LOCAL and CENTRAL
    # DIRECTORY uncompressed-size fields are patched down to a small, innocuous
    # value after the fact -- so any check that trusts header fields alone
    # would wave it through, while actually inflating it produces far more
    # bytes than declared. Proves the live, recounted-during-decompression cap
    # is what actually stops this, not the (lied-to) header pre-check.
    real_size = 2 * 1024 * 1024  # 2 MiB of repeated bytes -> compresses to ~KB
    lied_size = 100  # far under any per-entry cap used in the test

    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("manifest.json", '{"id": "lying_plugin"}')
        zf.writestr("bomb.bin", b"\x00" * real_size)
    data = bytearray(buf.getvalue())

    # Local file header: signature(4) ... uncompressed_size at offset 22 (4 bytes LE).
    # Central directory header: signature(4) ... uncompressed_size at offset 24 (4 bytes LE).
    def patch_uncompressed_size(data: bytearray, filename: bytes, new_size: int) -> None:
        needle = filename
        local_sig = b"PK\x03\x04"
        idx = 0
        found_local = False
        while True:
            idx = data.find(local_sig, idx)
            if idx == -1:
                break
            name_len = struct.unpack_from("<H", data, idx + 26)[0]
            extra_len = struct.unpack_from("<H", data, idx + 28)[0]
            name = bytes(data[idx + 30: idx + 30 + name_len])
            if name == needle:
                struct.pack_into("<I", data, idx + 22, new_size)
                found_local = True
            idx += 4
        if not found_local:
            raise RuntimeError(f"local header for {filename!r} not found")

        central_sig = b"PK\x01\x02"
        idx = 0
        found_central = False
        while True:
            idx = data.find(central_sig, idx)
            if idx == -1:
                break
            name_len = struct.unpack_from("<H", data, idx + 28)[0]
            name = bytes(data[idx + 46: idx + 46 + name_len])
            if name == needle:
                struct.pack_into("<I", data, idx + 24, new_size)
                found_central = True
            idx += 4
        if not found_central:
            raise RuntimeError(f"central directory header for {filename!r} not found")

    patch_uncompressed_size(data, b"bomb.bin", lied_size)

    path = FIXTURES_DIR / "lying_header.zip"
    path.write_bytes(bytes(data))


if __name__ == "__main__":
    make_valid_plugin_zip()
    make_path_traversal_zip()
    make_symlink_zip()
    make_oversized_entry_zip()
    make_lying_header_zip()
    print(f"Fixtures written to {FIXTURES_DIR}")
