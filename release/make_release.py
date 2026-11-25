"""Makes the download: a zip with jamo.exe, install.cmd, uninstall.cmd, a short README, the guide, the language rules and the
examples. Run build.bat first, so jamo.exe is up to date. Usage: python release/make_release.py [folder for the zip]"""
import re
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent


def version():
    source = (ROOT / "compiler" / "jamo.jamo").read_text(encoding="utf-8")
    return re.search(r'^const VERSION = "([^"]+)"', source, re.M).group(1)


def make_release(out_folder):
    name = f"jamo-{version()}-windows"
    out_folder.mkdir(parents=True, exist_ok=True)
    zip_path = out_folder / f"{name}.zip"
    files = [(ROOT / "jamo.exe", "jamo.exe"), (HERE / "install.cmd", "install.cmd"), (HERE / "uninstall.cmd", "uninstall.cmd"), (HERE / "README.txt", "README.txt"),
             (ROOT / "GUIDE.md", "GUIDE.md"), (ROOT / "LANGUAGE.md", "LANGUAGE.md")]
    files += [(path, f"examples/{path.name}") for path in sorted((ROOT / "examples").glob("*.jamo"))]
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as archive:
        for path, inside in files:
            data = path.read_bytes()
            if path.suffix in (".cmd", ".txt"):
                # Windows' own tools want Windows line endings
                data = data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
            archive.writestr(f"{name}/{inside}", data)
    return zip_path


if __name__ == "__main__":
    made = make_release(Path(sys.argv[1]) if len(sys.argv) > 1 else HERE)
    print(f"Made {made} ({made.stat().st_size // 1024} KB)")
