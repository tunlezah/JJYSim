#!/usr/bin/env python3
"""Build docs/web-ui-demo/index.html from the Rev.5 firmware's web page.

The demo is the device's own page, BF-018ARev5/BF_WebUiPage.h, with docs/web-ui-demo/emulator.js loaded just
before the page's script. emulator.js answers the page's /api/ requests in the browser, so the page runs on
GitHub Pages without a device. Run this again whenever BF_WebUiPage.h changes:

    python3 tools/make_web_ui_demo.py
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "BF-018ARev5" / "BF_WebUiPage.h"
TARGET = ROOT / "docs" / "web-ui-demo" / "index.html"

DOCTYPE = "<!doctype html>\n"
SCRIPT = "<script>\n'use strict';"
NOTE = ("<!-- Generated from BF-018ARev5/BF_WebUiPage.h by tools/make_web_ui_demo.py: edit those, not this file.\n"
        "     emulator.js stands in for the device so the page runs in the browser without hardware. -->\n")


def main():
    header = SOURCE.read_text(encoding="utf-8")
    match = re.search(r'R"rawliteral\((.*)\)rawliteral"', header, re.S)
    if not match:
        sys.exit(f"no raw string literal found in {SOURCE}")
    page = match.group(1)
    if not page.startswith(DOCTYPE) or page.count(SCRIPT) != 1:
        sys.exit(f"{SOURCE} no longer has the layout this script expects; update the script")

    page = DOCTYPE + NOTE + page[len(DOCTYPE):]
    page = page.replace(SCRIPT, '<script src="emulator.js"></script>\n' + SCRIPT)
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    TARGET.write_text(page, encoding="utf-8")
    print(f"wrote {TARGET.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
