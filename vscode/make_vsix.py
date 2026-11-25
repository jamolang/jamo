"""Packs the extension into jamo.vsix, which VS Code installs from one file. For developing Jamo only."""
import json
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
FILES = ["package.json", "extension.js", "jamo.tmLanguage.json", "language-configuration.json"]
FIXED_TIME = (2026, 1, 1, 0, 0, 0)


def make_vsix(out_path):
    package = json.loads((HERE / "package.json").read_text(encoding="utf-8"))
    manifest = f"""<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Language="en-US" Id="{package['name']}" Version="{package['version']}" Publisher="{package['publisher']}"/>
    <DisplayName>{package['displayName']}</DisplayName>
    <Description xml:space="preserve">{package['description']}</Description>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="{package['engines']['vscode']}"/>
    </Properties>
  </Metadata>
  <Installation><InstallationTarget Id="Microsoft.VisualStudio.Code"/></Installation>
  <Dependencies/>
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true"/>
  </Assets>
</PackageManifest>
"""
    content_types = """<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension=".json" ContentType="application/json"/>
  <Default Extension=".js" ContentType="application/javascript"/>
  <Default Extension=".vsixmanifest" ContentType="text/xml"/>
</Types>
"""
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as archive:
        def add(name, data):
            archive.writestr(zipfile.ZipInfo(name, FIXED_TIME), data, zipfile.ZIP_DEFLATED)
        add("extension.vsixmanifest", manifest)
        add("[Content_Types].xml", content_types)
        for name in FILES:
            add(f"extension/{name}", (HERE / name).read_bytes())


if __name__ == "__main__":
    make_vsix(Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "jamo.vsix")
