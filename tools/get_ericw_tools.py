#!/usr/bin/env python3
"""Downloads the ericw-tools release OpenTech2 is tested with (map compiler: qbsp,
vis, light) into build/tools/ericw-tools/.

ericw-tools is a separate GPL3 project (https://github.com/ericwa/ericw-tools);
it is downloaded from its own releases, unchanged, and checked against the
sha256 below. Bump ERICW_VERSION and the hashes together when moving to a new
release, and re-run the map checks.

Usage:
  get_ericw_tools.py              download for this system (Linux, macOS, Windows)
  get_ericw_tools.py --force      download again even if already there
"""

import argparse
import hashlib
import platform
import shutil
import stat
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ERICW_VERSION = "2.0.0-alpha11"
RELEASE_URL = f"https://github.com/ericwa/ericw-tools/releases/download/{ERICW_VERSION}"
# release file per system, with its sha256 as published on the release page
DOWNLOADS = {
    "Linux": ("ericw-tools-2.0.0-alpha11-Linux.zip",
              "166109ab47657d291142992070bcb661e3029708916e152c690473dfb0f74f15"),
    "Darwin": ("ericw-tools-2.0.0-alpha11-Darwin.zip",
               "bc5b66fb00234e3697373f38d65130fd0d14fc9eaa549277f6c93eabd4504531"),
    "Windows": ("ericw-tools-2.0.0-alpha11-win64.zip",
                "4e5ea11be2194a1c4acac6d6da9d5b5b9f65324fda2d67efa0731d1fd8e0745f"),
}

REPO_DIR = Path(__file__).resolve().parent.parent
INSTALL_DIR = REPO_DIR / "build" / "tools" / "ericw-tools"
VERSION_FILE = INSTALL_DIR / "VERSION"
PROGRAMS = ("qbsp", "vis", "light", "bspinfo", "bsputil", "maputil", "lightpreview")


def extract_safely(archive_path, target_dir):
    """Unzip, refusing any entry that would land outside target_dir."""
    target_root = target_dir.resolve()
    with zipfile.ZipFile(archive_path) as archive:
        for member in archive.infolist():
            destination = (target_dir / member.filename).resolve()
            if target_root not in destination.parents and destination != target_root:
                sys.exit(f"refusing archive entry outside the install folder: {member.filename}")
        archive.extractall(target_dir)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--force", action="store_true", help="download again even if this version is installed")
    arguments = parser.parse_args()

    system = platform.system()
    if system not in DOWNLOADS:
        sys.exit(f"no ericw-tools release for {system}; get it from {RELEASE_URL}")
    file_name, expected_sha256 = DOWNLOADS[system]

    if VERSION_FILE.exists() and VERSION_FILE.read_text().strip() == ERICW_VERSION and not arguments.force:
        print(f"ericw-tools {ERICW_VERSION} already in {INSTALL_DIR}")
        return 0

    with tempfile.TemporaryDirectory(prefix="optech2-ericw-") as temp_folder:
        archive_path = Path(temp_folder) / file_name
        print(f"downloading {RELEASE_URL}/{file_name}")
        with urllib.request.urlopen(f"{RELEASE_URL}/{file_name}") as response, open(archive_path, "wb") as archive:
            shutil.copyfileobj(response, archive)

        actual_sha256 = hashlib.sha256(archive_path.read_bytes()).hexdigest()
        if actual_sha256 != expected_sha256:
            sys.exit(f"sha256 mismatch for {file_name}: got {actual_sha256}, expected {expected_sha256}")

        if INSTALL_DIR.exists():
            shutil.rmtree(INSTALL_DIR)
        INSTALL_DIR.mkdir(parents=True)
        extract_safely(archive_path, INSTALL_DIR)

    # zip files do not keep the executable bit
    if system != "Windows":
        for program in PROGRAMS:
            program_path = INSTALL_DIR / program
            if program_path.exists():
                program_path.chmod(program_path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    VERSION_FILE.write_text(ERICW_VERSION + "\n")
    print(f"ericw-tools {ERICW_VERSION} installed in {INSTALL_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
