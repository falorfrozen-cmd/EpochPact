"""Page composition of the in-game interface. Mirrors renderPage() in ui/app.js: the same pages,
titles and setting panels in the same order. Sections the in-game interface does not draw yet
are kept as "pending" so their place and title stay visible.

tools/test_ingame_assets.py checks every setting panel here against the settingPanel(...)
calls in ui/app.js, so the two cannot drift apart unnoticed.
"""

# id, navigation label, symbol, hero title, hero subtitle: the `pages` table of app.js.
PAGES = [
    ("overview", "Overview", "◈", "Your timeline. Your rules.", "Shape your journey, your loot and your character."),
    ("game_setup", "Game setup", "⌁", "Connect your game.", "Choose Last Epoch.exe, install the player mod and launch offline."),
    ("general", "General settings", "⚙", "Your journey. Your rules.", "Experience, loot, enemies and everyday conveniences."),
    ("loot", "Loot & Pickup", "▣", "Keep what matters.", "Automatic collection, loot filters and crafting material pickup."),
    ("crafting", "Crafting", "⚒", "Make every craft count.", "Forge your selected item, preserve materials and customize crafting costs."),
    ("stats", "Character", "♜", "Refine your character.", "Find a stat and customize its bonus."),
    ("skills", "Skills & Trees", "✧", "Build beyond the branches.", "Skill experience, point budgets, tree connections and projectiles."),
    ("equipment", "Equipment", "⚔", "Equip your imagination.", "Offhand combinations and full set bonuses."),
    ("summons", "Summons", "♧", "Command your companions.", "Minion bonuses and temporary summon duration."),
    ("display", "Display & Camera", "◉", "See Eterra your way.", "Camera pitch, zoom and temporary graphics overrides."),
    ("world", "World & Exploration", "⌖", "Explore without friction.", "Map visibility and nearby chest, shrine and portal interactions."),
    ("inventory", "Inventory", "▤", "Your workshop, anywhere.", "Open your stash or vendor and transfer inventory items safely."),
    ("atlas", "Unique Atlas", "✦", "Every relic has a story.", "Your collection, missing treasures and the next item to chase."),
    ("stash", "Stash Assistant", "▤", "Find the keeper.", "Compare your copies and mark what matters."),
    ("campaign", "Campaign", "⌘", "Begin a new chapter.", "Quests, their normal rewards and waypoints across the world."),
    ("monolith", "Monolith", "◇", "Reshape your destiny.", "Timelines, Normal / Empowered progression and Stability."),
    ("cof", "Circle of Fortune", "✧", "Forge your fortune.", "Favor, Reputation, prophecies, lenses and loot quality."),
    ("factions", "Factions", "♧", "The factions of Eterra.", "Circle of Fortune, Merchant’s Guild, Knights and Weaver."),
    ("settings", "Settings", "☷", "Make this workshop yours.", "Themes, saved profiles and recovery backups."),
]

WEAVER_SETTINGS = ["weaverswill", "weaversdrop", "weaversupgrades"]
# app.js nameTagExamples: shown as written in the Loot filter window note
NAME_TAG_EXAMPLES = ["<color=#FF4040>red</color>", "<color=yellow>yellow</color>", "<b>bold</b>", "<i>italic</i>", "<u>underlined</u>"]


def general_ids(controls):
    """The General settings panel: app.js filters the catalog the same way."""
    return [c["id"] for c in controls if c["group"] == "general" and c["widget"] in ("number", "toggle")
            and not c["id"].startswith(("loot_", "craft_", "map_")) and c["id"] not in WEAVER_SETTINGS]


def graphics_ids(controls):
    return [c["id"] for c in controls if c["group"] == "display" and c["id"].startswith("qol_")]


def native(kind, **fields):
    """A section the window draws natively (ingame_sections.cpp): "controls" lists the catalog
    controls it shows, so their labels, descriptions and commands are in ui.json."""
    return {"kind": "native", "native": kind, **fields}


def settings(title, ids, text=None, note=None, overrides=None, head=None, facts=None, note_tail=None, tail=None, tail_controls=None):
    """A settingPanel: heading (its badge, or head="refresh": a Refresh button as the Weaver's Will
    panel has), optional muted paragraph, the cards, optional note below. tail: a native part inside
    the panel after the cards (ingame_sections.cpp RegisterTail); tail_controls are the controls it shows."""
    section = {"kind": "settings", "title": title, "controls": ids}
    if text:
        section["text"] = text
    if note:
        section["note"] = note
    if overrides:
        section["overrides"] = overrides
    if head:
        section["head"] = head
    if facts:
        section["facts"] = facts          # a live note under the text (app.js rageFacts)
    if note_tail:
        section["noteTail"] = note_tail   # shown after the translated note, as written
    if tail:
        section["tail"] = tail
        section["tailControls"] = tail_controls or []
    return section


WEAVER_TEXT = ("Weaver's Will Uniques: how much Weaver's Will they roll, how often they drop and how their upgrades go. "
               "When a kill near you upgrades an equipped Weaver's Will item, the game adds a new affix or raises one by a tier; "
               "choose below which affixes it should prefer. Minimum Weaver's Will for auto pickup is under Loot & Pickup → Quality rules.")
RAGE_TEXT = ("These change only the values the game loaded, while you play offline; nothing is saved to your character. "
             "1 (or the game's own) puts the game's values back.")
MONOLITH_DENSITY = {"label": "Monolith monster density",
                    "description": "Increases the average size of new regular enemy packs in Normal and Empowered Echoes. "
                                   "Apply before entering the next Echo; existing packs and bosses are unchanged."}
EFFECTS_NOTE = ("Uses live game icons and descriptions. Effects with no readable timer show —. The panels let clicks pass through "
                "and only show effects exposed by the game's native UI.")
GRAPHICS_NOTE = ("Graphics overrides last for this character session. Turning them off restores the previous renderer values. "
                 "Your saved graphics preferences are kept.")
MONOLITH_NOTE = ("Monster density is shared with General settings and applies to eligible packs in other combat areas too. "
                 "×1 uses normal pack sizes. Apply before entering the next Echo; restarting the game resets these session settings.")


DEATH_TEXT = ("When your character dies, the hits of the 10 seconds before it are kept: who hit, with which skill or ailment, the damage by type and the "
              "health left after each hit, with the buffs and debuffs you had. Kept until the game closes; the damage window shows it under ☠.")


AUTOSORT_TEXT = ("Sorting uses the game's own Sort: your inventory each time it opens, a stash tab each time it is shown. "
                 "These choices are remembered by EpochPact.")


def pending(title):
    return {"kind": "pending", "title": title}


def sections(page, controls, counts):
    """The sections of one page, in app.js order."""
    if page == "overview":
        player = [c for c in controls if c["id"] not in DEVELOPER_CONTROLS]
        return [
            {"kind": "summary", "items": [[str(len(player)), "Available tools"], [str(counts["sheetRows"]), "Character bonuses"],
                                          [str(counts["timelines"]), "Monolith timelines"], [str(counts["statAliases"]), "Common stats"]]},
            settings("Quick controls", ["xp", "speed", "drops", "gold"]),
            {"kind": "links", "title": "Explore your workshop", "pages": ["stats", "loot", "crafting", "campaign", "monolith", "cof"]},
        ]
    if page == "general":
        return [settings("General game settings", general_ids(controls)),
                settings("Weaver's Will", WEAVER_SETTINGS, text=WEAVER_TEXT, head="refresh", tail="weaver_affixes",
                         tail_controls=["weaver_affixes"])]
    if page == "loot":
        return [settings("Automatic collection", ["autopickup", "pickup_radius", "pickup_interval"]),
                native("recent_pickups", controls=["loot_log", "loot_log_clear"]), native("collect_now", controls=["pickup_now"]),
                settings("Quality rules", ["loot_lp", "loot_ww", "loot_t7", "loot_filter"]),
                pending("Loot filter files"),
                settings("Loot filter window", ["tier_filter_colors", "tier_name_colors"], note="Rule name examples",
                         note_tail=": " + "   ".join(NAME_TAG_EXAMPLES)),
                native("pickup_selections", controls=["loot_mode", "loot_category", "loot_affixes"]),
                native("pickup_rules", controls=["loot_reset"])]
    if page == "display":
        return [settings("Buffs & debuffs in game", ["effects_enabled", "effects_player", "effects_target", "effects_descriptions", "effects_scale", "effects_rows"],
                         note=EFFECTS_NOTE),
                settings("Camera", ["build_zoom", "build_cameraangle"]),
                settings("Minimap and overlay", ["map_minimapsize", "map_minimapzoom", "map_minimapopacity", "map_overlayopacity", "map_overlayzoom"]),
                settings("Item tiers", ["tier_icons", "tier_tooltips", "tier_lines", "tier_special", "tier_rules", "tier_rule_position", "tier_rule_parts"]),
                native("tier_colours", controls=["tier_color", "tier_rule_color"]),
                native("interface_font", controls=["font_family", "font_read"]),
                settings("Text size and letter case", ["font_scale", "font_case"]),
                settings("Graphics effects", graphics_ids(controls), note=GRAPHICS_NOTE)]
    if page == "world":
        return [settings("Map visibility", ["map_reveal"]),
                settings("Nearby interactions", ["qol_chests", "qol_shrines", "qol_portals", "qol_arearadius", "qol_areashape", "qol_areashow"]),
                native("area_colour", controls=["qol_areacolor"]),
                settings("Rage of Morditas (Season 5)", ["rage_spawn", "rage_hunttime", "rage_charges", "rage_buffs", "rage_blood"], text=RAGE_TEXT,
                         facts="rage")]
    if page == "skills":
        return [native("cached_summary", read="buildread"), native("skill_budgets"),
                settings("Experience and skill points", ["build_skillxp", "build_skillpoints", "build_passivepoints"]),
                settings("Weaver points", ["build_weaverpoints", "build_weaverflat"]),
                settings("Tree connections", ["build_skillnodes", "build_passivenodes", "build_weavernodes"]),
                settings("Projectiles", ["build_projectiles", "build_projectileflat"]),
                native("free_respec", controls=["respec_skills", "respec_passives", "respec_weaver"])]
    if page == "equipment":
        return [native("cached_summary", read="buildread"), settings("Equipment rules", ["build_offhand", "build_fullsets"])]
    if page == "summons":
        return [native("damage_meter", controls=["damage_session", "damage_session_read"]),
                settings("Death recap", ["death_recap"], text=DEATH_TEXT, head="tail", tail="death_recap",
                         tail_controls=["death_recap_read", "death_recap_clear"]),
                native("summon_allies", controls=["summons_read"]),
                settings("Summon lifetime", ["build_summonduration"]),
                settings("Minion counts", ["build_minionextra", "build_totemextra", "build_ballistaextra", "build_forgedextra", "build_companionextra", "build_immortalminions"]),
                pending("Minion and companion bonuses")]
    if page == "monolith":
        return [native("cached_summary", read="monolithread"),
                settings("Echo modifiers", ["density", "stability_multiplier"], note=MONOLITH_NOTE, overrides={"density": MONOLITH_DENSITY}),
                settings("Boss blessing rewards", ["build_blessingoptions", "build_blessingrolls"]),
                native("monolith_management", controls=["monolith_read", "monolith_unlock", "monolith_select", "corruption", "stability"]),
                pending("Monolith Navigator")]
    if page == "campaign":
        return [native("cached_summary", read="progressread"),
                native("campaign", controls=["campaign_complete", "waypoints_unlock"]), native("mastery", controls=["mastery_choose"])]
    if page == "inventory":
        return [native("inventory_shortcuts", controls=["inventory_read", "inventory_stash", "inventory_vendor", "inventory_stashall", "inventory_sellall"]),
                settings("Automatic sorting and Sell all", ["inventory_autosort", "inventory_autosortall", "inventory_sellkeep"], text=AUTOSORT_TEXT,
                         head="none", tail="sort_tabs", tail_controls=["inventory_tools_read", "inventory_sorttab", "stash_rules_read"]),
                native("stash_rules", controls=["stash_rules_read", "stash_rules_create", "stash_rule_assign", "stash_rules_sort"])]
    if page == "cof":
        return [native("cached_summary", read="cofread"),
                native("cof_main", controls=["cof_read", "cof_join", "cof_rank", "cof_favor", "cof_reputation_grant", "cof_lenses_unlock"]),
                native("cof_prophecies", controls=["cofprophecy", "cofpreview", "cof_charges"]),
                settings("Gain, drop and quality multipliers", [c["id"] for c in controls if c["group"] == "cof" and c["lifetime"] == "session"])]
    if page == "factions":
        return [native("factions")]
    return [pending(dict((p[0], p[1]) for p in PAGES)[page])]


DEVELOPER_CONTROLS = {"density_read", "map_read", "pickup_radius_read", "tier_read", "map_settings_read", "font_read", "sheet_catalog",
                      "sheet_open", "resistance_labels", "session_read", "status", "player_read", "characters", "play_offline", "load_character"}
