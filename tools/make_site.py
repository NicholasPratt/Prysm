#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Nicholas Pratt (after Felucca's web/make_site.py, Leo Kuroshita, Hügelton Instruments)
"""Make Prysm's site (GitHub Pages: .github/workflows/pages.yml publishes site/):

  index.html                 the web installer: web/index.html with web/fm1pkg.js, web/fm1ota.js and the
                             package's metadata inlined (one file, no build step in the browser)
  manual.html                docs/MANUAL.md as a page
  splash.jpg                 the picture
  firmware/prysm-VER.fwsc    the package, with LICENSE, LICENSING.md and LICENSES/ beside it (the package holds
                             three JieLi AC79 SDK files, Apache-2.0)

  tools/make_site.py build/prysm-X.Y.fwsc X.Y [OUT_DIR, default site]

Only a package made by this tree's build is accepted: a release identity (FM-1_98N) and Felucca's update loader
(never vendor files patched in).
"""
import html
import json
import re
import shutil
import sys
from pathlib import Path

import markdown

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
SOURCE = "https://github.com/NicholasPratt/Prysm"


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def github_slug(text, _sep):
    """heading ids as GitHub makes them, so the manual's links work in both places"""
    s = re.sub(r"[^\w\- ]", "", html.unescape(re.sub(r"<[^>]+>", "", text)).lower())
    return s.replace(" ", "-")


MANUAL_CSS = """
:root { --bg: #fbfaff; --fg: #17141f; --dim: #6c6878; --acc: #7d5bd6; --line: #e2dcf2; --code: #f1edfa; }
@media (prefers-color-scheme: dark) { :root { --bg: #0b0a10; --fg: #ece9f5; --dim: #8d88a0; --acc: #b48cff;
  --line: #2a2538; --code: #17141f; color-scheme: dark; } }
body { margin: 0; background: var(--bg); color: var(--fg);
       font: 16px/1.6 -apple-system, BlinkMacSystemFont, "Segoe UI", "Helvetica Neue", Arial, sans-serif; }
main { max-width: 820px; margin: 0 auto; padding: 32px 16px 64px; }
h1 { color: var(--acc); letter-spacing: .04em; } h2 { margin-top: 8px; }
a { color: var(--acc); } hr { border: 0; border-top: 1px solid var(--line); margin: 28px 0; }
table { border-collapse: collapse; margin: 10px 0 18px; display: block; overflow-x: auto; }
th, td { border: 1px solid var(--line); padding: 5px 9px; text-align: left; vertical-align: top; }
th { background: var(--code); }
thead:has(th:empty:first-child) { display: none; }   /* (tables with no headings) */
code, pre { background: var(--code); font-family: ui-monospace, Menlo, Consolas, monospace; font-size: 13px; }
pre { padding: 12px; overflow-x: auto; } code { padding: 1px 4px; } pre code { padding: 0; }
.top { font-size: 14px; color: var(--dim); }
"""


def main(pkg, version, out="site"):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_98\d", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not a Prysm release (FM-1_98N: ./build.sh --release X.Y)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no Felucca update loader in it: only packages made by ./build.sh go on the site")
    name = f"prysm-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"

    page = (WEB / "index.html").read_text(encoding="utf-8")
    lib = strip_module((WEB / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((WEB / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": "firmware/" + name, "source": SOURCE})
    for mark in ("/*LIB*/", "/*META*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"web/index.html must contain {mark} once")
    page = page.replace("/*LIB*/", lib).replace("/*META*/", meta)

    fw = out / "firmware"
    (fw / "LICENSES").mkdir(parents=True, exist_ok=True)
    for old in fw.glob("prysm-*.fwsc"):             # one package: the current one
        old.unlink()
    (out / "index.html").write_text(page, encoding="utf-8")
    (out / ".nojekyll").write_text("")
    shutil.copy(pkg, fw / name)
    shutil.copy(ROOT / "assets" / "splash.jpg", out / "splash.jpg")
    for doc in ("LICENSE", "LICENSING.md"):
        shutil.copy(ROOT / doc, fw / doc)
    names = sorted(f.name for f in (ROOT / "LICENSES").glob("*.txt"))
    for n in names:
        shutil.copy(ROOT / "LICENSES" / n, fw / "LICENSES" / n)
    (fw / "LICENSES" / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><title>Prysm licences</title><h1>Licence texts</h1><ul>'
        + "".join(f'<li><a href="{n}">{n}</a></li>' for n in names)
        + '</ul><p><a href="../LICENSING.md">LICENSING.md</a> · <a href="../LICENSE">LICENSE (GPL-3.0)</a></p>\n',
        encoding="utf-8")

    md = (ROOT / "docs" / "MANUAL.md").read_text(encoding="utf-8")
    body = markdown.markdown(md, extensions=["tables", "fenced_code", "toc"],
                             extension_configs={"toc": {"slugify": github_slug}})
    (out / "manual.html").write_text(
        '<!doctype html><html lang="en"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width, initial-scale=1"><title>Prysm manual</title>'
        f"<style>{MANUAL_CSS}</style></head><body><main>"
        f'<p class="top"><a href="./">Prysm installer</a> · version {html.escape(version)}</p>{body}</main></body></html>\n',
        encoding="utf-8")
    print(f"{out}/: index.html (installer, {product}), manual.html, firmware/{name} ({len(raw)} bytes)")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
