"""Render the three watch faces as SVG, following main/ui.c exactly.

The geometry, palette and text positions below are transcribed from ui.c so the
diagrams stay faithful to the firmware. No raster photos, no external assets —
plain SVG that GitHub renders inline.

Usage:  python tools/render_screenshots.py
Output: docs/images/*.svg
"""
import math
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "docs", "images")
os.makedirs(OUT, exist_ok=True)

# ---- palette (ui.c) -------------------------------------------------------
BG, TEXT, MUTED, DIM, RED = "#000000", "#E8E8E8", "#9AA0A6", "#5A6068", "#E10600"
WIFI_OFF, BORDER = "#3A3F45", "#6E747C"
ARC_BG = "#1A1A1E"
TEAM = {
    "MCL": "#FF8700", "RBR": "#3671C6", "FER": "#E8002D", "MER": "#27F4D2",
    "AMR": "#229971", "ALP": "#FF87BC", "WIL": "#64C4FF", "RBU": "#6692FF",
    "HAS": "#B6BABD", "SAU": "#52E252",
}
CX = CY = 233.0
FONT = "Arial, Helvetica, sans-serif"
MONO = "Consolas, 'Courier New', monospace"


def pol(r, deg):
    a = math.radians(deg)
    return CX + r * math.cos(a), CY + r * math.sin(a)


def arc(r, start, end, color, width, cap="butt"):
    """LVGL angles: 0=3 o'clock, clockwise; set_angles(start,end) sweeps +ve."""
    sweep = (end - start) % 360
    if sweep == 0:
        sweep = 360
    x1, y1 = pol(r, start)
    x2, y2 = pol(r, end)
    large = 1 if sweep > 180 else 0
    return (f'<path d="M{x1:.2f} {y1:.2f} A{r:.2f} {r:.2f} 0 {large} 1 '
            f'{x2:.2f} {y2:.2f}" fill="none" stroke="{color}" '
            f'stroke-width="{width}" stroke-linecap="{cap}"/>')


def txt(x, y, s, size, color, *, bold=False, italic=False, mono=False, anchor="middle"):
    style = "font-style:italic;" if italic else ""
    weight = "font-weight:700;" if bold else ""
    return (f'<text x="{x}" y="{y}" fill="{color}" font-size="{size}" '
            f'font-family="{MONO if mono else FONT}" text-anchor="{anchor}" '
            f'style="dominant-baseline:central;{style}{weight}">{s}</text>')


def status_bar():
    """build_status(): time / date / 4 WiFi bars / battery at y=13."""
    o = []
    o.append(txt(178, 25, "07:52", 18, TEXT, bold=True))
    o.append(txt(222, 25, "11/18", 14, MUTED))
    for i in range(4):  # rising bars, active = text colour
        h = 4 + i * 4
        o.append(f'<rect x="{250+i*6}" y="{33-h}" width="3" height="{h}" '
                 f'rx="1" fill="{TEXT}"/>')
    o.append(f'<rect x="279" y="19" width="22" height="12" rx="3" fill="none" '
             f'stroke="{BORDER}" stroke-width="1"/>')
    o.append(f'<rect x="282" y="22" width="13" height="6" rx="1" fill="{TEXT}"/>')
    o.append(f'<rect x="302" y="22" width="3" height="6" rx="1" fill="{BORDER}"/>')
    return "".join(o)


def dots(active):
    o = []
    for i in range(3):
        x = CX + (i - 1) * 14
        o.append(f'<circle cx="{x}" cy="449" r="3" '
                 f'fill="{RED if i == active else WIFI_OFF}"/>')
    return "".join(o)


def badge(color, code):
    return (f'<circle cx="{CX}" cy="{CY}" r="75" fill="{BG}" '
            f'stroke="{color}" stroke-width="3"/>'
            + txt(CX, CY, code, 40, color, bold=True, italic=True))


def bg_arcs(radii, width=9):
    return "".join(arc(r, 0, 360, ARC_BG, width) for r in radii)


def wrap(body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 466 466" '
            f'width="466" height="466">'
            f'<rect width="466" height="466" fill="{BG}"/>'
            f'{body}</svg>')


def save(name, body):
    with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
        f.write(wrap(body))
    print("wrote", name)


# ---- page 1: next session -------------------------------------------------
def page1():
    o = [arc(190, 90 - 329, 90, RED, 3, "round")]  # R21/23 progress arc
    bars = [("MER", 80, TEAM["MER"]), ("FER", 55, TEAM["FER"]), ("RBR", 46, TEAM["RBR"])]
    for i, (code, w, c) in enumerate(bars):
        y = 65 + i * 20
        o.append(f'<rect x="{213-w/2:.1f}" y="{y-3.5}" width="{w}" height="7" '
                 f'rx="4" fill="{c}"/>')
        o.append(txt(273, y, code, 14, MUTED, anchor="start"))
    o.append(txt(CX, 129, "VGS   R21/23", 14, MUTED))
    o.append(badge(RED, "VGS"))
    o.append(txt(CX, 329, "04d 04h 08m", 28, TEXT, bold=True))
    o.append(txt(CX, 359, "LOC 21 NOV 19:00", 14, TEXT))
    o.append(txt(CX, 380, "HOM 22 NOV 03:00", 14, DIM))
    o.append(status_bar())
    o.append(dots(0))
    save("page1-next-session.svg", "".join(o))


# ---- pages 2 / 3 share the five arcs + centre badge -----------------------
def arcs_for(entries):
    """entries: list of (team_code, points) in rank order."""
    radii = [190 - i * 9 for i in range(5)]
    o = [bg_arcs(radii)]
    mx = entries[0][1]
    for r, (code, pts) in zip(radii, entries):
        span = max(4, int(300 * pts / mx))
        o.append(arc(r - 4.5, 90 - span / 2, 90 + span / 2, TEAM[code], 9))
    return "".join(o)


def leader_page(name, entries, badge_color, code, pts, sub, header, expanded, rows):
    o = [arcs_for(entries)]
    if expanded:
        for i, (label, colr) in enumerate(rows):
            o.append(txt(CX, 188 + i * 30, label, 20, colr, mono=True))
    else:
        o.append(txt(CX, 133 if name == "p2" else 129, header[0], 14, MUTED))
        if len(header) > 1:
            o.append(txt(CX, 151, header[1], 14, DIM))
        o.append(badge(badge_color, code))
        o.append(txt(CX, 329, pts, 18, badge_color, bold=True))
        o.append(txt(CX, 357, sub, 14, DIM))
    o.append(status_bar())
    o.append(dots(1 if name == "p2" else 2))
    save(("page2-drivers" if name == "p2" else "page3-constructors")
         + ("-expanded" if expanded else "") + ".svg", "".join(o))


DRV = [("MER", 390), ("RBR", 279), ("MER", 262), ("FER", 261), ("FER", 249)]
TEAMS = [("MER", 652), ("FER", 451), ("RBR", 373), ("MCL", 362), ("AMR", 94)]

page1()
leader_page("p2", DRV, TEAM["MER"], "ANT", "390 PTS", "DRIVERS",
            ("Mercedes",), False, None)
leader_page("p2", DRV, TEAM["MER"], "ANT", "390 PTS", "DRIVERS",
            ("Mercedes",), True, [
                ("P2  VER  279", TEAM["RBR"]),
                ("P3  RUS  262", TEAM["MER"]),
                ("P4  HAM  261", TEAM["FER"]),
                ("P5  LEC  249", TEAM["FER"]),
            ])
leader_page("p3", TEAMS, TEAM["MER"], "MER", "652 PTS", "CONSTRUCTORS",
            ("Brazilian Grand Prix", "WIN NOR   FAST ANT"), False, None)
leader_page("p3", TEAMS, TEAM["MER"], "MER", "652 PTS", "CONSTRUCTORS",
            ("Brazilian Grand Prix", "WIN NOR   FAST ANT"), True, [
                ("P2  FER  451", TEAM["FER"]),
                ("P3  RBR  373", TEAM["RBR"]),
                ("P4  MCL  362", TEAM["MCL"]),
                ("P5  AMR   94", TEAM["AMR"]),
            ])
