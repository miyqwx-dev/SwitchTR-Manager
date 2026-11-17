"""Uygulama gorsellerini uretir. Gorseller yeniden cizilmez, yalnizca boyutlandirilir.

Kullanim:  python tools/make_assets.py   (Pillow ve qrcode gerekir)

Uretilenler:
  icon.jpg                 256x256 NRO ikonu (assets/icon_src.webp sigdirilarak)
  data/dev_logo.bin        160x160 RGBA, yuvarlak gelistirici logosu
  data/patch_logo.bin      160x160 RGBA, yuvarlak yama gelistiricisi logosu
  source/qr_codes.h        GitHub sayfalari icin QR kod matrisleri
  assets/preview_*.png     onizlemeler
"""
import os

import qrcode
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
A = lambda *p: os.path.join(ROOT, *p)

LOGO = 160
SS = 4  # kenar yumusatma icin buyuk cizip kucultme
QRS = [("QR_DEV", "https://github.com/miyqwx-dev"), ("QR_PATCH", "https://github.com/pextechh/SwitchTR")]


def circle_logo(src, out_bin, out_png):
    """Kare ortadan alinir, kuculturulur ve yuvarlak maskelenir (kirpma yok, sadece kose)."""
    im = Image.open(src).convert("RGBA")
    w, h = im.size
    side = min(w, h)
    left, top = (w - side) // 2, (h - side) // 2
    im = im.crop((left, top, left + side, top + side)).resize((LOGO * SS, LOGO * SS), Image.LANCZOS)
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, im.size[0] - 1, im.size[1] - 1), fill=255)
    im.putalpha(mask)
    im = im.resize((LOGO, LOGO), Image.LANCZOS)
    im.save(out_png)
    with open(out_bin, "wb") as f:
        f.write(im.tobytes())  # RGBA8888, satir satir


def make_icon():
    """Gorseli oranini bozmadan 256x256'ya sigdirir; bos kalan ust/alt, gorselin kenar rengiyle dolar."""
    im = Image.open(A("assets", "icon_src.webp")).convert("RGB")
    w, h = im.size
    # kenar rengi: dort koseden ortalama
    px = [im.getpixel(p) for p in [(2, 2), (w - 3, 2), (2, h - 3), (w - 3, h - 3)]]
    bg = tuple(sum(c[i] for c in px) // len(px) for i in range(3))
    scale = 256 / max(w, h)
    nw, nh = round(w * scale), round(h * scale)
    small = im.resize((nw, nh), Image.LANCZOS)
    out = Image.new("RGB", (256, 256), bg)
    out.paste(small, ((256 - nw) // 2, (256 - nh) // 2))
    out.save(A("icon.jpg"), quality=95)
    out.save(A("assets", "preview_icon.png"))


def make_qr():
    with open(A("source", "qr_codes.h"), "w", newline="\n") as f:
        f.write("// tools/make_assets.py tarafindan uretildi\n#pragma once\n")
        for name, url in QRS:
            q = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M, border=0)
            q.add_data(url)
            q.make(fit=True)
            m = q.get_matrix()
            f.write("// %s\nstatic const int %s_N = %d;\nstatic const char *const %s_ROWS[] = {\n" % (url, name, len(m), name))
            for row in m:
                f.write('    "%s",\n' % "".join("1" if c else "0" for c in row))
            f.write("};\n")


if __name__ == "__main__":
    os.makedirs(A("data"), exist_ok=True)
    circle_logo(A("assets", "dev_logo_src.png"), A("data", "dev_logo.bin"), A("assets", "preview_dev_logo.png"))
    circle_logo(A("assets", "patch_logo_src.webp"), A("data", "patch_logo.bin"), A("assets", "preview_patch_logo.png"))
    make_icon()
    make_qr()
    print("tamam")
