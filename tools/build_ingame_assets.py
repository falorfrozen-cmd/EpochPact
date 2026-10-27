"""Build the in-game interface's data, images and fonts from the desktop interface's own sources.

Everything the in-game window draws comes from the same files the desktop app uses: the catalog
(ui/catalog.json), its English texts (ui/locale-en.json), the translations (ui/i18n-*.json), the
page composition (tools/ingame_pages.py, mirroring app.js) and the theme's colours, sizes and
artwork (ui/style.css, ui/assets). Images are composed here at twice the 1440x900 layout size, the
way style.css paints them, so the game only scales and draws them.

Output (--out, default native/build/ingame): ui.json, PNG/JPEG images, two static Inter fonts and
ingame.rc, which native/build.bat links into the player loader and the cores as RCDATA.

usage: py -3 tools/build_ingame_assets.py [--out DIR]
"""
from __future__ import annotations

import argparse
import hashlib
import io
import os
import json
import math
import re
import subprocess
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

try:
    from tools import ingame_pages
except ImportError:  # run as a script
    import ingame_pages  # type: ignore

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "ui"
ASSETS = UI / "assets"
S = 2          # export scale: images are made at twice the 1440x900 layout size
SS = 4         # supersampling for shapes before they are reduced to S
VERSION = 1

# :root of ui/style.css (Chronoforge). Void Atlas comes later with its own layout.
THEME = {"bg": "#07120f", "panel": "#0b1815", "ink": "#eee6d5", "muted": "#b3beb8", "gold": "#ac8950", "line": "#665436",
         "accent": "#80e2b1", "glow": "#2a9a70", "soft": "#183c2e"}

# Words app.js shows next to a value (unitFor) and for a lifetime (lifetime).
LIFETIME = {"session": "Session", "actor": "Character", "game_save": "Game save", "read_only": "Read only", "view": "View", "recovery": "Recovery"}
NO_UNIT = {"lprolls", "craft_fp", "loot_ww", "build_weaverflat", "build_projectileflat", "build_blessingoptions", "build_minionextra", "build_totemextra",
           "build_ballistaextra", "build_forgedextra", "build_companionextra"}
AUTOMATIC_TEMPLATES = ("buildset ", "qolset ", "qolarea ", "pickupradius ", "pickupinterval ", "autopickup ", "mapreveal ", "mapset ", "tierset ",
                       "fontset ", "effectsset ", "deathrecap ", "rageset ", "invset ")
# Values the catalog does not name a source for: where app.js reads them.
EXTRA_SOURCES = {
    "pickup_radius": ("pickupradius", "radius"), "pickup_interval": ("pickupradius", "intervalSeconds"),
    "tier_icons": ("tierread", "icons"), "tier_tooltips": ("tierread", "tooltips"), "tier_lines": ("tierread", "lines"),
    "tier_rules": ("tierread", "rules"), "tier_rule_position": ("tierread", "rulePosition"), "tier_rule_parts": ("tierread", "ruleParts"),
    "tier_filter_colors": ("tierread", "filterColors"), "tier_name_colors": ("tierread", "nameColors"), "tier_special": ("tierread", "special"),
    "map_minimapsize": ("mapsettings", "minimapSize"), "map_minimapzoom": ("mapsettings", "minimapZoom"),
    "map_minimapopacity": ("mapsettings", "minimapOpacity"), "map_overlayopacity": ("mapsettings", "overlayOpacity"),
    "map_overlayzoom": ("mapsettings", "overlayZoom"),
    "inventory_autosort": ("invread", "autosort"), "inventory_autosortall": ("invread", "autosortAll"), "inventory_sellkeep": ("invread", "sellKeep"),
}
# Reads whose results are JSON objects the panel can follow a path into.
JSON_READS = {"buildread", "qolread", "rageread", "effectsread", "deathrecap", "pickupradius", "tierread", "mapsettings", "invread", "fontread",
              "craftread"}
# Interface texts the desktop app does not have; translated here (the app's own strings come from ui/i18n-*.json).
OWN_TEXTS = {
    "This section is not in the in-game window yet. Use the EpochPact app for it.": {
        "tr": "Bu bölüm henüz oyun içi pencerede yok. Bunun için EpochPact uygulamasını kullanın.",
        "ru": "Этого раздела пока нет в игровом окне. Используйте для него приложение EpochPact."},
    "F6 closes this window.": {"tr": "F6 bu pencereyi kapatır.", "ru": "F6 закрывает это окно."},
    "In game": {"tr": "Oyunda", "ru": "В игре"},
    "Window": {"tr": "Pencere", "ru": "Окно"},
    "Full screen": {"tr": "Tam ekran", "ru": "Во весь экран"},
    "Left half": {"tr": "Sol yarı", "ru": "Левая половина"},
    "Right half": {"tr": "Sağ yarı", "ru": "Правая половина"},
    "Default size": {"tr": "Varsayılan boyut", "ru": "Обычный размер"},
}


# ---------------------------------------------------------------------------------------- colours
def rgba(hex_colour: str) -> tuple[int, int, int, int]:
    h = hex_colour.lstrip("#")
    if len(h) == 6:
        h += "ff"
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4, 6))  # type: ignore[return-value]


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(4))


def premultiplied_lerp(a, b, t):
    """CSS gradients interpolate in premultiplied alpha."""
    pa = [a[i] * a[3] / 255 for i in range(3)] + [a[3]]
    pb = [b[i] * b[3] / 255 for i in range(3)] + [b[3]]
    p = [pa[i] + (pb[i] - pa[i]) * t for i in range(4)]
    alpha = p[3]
    if alpha <= 0:
        return (0, 0, 0, 0)
    return (round(p[0] * 255 / alpha), round(p[1] * 255 / alpha), round(p[2] * 255 / alpha), round(alpha))


def stops_colour(stops, t):
    """stops: [(position 0..1, rgba)] as in a CSS gradient."""
    if t <= stops[0][0]:
        return stops[0][1]
    for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
        if t <= p1:
            return premultiplied_lerp(c0, c1, 0 if p1 == p0 else (t - p0) / (p1 - p0))
    return stops[-1][1]


def linear_gradient(width, height, angle_deg, stops):
    """CSS linear-gradient(angle, stops) over a width x height box."""
    angle = math.radians(angle_deg)
    dx, dy = math.sin(angle), -math.cos(angle)
    length = abs(width * dx) + abs(height * dy)
    image = Image.new("RGBA", (width, height))
    pixels = image.load()
    cache = {}
    for y in range(height):
        for x in range(width):
            t = ((x + 0.5 - width / 2) * dx + (y + 0.5 - height / 2) * dy) / length + 0.5
            key = round(t * 1024)
            colour = cache.get(key)
            if colour is None:
                colour = cache[key] = stops_colour(stops, key / 1024)
            pixels[x, y] = colour
    return image


def radial_gradient(width, height, cx, cy, rx, ry, stops):
    image = Image.new("RGBA", (width, height))
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            d = math.hypot((x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry)
            pixels[x, y] = stops_colour(stops, d)
    return image


def ellipse_mask(width, height, solid, clear):
    """mask-image: radial-gradient(ellipse, black solid, transparent clear) (farthest-corner)."""
    rx, ry = width / 2 * math.sqrt(2), height / 2 * math.sqrt(2)
    mask = Image.new("L", (width, height))
    pixels = mask.load()
    for y in range(height):
        for x in range(width):
            d = math.hypot((x + 0.5 - width / 2) / rx, (y + 0.5 - height / 2) / ry)
            pixels[x, y] = 255 if d <= solid else 0 if d >= clear else round(255 * (clear - d) / (clear - solid))
    return mask


def solid(width, height, colour):
    return Image.new("RGBA", (width, height), rgba(colour) if isinstance(colour, str) else colour)


# ------------------------------------------------------------------------------------ sprites
def shape(size_css, draw, scale=S):
    """Draw at SS times the export size, then reduce: smooth edges at any radius."""
    w, h = size_css
    big = Image.new("RGBA", (w * scale * SS, h * scale * SS), (0, 0, 0, 0))
    draw(ImageDraw.Draw(big), scale * SS)
    return big.resize((w * scale, h * scale), Image.LANCZOS)


def frame_sprite(radius, stroke=1.0, fill=False):
    """White rounded frame (or fill), 9-sliced at runtime. Returns (image, slice in css px)."""
    edge = int(max(radius, math.ceil(stroke))) + 2
    side = edge * 2 + 2

    def draw(d, k):
        box = [0, 0, side * k - 1, side * k - 1]
        if fill:
            d.rounded_rectangle(box, radius=radius * k, fill=(255, 255, 255, 255))
        else:
            d.rounded_rectangle(box, radius=radius * k, outline=(255, 255, 255, 255), width=max(1, round(stroke * k)))
    return shape((side, side), draw), edge


def band_sprite(width):
    """box-shadow: inset 0 0 0 <width>px: a white band inside the edge."""
    side = width * 2 + 2

    def draw(d, k):
        d.rectangle([0, 0, side * k - 1, side * k - 1], fill=(255, 255, 255, 255))
        d.rectangle([width * k, width * k, (side - width) * k - 1, (side - width) * k - 1], fill=(0, 0, 0, 0))
    return shape((side, side), draw), width


def corner_sprite():
    """.panel::before: a 13px corner of 1px gold lines (top-left; the other corner is rotated)."""
    def draw(d, k):
        d.rectangle([0, 0, 13 * k - 1, k - 1], fill=(255, 255, 255, 255))
        d.rectangle([0, 0, k - 1, 13 * k - 1], fill=(255, 255, 255, 255))
    return shape((13, 13), draw)


def circle_sprite(diameter, ring=False):
    def draw(d, k):
        box = [0, 0, diameter * k - 1, diameter * k - 1]
        if ring:
            d.ellipse(box, outline=(255, 255, 255, 255), width=k)
        else:
            d.ellipse(box, fill=(255, 255, 255, 255))
    return shape((diameter, diameter), draw)


def track_sprite():
    """input[type=range]: 8px tall, 1px gold border, radius 8, linear-gradient(90deg, glow, #343a35).
    Made 256 css px wide and stretched to each slider's width (a smooth horizontal gradient)."""
    width, height = 256, 8
    fill = linear_gradient(width * S, height * S, 90, [(0, rgba(THEME["glow"])), (1, rgba("#343a35"))])
    mask = shape((width, height), lambda d, k: d.rounded_rectangle([0, 0, width * k - 1, height * k - 1], radius=height / 2 * k, fill=(255, 255, 255, 255)))
    border = shape((width, height), lambda d, k: d.rounded_rectangle([0, 0, width * k - 1, height * k - 1], radius=height / 2 * k,
                                                                    outline=rgba(THEME["gold"]), width=k))
    image = Image.new("RGBA", fill.size)
    image.paste(fill, (0, 0), mask.split()[3])
    image.alpha_composite(border)
    return image


def thumb_sprite():
    """::-webkit-slider-thumb: 19px, rotate(45deg), accent fill, 3px double #775329 border,
    radius 3, box-shadow 0 0 8px glow. The image is 44 css px square, centred on the thumb."""
    canvas = 44
    k = S * SS
    big = Image.new("RGBA", (canvas * k, canvas * k), (0, 0, 0, 0))
    square = Image.new("RGBA", (19 * k, 19 * k), (0, 0, 0, 0))
    d = ImageDraw.Draw(square)
    d.rounded_rectangle([0, 0, 19 * k - 1, 19 * k - 1], radius=3 * k, fill=rgba("#775329"))           # outer line
    d.rounded_rectangle([k, k, 18 * k - 1, 18 * k - 1], radius=2 * k, fill=rgba(THEME["accent"]))     # gap shows the background
    d.rounded_rectangle([2 * k, 2 * k, 17 * k - 1, 17 * k - 1], radius=1 * k, fill=rgba("#775329"))  # inner line
    d.rounded_rectangle([3 * k, 3 * k, 16 * k - 1, 16 * k - 1], radius=1 * k, fill=rgba(THEME["accent"]))
    # The double border's gap is the element's background colour (the accent): as Chromium draws it.
    rotated = square.rotate(45, resample=Image.BICUBIC, expand=True)
    glow = Image.new("RGBA", rotated.size, rgba(THEME["glow"]))
    glow.putalpha(rotated.split()[3])
    glow_canvas = Image.new("RGBA", big.size, (0, 0, 0, 0))
    offset = ((big.width - rotated.width) // 2, (big.height - rotated.height) // 2)
    glow_canvas.paste(glow, offset, glow)
    glow_canvas = glow_canvas.filter(ImageFilter.GaussianBlur(4 * k))   # 8px blur radius = sigma 4
    big.alpha_composite(glow_canvas)
    big.alpha_composite(rotated, offset)
    return big.resize((canvas * S, canvas * S), Image.LANCZOS)


# ------------------------------------------------------------------------------------ artwork
def reference_crop(image, scaled_size, position, size, solid_at, clear_at):
    """A sprite cut from the reference sheet the way style.css does with background-size and
    -position, faded at the edges by its radial mask-image."""
    big = image.resize((scaled_size[0] * S, scaled_size[1] * S), Image.LANCZOS)
    x, y = -position[0] * S, -position[1] * S
    crop = big.crop((x, y, x + size[0] * S, y + size[1] * S)).convert("RGBA")
    crop.putalpha(ImageChops.multiply(crop.split()[3], ellipse_mask(size[0] * S, size[1] * S, solid_at, clear_at)))
    return crop


def cover(art, width, height, y_position):
    """background-size: cover; background-position: center <y_position>."""
    scale = max(width / art.width, height / art.height)
    w, h = math.ceil(art.width * scale), math.ceil(art.height * scale)
    resized = art.resize((w, h), Image.LANCZOS)
    left, top = round((width - w) * 0.5), round((height - h) * y_position)
    out = Image.new("RGB", (width, height))
    out.paste(resized, (left, top))
    return out.convert("RGBA")


def hero_image(art, height_css):
    """.hero (#07100d) under .hero-art: gradient over the art, the whole layer at opacity .85.
    .hero-art fills the hero inside its 1px bottom border: 225px, or 155px on a page with tabs."""
    width, height = 1185 * S, height_css * S   # the main area less its 15px scrollbar
    layer = cover(art, width, height, 0.48)
    layer.alpha_composite(linear_gradient(width, height, 90, [(0.05, rgba("#08120fff")), (0.38, rgba("#08120fca")), (0.72, (8, 18, 15, 0))]))
    base = solid(width, height, "#07100d")
    return Image.blend(base, layer, 0.85).convert("RGB")


def sidebar_image(art):
    """.sidebar: radial-gradient(at bottom) over linear-gradient(#080f0df5,#081410e8) over the art
    (background-size: auto 100%, at the top left)."""
    width, height = 238 * S, 898 * S
    scale = height / art.height
    layer = Image.new("RGBA", (width, height))
    layer.paste(art.resize((math.ceil(art.width * scale), height), Image.LANCZOS).convert("RGBA"), (0, 0))
    layer.alpha_composite(linear_gradient(width, height, 180, [(0, rgba("#080f0df5")), (1, rgba("#081410e8"))]))
    rx, ry = 119 * S * math.sqrt(2), 898 * S * math.sqrt(2)
    layer.alpha_composite(radial_gradient(width // 4, height // 4, width / 8, height / 4, rx / 4, ry / 4,
                                          [(0, rgba("#1a45332b")), (1, (26, 69, 51, 0))]).resize((width, height), Image.BICUBIC))
    return layer.convert("RGB")


def main_background():
    """.main-scroll: radial-gradient(ellipse at 70% 0%, soft, transparent 50%) over the page colour.
    Smooth, so it is made at a quarter size and stretched."""
    width, height = 300, 187       # 1200 x 748 / 4
    base = solid(width, height, THEME["bg"])
    soft = rgba(THEME["soft"])
    base.alpha_composite(radial_gradient(width, height, 0.7 * width, 0, 0.7 * width * math.sqrt(2), height * math.sqrt(2),
                                         [(0, soft), (0.5, soft[:3] + (0,))]))
    return base.convert("RGB")


def density_icon(source):
    """.icon.density: the density artwork, contain, centred in 55x61, no mask."""
    size = (55 * S, 61 * S)
    image = Image.new("RGBA", size, (0, 0, 0, 0))
    side = 55 * S
    image.paste(source.resize((side, side), Image.LANCZOS), (0, (size[1] - side) // 2))
    return image


# ------------------------------------------------------------------------------------- fonts
def inter_instance(weight):
    """A static Inter of one weight (opsz 14, the text sizes used), limited to the scripts the
    interface shows: Latin with Turkish, Cyrillic, punctuation, arrows and the maths signs."""
    from fontTools.ttLib import TTFont
    from fontTools.varLib import instancer
    from fontTools import subset
    font = instancer.instantiateVariableFont(TTFont(ASSETS / "inter.ttf"), {"wght": weight, "opsz": 14})
    ranges = [(0x20, 0x7E), (0xA0, 0x17F), (0x400, 0x4FF), (0x2000, 0x206F), (0x20A0, 0x20BF), (0x2190, 0x21FF), (0x2200, 0x22FF),
              (0x2300, 0x23FF), (0x25A0, 0x25FF), (0x2600, 0x26FF), (0x2700, 0x27BF)]  # symbols Inter has, as the browser uses them
    options = subset.Options()
    options.layout_features = ["kern", "liga", "calt", "tnum"]
    options.name_IDs = ["*"]
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=[c for low, high in ranges for c in range(low, high + 1)])
    subsetter.subset(font)
    buffer = io.BytesIO()
    font.save(buffer)
    return buffer.getvalue()


def text_advances(faces):
    """Each character's advance in em for the faces the window draws (Inter as embedded, Georgia
    from Windows as the game loads it): the window lays out table columns from text widths as the
    browser's automatic table layout does, without asking the game to measure every cell."""
    from fontTools.ttLib import TTFont
    out = {}
    sources = dict(faces)
    windows = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts"
    for face, file in (("serif", "georgia.ttf"), ("serifBold", "georgiab.ttf")):
        if (windows / file).is_file():
            sources[face] = (windows / file).read_bytes()
    for face, font_bytes in sources.items():
        font = TTFont(io.BytesIO(font_bytes))
        em, metrics = font["head"].unitsPerEm, font["hmtx"]
        out[face] = {str(cp): round(metrics[glyph][0] / em, 4) for cp, glyph in font.getBestCmap().items() if cp < 0x3000}
    return out


# -------------------------------------------------------------------------------------- data
def js_number(value):
    """String(value) as JavaScript writes it."""
    if value is None:
        return None
    if isinstance(value, bool):
        return "true" if value else "false"
    if float(value).is_integer():
        return str(int(value))
    return repr(float(value))


def unit_for(c):
    i = c["id"]
    if i == "weaversdrop":
        return "%"
    if i in ("pickup_radius", "qol_arearadius"):
        return " game units"
    if i == "pickup_interval":
        return " s"
    if i.startswith("map_") and i.endswith("opacity"):
        return ""
    if i.startswith("map_"):
        return "×"
    if i == "build_cameraangle":
        return "°"
    if i.startswith("cof_double_"):
        return "%"
    if i == "loot_lp":
        return " LP"
    if i in NO_UNIT or c["widget"] == "toggle":
        return ""
    return "×"


def icon_for(control_id):
    if control_id == "speed":
        return "speed"
    if control_id == "gold":
        return "gold"
    if control_id in ("drops", "rarity"):
        return "drops"
    if control_id == "density":
        return "density"
    if control_id in ("cooldown", "autopickup"):
        return "cooldown"
    return "default"


def source_for(c):
    if c["id"] in EXTRA_SOURCES:
        read, path = EXTRA_SOURCES[c["id"]]
        return {"read": read, "path": path}
    source = c.get("currentSource") or ""
    if source.startswith("currentValues."):
        return {"read": "status", "path": source.split(".", 1)[1]}
    if source.startswith("liveState."):
        read, _, path = source.split(".", 1)[1].partition(".")
        if read in JSON_READS and "[" not in path:
            return {"read": read, "path": path}
    return None


def automatic(page, c):
    """automaticSetting() of app.js for a control shown on `page`."""
    template = c.get("commandTemplate") or ""
    return bool(((page in ("general", "overview") and c["group"] == "general") or template.startswith(AUTOMATIC_TEMPLATES))
                and c["widget"] in ("number", "toggle") and c["lifetime"] in ("session", "actor"))


def control_entry(c, english):
    label, description = english.get(c["id"], (c["id"], ""))
    value = next((p for p in c.get("parameters", []) if p["name"] == "value"), {})
    entry = {"id": c["id"], "widget": "choice" if value.get("choices") else c["widget"], "label": label, "description": description,
             "lifetime": c["lifetime"], "command": c.get("commandTemplate"), "reset": c.get("resetCommand"),
             "default": c.get("default"), "unit": unit_for(c), "icon": icon_for(c["id"])}
    if c["widget"] == "number":
        entry.update(min=value.get("minimum"), max=value.get("maximum"), integer=value.get("type") == "integer")
    if value.get("choices"):
        entry["choices"] = value["choices"]
        entry["choiceLabels"] = value.get("choiceLabels") or [("Normal game choices" if x == 0 else f"{x} choices") for x in value["choices"]]
    if c["widget"] == "toggle":
        entry["defaultText"] = "Default: on" if c.get("default") else "Default: off"
    elif c["widget"] == "number" and not value.get("choices"):
        default = js_number(c.get("default")) if c.get("default") is not None else "rank bonus"
        entry["defaultText"] = f"{js_number(value.get('minimum'))}–{js_number(value.get('maximum'))}{entry['unit']} · Default: {default}"
    source = source_for(c)
    if source:
        entry["source"] = source
    entry["params"] = [{k: p[k] for k in ("name", "type", "minimum", "maximum", "choices", "choiceLabels") if k in p}
                       for p in c.get("parameters", [])]
    return entry


def inter_cmap():
    """The characters Inter (ui/assets/inter.ttf, the desktop's font) has."""
    if not hasattr(inter_cmap, "cache"):
        from fontTools.ttLib import TTFont
        inter_cmap.cache = set(TTFont(ASSETS / "inter.ttf").getBestCmap())
    return inter_cmap.cache


def build_data():
    catalog = json.loads((UI / "catalog.json").read_text(encoding="utf-8"))
    english = json.loads((UI / "locale-en.json").read_text(encoding="utf-8"))["controls"]
    controls = catalog["controls"]
    by_id = {c["id"]: c for c in controls}
    pages, used = [], {}
    for page_id, nav, symbol, title, subtitle in ingame_pages.PAGES:
        sections = ingame_pages.sections(page_id, controls, catalog["counts"])
        panel_names = {"recent_pickups": ("loot-log", "Recent pickups"), "collect_now": ("", "Collect now"),
                       "pickup_rules": ("", "Pickup rules"), "pickup_selections": ("", "Pickup selections"),
                       "filter_files": ("filter-files", "Loot filter files"), "stats_filters": ("", "Character bonuses"),
                       "skill_budgets": ("", "Skill and point budgets"), "free_respec": ("", "Free respec"),
                       "summon_allies": ("", "Live summoned allies"), "damage_meter": ("damage-meter", "Damage meter"),
                       "minion_bonuses": ("", "Minion and companion bonuses"),
                       "inventory_shortcuts": ("", "Inventory shortcuts"), "stash_rules": ("", "Stash tabs from loot filter"),
                       "campaign": ("", "Campaign quests"), "mastery": ("", "Mastery"), "monolith_management": ("", "Monolith management"),
                       "collection_head": ("collection", "Unique Atlas" if page_id == "atlas" else "Stash Assistant"),
                       "echo_head": ("navigator", "Monolith Navigator"), "ws_head": ("custom-workshop", "Create custom item"),
                       "stash_search_help": ("", "In-game stash search"), "craft_materials": ("", "Crafting materials"),
                       "glyph_chances": ("", "Glyph chances"), "factions": ("", "Faction data")}
        group_parts = {}
        for section in sections:
            section_title, identifier = section.get("title", ""), ""
            if section.get("native") in panel_names:
                identifier, section_title = panel_names[section["native"]]
            if section.get("group") and section["group"] in group_parts:
                section.update(group_parts[section["group"]]); continue
            if section_title and page_id != "settings":
                part = {"partKey": "panel:" + (identifier or page_id + ":" + section_title), "partTitle": section_title}
                section.update(part)
                if section.get("group"): group_parts[section["group"]] = part
        sections.append(ingame_pages.native("global_search"))
        for section in sections:
            if section["kind"] == "native":
                for i in section.get("controls", []):
                    used[i] = by_id[i]
            if section["kind"] != "settings":
                continue
            section["automatic"] = [automatic(page_id, by_id[i]) for i in section["controls"]]
            for i in section["controls"] + section.get("tailControls", []):
                used[i] = by_id[i]
        # the face the browser draws the navigation symbol with: Inter when Inter has it, else the
        # system fallback (Segoe UI Symbol)
        face = "body" if all(ord(ch) in inter_cmap() for ch in symbol) else "symbol"
        # a page with tabs: its first tab, the tab whose drafts Apply changes sends, its hero
        pages.append({**ingame_pages.PAGE_EXTRA.get(page_id, {}), "id": page_id, "nav": nav, "symbol": symbol, "symbolFace": face, "title": title, "subtitle": subtitle,
                      "sections": sections})
    data = {"version": VERSION, "reference": [1440, 900], "theme": THEME, "lifetimes": LIFETIME, "pages": pages,
            "controls": {i: control_entry(c, english) for i, c in used.items()}}
    data["counts"] = catalog["counts"]
    data["searchControls"] = [dict(control_entry(c, english), group=c["group"]) for c in controls
                              if c["id"] not in ingame_pages.DEVELOPER_CONTROLS]
    # Profiles may contain a session control whose page has not been visited yet.
    for c in controls:
        if c["lifetime"] in ("session", "actor") and c["widget"] in ("number", "toggle"):
            data["controls"].setdefault(c["id"], control_entry(c, english))
    data["campaignQuests"] = catalog["optionCatalogs"]["campaignQuests"]  # the static coverage table of Campaign
    data["lensCatalog"] = catalog["optionCatalogs"]["lenses"]  # the Circle of Fortune lens catalog before a live read
    # the names the Custom Items workshop builds its extra effects' names from (workshopModifierEntries)
    stats = catalog["characterStats"]
    data["characterStats"] = {k: [{"id": x["id"], "name": x["name"]} for x in stats[k]] for k in ("properties", "tags", "ailments")}
    # Browser names, keys, units and bounds come from the same stat-model.js the app uses.
    result = subprocess.run(["node", str(ROOT / "tools/ingame_stat_catalog.js")], capture_output=True, check=True)
    data["statBrowser"] = json.loads(result.stdout.decode("utf-8"))
    data["appVersion"] = re.search(r'#define EP_VERSION_STR "([^"]+)"', (ROOT / "native/version_info.h").read_text(encoding="utf-8"))[1]
    data["i18n"] = translations(data)
    # every character the window's texts use in each language: the fonts' atlases are filled with
    # them in the background, so no text waits for a new glyph later
    english = set("".join(texts_of(data))) | {chr(c) for c in range(0x20, 0x7F)}
    data["charset"] = {"en": "".join(sorted(c for c in english if c >= " "))}
    for language in ("tr", "ru"):
        words = english | set("".join(data["i18n"][language].values()))
        data["charset"][language] = "".join(sorted(c for c in words if c >= " "))
    # the app's dictionaries as written, for the texts the window composes while it runs
    # (native/core/ingame_i18n.cpp translates them the way ui/i18n.js does)
    data["dictionaries"] = {language: json.loads((UI / f"i18n-{language}.json").read_text(encoding="utf-8")) for language in ("tr", "ru")}
    return data


def texts_of(data):
    """Every text the in-game window shows, as the desktop app shows it (whole text nodes)."""
    texts = set(LIFETIME.values())
    texts |= {"LAST EPOCH · OFFLINE TOOLS", "Rewrite the past.", "Shape your journey.", "Created by Falor", "Support · Discord",
              "Customize navigation", "↻ Refresh", "Refresh", "Find a setting…", "CHRONOFORGE", "Checking connection", "● Offline · InGame",
              "● InGame", "Changes apply automatically", "Use Apply changes below", "Default", "On", "Off", "Applying…", "Applied", "Draft",
              "Ready", "A setting needs attention", "Applying changes…", "{0} settings in draft", "Save actions use their own buttons.",
              "Apply changes", "Discard draft", "Rank", "⚙−☠▶Ⅱ↺⌃⌄◢×"}
    by_id = {p["id"]: p for p in data["pages"]}
    for page in data["pages"]:
        texts |= {page["nav"], page["title"], page["subtitle"]}  # the breadcrumb is "EpochPact / " + the translated nav
        for section in page["sections"]:
            texts |= {v for k, v in section.items() if k in ("title", "text", "note") and isinstance(v, str)}
            if section.get("details"):
                texts.add(section["details"]["summary"])
            for item in section.get("items", []):
                texts.add(item[1])
            for target in section.get("pages", []):
                texts.add(f"{by_id[target]['symbol']} {by_id[target]['nav']}")
            for override in section.get("overrides", {}).values():
                texts |= set(override.values())
    for c in data["controls"].values():
        texts |= {c["label"], c["description"], c["unit"].strip(), c.get("defaultText", ""), *c.get("choiceLabels", [])}
    texts |= native_texts()
    for rows in data.get("statBrowser", {}).values():
        for row in rows:
            texts |= {row["name"], row["category"], *row["shared"]}
    return sorted(t for t in texts if t)


def native_texts():
    """The English texts the native window code passes to Tr() / Fmt(): a literal argument, or
    both literal branches of a ?: argument."""
    import codecs
    found = set()

    literal = r'"(?:[^"\\]|\\.)*"'

    def literals(code):
        return [codecs.escape_decode(m[1:-1].encode("utf-8"))[0] for m in re.findall(literal, code)]

    for source in sorted((ROOT / "native" / "core").glob("ingame_*.cpp")):
        code = source.read_text(encoding="utf-8")
        for start in (m.end() for m in re.finditer(r"\b(?:Tr|Fmt)\(", code)):
            # the first argument: up to the matching ")" or a top-level ","
            depth, i, quote = 0, start, False
            while i < len(code):
                ch = code[i]
                if quote:
                    if ch == "\\":
                        i += 1
                    elif ch == '"':
                        quote = False
                elif ch == '"':
                    quote = True
                elif ch in "([{":
                    depth += 1
                elif ch in ")]}":
                    if depth == 0:
                        break
                    depth -= 1
                elif ch == "," and depth == 0:
                    break
                i += 1
            argument = code[start:i].strip()
            if re.fullmatch(r"(?:" + literal + r"\s*)+", argument):
                found.add(b"".join(literals(argument)).decode("utf-8"))
            else:
                ternary = re.search(r"\?\s*(" + literal + r")\s*:\s*(" + literal + r")", argument)
                if ternary:
                    for branch in ternary.groups():
                        found.add(literals(branch)[0].decode("utf-8"))
    return found


def translations(data):
    """{language: {English: translation}} from ui/i18n.js run over ui/i18n-<language>.json (Node)."""
    texts = texts_of(data)
    request = json.dumps({"tr": texts, "ru": texts}, ensure_ascii=False).encode("utf-8")
    result = subprocess.run(["node", str(ROOT / "tools" / "ingame_translate.js")], input=request, capture_output=True, check=True)
    out = json.loads(result.stdout.decode("utf-8"))
    for language in ("tr", "ru"):
        # a text the dictionary has word for word (a template too) is its entry, as the app writes it
        words = json.loads((UI / f"i18n-{language}.json").read_text(encoding="utf-8"))
        out[language].update({t: words[t] for t in texts if words.get(t) and words[t] != t})
        out[language].update({t: v[language] for t, v in OWN_TEXTS.items()})
    return out


# -------------------------------------------------------------------------------------- build
def build(out: Path):
    out.mkdir(parents=True, exist_ok=True)
    files = {}

    def save(name, image, kind="PNG", **options):
        path = out / name
        buffer = io.BytesIO()
        image.save(buffer, kind, **options)
        path.write_bytes(buffer.getvalue())
        files[name] = path

    reference = Image.open(ASSETS / "chronoforge-reference.png").convert("RGB")
    art = Image.open(ASSETS / "chronoforge-art.png").convert("RGB")
    save("crest.png", reference_crop(reference, (1071, 670), (-37, -16), (135, 120), 0.52, 0.75), optimize=True)
    for name, position in {"default": (-162, -190), "speed": (-163, -260), "gold": (-436, -263), "drops": (-436, -189), "cooldown": (-14, -9)}.items():
        save(f"icon-{name}.png", reference_crop(reference, (714, 446), position, (55, 61), 0.60, 0.77), optimize=True)
    save("icon-density.png", density_icon(Image.open(ASSETS / "monster-density-256.png").convert("RGBA")), optimize=True)
    save("hero.jpg", hero_image(art, 225), "JPEG", quality=92, optimize=True)
    save("hero-compact.jpg", hero_image(art, 155), "JPEG", quality=92, optimize=True)
    save("sidebar.jpg", sidebar_image(art), "JPEG", quality=92, optimize=True)
    save("main.png", main_background(), optimize=True)
    slices = {}
    for name, (radius, fill) in {"frame-r0": (0, False), "frame-r2": (2, False), "frame-r3": (3, False), "frame-r4": (4, False),
                                 "frame-r5": (5, False), "frame-r6": (6, False), "fill-r5": (5, True), "fill-r6": (6, True),
                                 "frame-r12": (12, False), "fill-r2": (2, True), "fill-r3": (3, True), "fill-r4": (4, True),
                                 "fill-r12": (12, True)}.items():
        image, edge = frame_sprite(radius, fill=fill)
        save(f"{name}.png", image, optimize=True)
        slices[name] = edge
    for width in (2, 4):
        image, edge = band_sprite(width)
        save(f"band-{width}.png", image, optimize=True)
        slices[f"band-{width}"] = edge
    save("corner.png", corner_sprite(), optimize=True)
    save("ring-34.png", circle_sprite(34, ring=True), optimize=True)
    save("ring-30.png", circle_sprite(30, ring=True), optimize=True)  # .craft-step h3>span
    save("circle-17.png", circle_sprite(17), optimize=True)
    save("track.png", track_sprite(), optimize=True)
    save("thumb.png", thumb_sprite(), optimize=True)
    save("grad-panel.png", linear_gradient(128 * S, 128 * S, 135, [(0, rgba("#25362e33")), (0.5, (37, 54, 46, 0))]), optimize=True)
    save("grad-card.png", linear_gradient(128 * S, 128 * S, 135, [(0, rgba("#24402c24")), (1, (36, 64, 44, 0))]), optimize=True)
    save("grad-stat-value.png", linear_gradient(128 * S, 128 * S, 120, [(0, rgba(THEME["soft"])), (0.8, (0, 0, 0, 0))]), optimize=True)
    save("grad-browser.png", linear_gradient(128 * S, 128 * S, 135, [(0, rgba(THEME["soft"])), (1, rgba("#00000022"))]), optimize=True)
    save("grad-button.png", linear_gradient(64 * S, 32 * S, 160, [(0, rgba("#29302c55")), (1, rgba("#070d0cb8"))]), optimize=True)
    save("grad-primary.png", linear_gradient(128 * S, 32 * S, 145, [(0, rgba(THEME["soft"])), (1, rgba("#07160fe0"))]), optimize=True)
    soft = rgba(THEME["soft"])
    save("grad-nav.png", linear_gradient(128 * S, 8, 90, [(0, soft), (1, rgba("#12241b22"))]), optimize=True)
    # .dm-ico: the damage types' icons (ui/assets/damage-types.png, 7 frames), one image each
    damage_types = Image.open(ASSETS / "damage-types.png").convert("RGBA")
    frame = damage_types.height
    for i in range(7):
        save(f"dmg-type-{i}.png", damage_types.crop((i * frame, 0, (i + 1) * frame, frame)).resize((36, 36), Image.LANCZOS), optimize=True)
    faces = {}
    for weight, name, face in ((400, "inter-regular.ttf", "body"), (600, "inter-semibold.ttf", "semi"), (700, "inter-bold.ttf", "bold")):
        font = inter_instance(weight)
        (out / name).write_bytes(font)
        files[name] = out / name
        faces[face] = font

    data = build_data()
    data["advances"] = text_advances(faces)
    data["slices"] = slices
    data["files"] = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in sorted(files.items())}
    (out / "ui.json").write_text(json.dumps(data, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    files["ui.json"] = out / "ui.json"

    # RCDATA named by file: IMG_HERO_JPG etc. The core looks the names up; ui.json lists them.
    lines = ["// Generated by tools/build_ingame_assets.py. Do not edit."]
    for name, path in sorted(files.items()):
        lines.append(f'{resource_name(name)} RCDATA "{str(path.resolve()).replace(chr(92), chr(92) * 2)}"')
    (out / "ingame.rc").write_text("\n".join(lines) + "\n", encoding="ascii")
    total = sum(p.stat().st_size for p in files.values())
    print(f"in-game assets: {len(files)} files, {total / 1024:.0f} KiB -> {out}")
    return data


def resource_name(file_name):
    return "EP_" + "".join(ch if ch.isalnum() else "_" for ch in file_name).upper()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "native/build/ingame")
    build(parser.parse_args().out.resolve())
