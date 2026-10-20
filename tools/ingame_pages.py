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


def general_ids(controls):
    """The General settings panel: app.js filters the catalog the same way."""
    return [c["id"] for c in controls if c["group"] == "general" and c["widget"] in ("number", "toggle")
            and not c["id"].startswith(("loot_", "craft_", "map_")) and c["id"] not in WEAVER_SETTINGS]


def graphics_ids(controls):
    return [c["id"] for c in controls if c["group"] == "display" and c["id"].startswith("qol_")]


def settings(title, ids, text=None, note=None, overrides=None, head=None):
    """A settingPanel: heading (its badge, or head="refresh": a Refresh button as the Weaver's Will
    panel has), optional muted paragraph, the cards, optional note below."""
    section = {"kind": "settings", "title": title, "controls": ids}
    if text:
        section["text"] = text
    if note:
        section["note"] = note
    if overrides:
        section["overrides"] = overrides
    if head:
        section["head"] = head
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
                settings("Weaver's Will", WEAVER_SETTINGS, text=WEAVER_TEXT, head="refresh"), pending("Weaver's Will affixes")]
    if page == "loot":
        return [settings("Automatic collection", ["autopickup", "pickup_radius", "pickup_interval"]),
                pending("Recent pickups"), pending("Collect now"),
                settings("Quality rules", ["loot_lp", "loot_ww", "loot_t7", "loot_filter"]),
                pending("Loot filter files"),
                settings("Loot filter window", ["tier_filter_colors", "tier_name_colors"]),
                pending("Pickup selections"), pending("Pickup rules")]
    if page == "display":
        return [settings("Buffs & debuffs in game", ["effects_enabled", "effects_player", "effects_target", "effects_descriptions", "effects_scale", "effects_rows"],
                         note=EFFECTS_NOTE),
                settings("Camera", ["build_zoom", "build_cameraangle"]),
                settings("Minimap and overlay", ["map_minimapsize", "map_minimapzoom", "map_minimapopacity", "map_overlayopacity", "map_overlayzoom"]),
                settings("Item tiers", ["tier_icons", "tier_tooltips", "tier_lines", "tier_special", "tier_rules", "tier_rule_position", "tier_rule_parts"]),
                pending("Tier colours"), pending("Interface font"),
                settings("Graphics effects", graphics_ids(controls), note=GRAPHICS_NOTE)]
    if page == "world":
        return [settings("Map visibility", ["map_reveal"]),
                settings("Nearby interactions", ["qol_chests", "qol_shrines", "qol_portals", "qol_arearadius", "qol_areashape", "qol_areashow"]),
                pending("Interaction area colour"),
                settings("Rage of Morditas (Season 5)", ["rage_spawn", "rage_hunttime", "rage_charges", "rage_buffs", "rage_blood"], text=RAGE_TEXT)]
    if page == "skills":
        return [pending("Skill and point budgets"),
                settings("Experience and skill points", ["build_skillxp", "build_skillpoints", "build_passivepoints"]),
                settings("Weaver points", ["build_weaverpoints", "build_weaverflat"]),
                settings("Tree connections", ["build_skillnodes", "build_passivenodes", "build_weavernodes"]),
                settings("Projectiles", ["build_projectiles", "build_projectileflat"]),
                pending("Free respec")]
    if page == "equipment":
        return [settings("Equipment rules", ["build_offhand", "build_fullsets"])]
    if page == "summons":
        return [pending("Damage meter"), pending("Death recap"), pending("Live summoned allies"),
                settings("Summon lifetime", ["build_summonduration"]),
                settings("Minion counts", ["build_minionextra", "build_totemextra", "build_ballistaextra", "build_forgedextra", "build_companionextra", "build_immortalminions"])]
    if page == "monolith":
        return [pending("Monolith"), settings("Echo modifiers", ["density", "stability_multiplier"], note=MONOLITH_NOTE,
                                              overrides={"density": MONOLITH_DENSITY})]
    return [pending(dict((p[0], p[1]) for p in PAGES)[page])]


DEVELOPER_CONTROLS = {"density_read", "map_read", "pickup_radius_read", "tier_read", "map_settings_read", "font_read", "sheet_catalog",
                      "sheet_open", "resistance_labels", "session_read", "status", "player_read", "characters", "play_offline", "load_character"}
