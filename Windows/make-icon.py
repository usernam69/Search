# The app's icon, drawn rather than exported: Drice's Subtract.svg, read from
# its own path data so the mark stays crisp at every size instead of a raster
# scaled up.
#
# A Windows .ico holds several sizes in one file, so each is drawn at its own
# size: the pill and the S it has cut out of it, on its plate.
#
#   python make-icon.py            writes src/search.ico

from PIL import Image, ImageDraw
import os, re

# Drice's Subtract.svg (22 September 2026): the logomark's own path.
MARK = (
    "M469.443 0C545.471 0.00013198 607.103 61.6325 607.104 137.66"
    "C607.104 213.688 545.471 275.321 469.443 275.321H137.66"
    "C61.6323 275.321 0 213.688 0 137.66C0.00016085 61.6325 61.6325 0.000140192 137.66 0"
    "H469.443ZM138.104 51.5977C127.234 51.5977 117.512 53.5115 108.938 57.3389"
    "C100.518 61.0132 93.8581 66.2188 88.959 72.9551C84.2132 79.5381 81.8398 87.3464 81.8398 96.3789"
    "C81.8399 105.258 83.6773 112.607 87.3516 118.425C91.0258 124.089 95.9251 128.682 102.049 132.203"
    "C108.173 135.571 114.833 138.327 122.028 140.471L151.652 149.197"
    "C158.389 151.188 163.9 154.249 168.187 158.383C172.473 162.516 174.617 168.028 174.617 174.917"
    "C174.617 182.572 171.402 188.849 164.972 193.748C158.695 198.494 150.122 200.867 139.252 200.867"
    "C132.21 200.867 125.702 199.413 119.731 196.504C113.914 193.442 109.091 189.308 105.264 184.103"
    "C101.436 178.744 99.2169 172.697 98.6045 165.961H97.6855L75.4102 171.013"
    "C76.3287 180.658 79.697 189.308 85.5146 196.963C91.3322 204.618 98.9103 210.665 108.249 215.104"
    "C117.741 219.544 128.076 221.765 139.252 221.765C151.193 221.765 161.68 219.774 170.713 215.794"
    "C179.746 211.813 186.711 206.225 191.61 199.029C196.662 191.834 199.188 183.414 199.188 173.769"
    "C199.188 164.124 197.352 156.239 193.678 150.115C190.003 143.838 185.104 138.863 178.98 135.188"
    "C172.857 131.514 166.044 128.605 158.542 126.462L128.229 117.735"
    "C121.799 115.898 116.516 113.219 112.383 109.698C108.402 106.177 106.412 101.354 106.412 95.2305"
    "C106.412 88.188 109.168 82.6758 114.68 78.6953C120.344 74.5619 128.152 72.4951 138.104 72.4951"
    "C147.901 72.4952 155.939 74.9448 162.216 79.8438C168.493 84.7428 172.397 91.1729 173.928 99.1338"
    "H174.847L196.663 93.8525C195.745 85.5853 192.605 78.3131 187.247 72.0361"
    "C181.889 65.6061 174.923 60.6306 166.35 57.1094C157.929 53.4351 148.514 51.5977 138.104 51.5977Z"
)

CANVAS = (608.0, 276.0)

# The path is M, L, H, V, C and Z — what Figma writes for a flattened shape,
# and nothing else. Curves are flattened into short straight runs, which at
# 256 px is under a pixel each.
def subpaths(d):
    """The path as a list of contours. The mark is one path with two: the
    pill, then the S cut out of it — so the second is filled in the plate's
    colour, and the S shows through."""
    tokens = re.findall(r"[MLHVCZmlhvcz]|-?\d*\.?\d+(?:e-?\d+)?", d)
    out, cmd, nums = [], None, []

    def flush():
        nonlocal cmd
        if cmd is not None and nums:
            out.append((cmd, list(nums)))
        cmd = None

    for t in tokens:
        if t.isalpha():
            flush()
            cmd = t
        else:
            nums.append(float(t))
    flush()

    contours, pts, pen = [], [], (0.0, 0.0)
    for c, n in out:
        if c in "Mm":
            if pts:
                contours.append(pts)
            pts, pen = [], (0.0, 0.0)
            k = 0
            while k + 1 < len(n):
                pen = (n[k], n[k + 1]); pts.append(pen); k += 2
        elif c in "Ll":
            k = 0
            while k + 1 < len(n):
                pen = (n[k], n[k + 1]); pts.append(pen); k += 2
        elif c in "Hh":
            for v in n:
                pen = (v, pen[1]); pts.append(pen)
        elif c in "Vv":
            for v in n:
                pen = (pen[0], v); pts.append(pen)
        elif c in "Cc":
            k = 0
            while k + 5 < len(n):
                p0 = pen
                p1 = (n[k], n[k + 1]); p2 = (n[k + 2], n[k + 3]); p3 = (n[k + 4], n[k + 5])
                steps = 24
                for s in range(1, steps + 1):
                    t = s / steps
                    u = 1 - t
                    x = u*u*u*p0[0] + 3*u*u*t*p1[0] + 3*u*t*t*p2[0] + t*t*t*p3[0]
                    y = u*u*u*p0[1] + 3*u*u*t*p1[1] + 3*u*t*t*p2[1] + t*t*t*p3[1]
                    pts.append((x, y))
                pen = p3
                k += 6
        elif c in "Zz":
            if pts:
                pts.append(pts[0])
    if pts:
        contours.append(pts)
    return contours


def draw(size):
    s = 4  # draw large, scale down: cheap antialiasing at 16 px
    W = size * s
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # The plate sits inside a rounded square with air around it. Windows icons
    # fill more of their box, so the air is small.
    plate = W * 0.92
    off = (W - plate) / 2
    d.rounded_rectangle([off, off, off + plate, off + plate], radius=plate * 0.22, fill=(255, 255, 255, 255))

    # The mark, scaled to sit in the plate.
    mark_w = plate * 0.74
    scale = mark_w / CANVAS[0]
    mark_h = CANVAS[1] * scale
    ox = off + (plate - mark_w) / 2
    oy = off + (plate - mark_h) / 2

    # The pill, then the S cut out of it: the plate showing through.
    d.rounded_rectangle([ox, oy, ox + mark_w, oy + mark_h], radius=mark_h / 2, fill=(10, 10, 10, 255))
    contours = [[(ox + x * scale, oy + y * scale) for x, y in c] for c in subpaths(MARK)]
    for contour in contours[1:]:
        d.polygon(contour, fill=(255, 255, 255, 255))

    return img.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "src", "search.ico")
    sizes = [16, 24, 32, 48, 64, 128, 256]
    imgs = [draw(n) for n in sizes]
    imgs[-1].save(out, format="ICO", sizes=[(n, n) for n in sizes], append_images=imgs[:-1])
    print("wrote", out)
