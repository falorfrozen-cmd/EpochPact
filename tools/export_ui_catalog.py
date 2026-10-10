"""Export the existing offline mod's UI contract; refresh only reads game state."""
from __future__ import annotations

import argparse
import ast
import csv
import hashlib
import importlib
import inspect
import json
import re
from datetime import datetime, timedelta, timezone
from pathlib import Path

try:
    from . import le_session
except ImportError:
    import le_session

ROOT = Path(__file__).resolve().parents[1]
GROUPS = {
    "general": "Genel oyun ayarları", "stats": "C ekranı / karakter statları",
    "loot": "Loot ve toplama", "crafting": "Crafting",
    "campaign": "Kampanya ve waypoint", "monolith": "Monolith / endgame",
    "cof": "Circle of Fortune", "factions": "Faction / Weaver bilgileri",
    "session": "Bağlantı ve karakter", "recovery": "Yedek / geri alma",
    "atlas": "Unique Atlas", "stash": "Stash Assistant",
}
READ_COMMANDS = (
    "sessionread", "playerread", "statraw", "stat", "sheetstats", "status",
    "densityread", "progressread", "monolithread", "cofread", "factionread",
    "lootread", "craftread", "mapread",
)
FLOAT_MAX = 3.4028234663852886e38


def parameter(name, kind, minimum=None, maximum=None, *, choices=None, source=None):
    result = {"name": name, "type": kind}
    if minimum is not None: result["minimum"] = minimum
    if maximum is not None: result["maximum"] = maximum
    if choices is not None: result["choices"] = choices
    if source: result["optionsSource"] = source
    return result


def controls():
    items = []
    def add(id, group, label, widget, command, lifetime, description, *, params=(),
            default=None, reset=None, api=None, response="json", current=None, requires=(), doc=None):
        offline = [] if id in {"session_read", "status", "characters", "play_offline", "load_character"} else ["offline"]
        items.append(dict(id=id, group=group, label=label, widget=widget,
            availability="implemented", commandTemplate=command, pythonApi=api,
            responseFormat=response, parameters=list(params), default=default,
            resetCommand=reset, lifetime=lifetime, description=description,
            currentSource=current, requirements=[*offline, *requires], documentation=doc))
    def mult(id, group, label, command, high, description, api=None, current=None, doc=None, lifetime="session", requires=()):
        add(id, group, label, "number", command + " {value}", lifetime, description,
            params=[parameter("value", "number", 1, high)], default=1,
            reset=command + " 1", response="json" if group in ("cof", "monolith") else "text",
            api=api, current=current, requires=requires, doc=doc)
    save_id = parameter("save_id", "digit_string", source="liveState.progressread.player.id")
    value = lambda low, high: parameter("value", "integer", low, high)
    slot = parameter("slot", "integer", 0, 3, source="liveState.cofread.slots")
    reward = parameter("reward", "integer_or_none", 0, 65535, source="liveState.cofread.rewards[available=true].id")
    lens = parameter("lens", "integer_or_none", 0, 11, source="liveState.cofread.lenses[purchased=true].id")
    timeline = parameter("timeline", "integer", 1, 254, source="liveState.monolithread.timelines.id")
    difficulty = parameter("difficulty", "enum", choices=["normal", "empowered"], source="selected timeline.difficulties")
    difficulty["commandToIndex"] = {"normal": 0, "empowered": 1}
    difficulty["pythonTransform"] = 'empowered = (difficulty == "empowered")'
    add("atlas_read", "atlas", "Unique Atlas", "read", "atlasread", "read_only",
        "Read the game's Unique/Set catalog and the loaded stash, inventory and equipment. Wishlist stays local.",
        api="tools.collection_backend.atlas()", doc="docs/collection-navigator.md")
    add("stash_read", "stash", "Stash Assistant", "read", "stashread", "read_only",
        "Compare owned equipment and duplicate uniques. Protection is a local reminder; no items are changed.",
        api="tools.collection_backend.stash()", doc="docs/collection-navigator.md")
    add("echo_read", "monolith", "Monolith Navigator", "read", "echoread {save_id} {timeline} {difficulty}", "read_only",
        "Search rewards on an existing Echo web. This does not generate new Echoes.",
        params=[save_id, timeline, difficulty], api="tools.monolith_backend.echoes(save_id, timeline, empowered)", doc="docs/collection-navigator.md")
    add("echo_focus", "monolith", "Show Echo in game", "button", "echofocus {save_id} {timeline} {difficulty} {index}", "view",
        "Focus a visible Echo in the normal game map. Does not start or complete it.",
        params=[save_id, timeline, difficulty, parameter("index", "integer", 0, 2147483647)],
        api="tools.monolith_backend.focus_echo(save_id, timeline, empowered, index)", doc="docs/collection-navigator.md")
    stat_mode = parameter("mode", "enum", choices=["added", "increased", "more"])
    stat_value = parameter("value", "number", -FLOAT_MAX, FLOAT_MAX)
    stat_requires = ("loaded_actor",)
    for id, label, high, description in (
        ("xp", "Öldürme / mote XP çarpanı", 100, "GainExpFromEnemyOrMote kazancı; görev XP ödüllerini çarpmaz."),
        ("gold", "Yerden toplanan altın çarpanı", 100, "Normal altın pickup kazancına uygulanır."),
        ("drops", "Genel eşya drop adedi çarpanı", 25, "ItemDrop.itemMultiplier; CoF düşman hook'u ile ortak dispatcher."),
        ("density", "Monster density", 5, "Gelecekteki uygun normal paketlerin ortalama boyutu; mevcut paketler değişmez, boss/özel spawn hariç."),
        ("rarity", "Temel rarity yükseltme çarpanı", 10, "Her normal rarity roll'unu (x−1)/x ihtimalle bir kademe yükseltir; Exalted/T7 kontrolü ayrı."),
        ("speed", "Hareket hızı çarpanı", 5, "Movespeed increased katkısı = x−1; movespeed alias/raw stat ile aynı full key'i paylaşır."),
        ("cooldown", "Cooldown / charge hız çarpanı", 10, "Normal charge tick hızını ve cooldown sorgusunu ölçekler."),
    ):
        mult(id, "general", label, id, high, description, current=f"currentValues.{id}",
             lifetime="actor" if id == "speed" else "session", doc="docs/monster-density.md" if id == "density" else "GEMINI.md")
        if id == "speed":
            items[-1]["statBinding"] = dict(sp=9, tags=0, special=0, extra=0, mode="increased", valueTransform="x - 1", readCommand="statraw 9 0 0 0")
    add("autopickup", "loot", "Otomatik pickup", "toggle", "autopickup {value}", "session",
        "Normal pickup API'leri; eşya, altın, potion, tome ve bone. Tarama aralığı 0.75 saniye.",
        params=[value(0, 1)], default=0, reset="autopickup 0", response="text", current="currentValues.autopickup", doc="GEMINI.md")
    add("density_read", "general", "Density ölçümü", "read", "densityread", "read_only",
        "Paket/generator/planlanan-gözlenen spawn ve dışlama sayaçları.", response="text", doc="docs/monster-density.md")
    add("loot_read", "loot", "Akıllı toplama durumu / affix kataloğu", "read", "lootread", "read_only",
        "Seçimler ve sayaçlar; runtime affix ID/ad listesi. acceptedRequests envantere giriş garantisi değildir.", api="tools.loot_crafting_backend.loot_read()", doc="docs/loot-crafting.md")
    add("loot_mode", "loot", "Toplama modu", "selector", "lootmode {mode}", "session",
        "all: tüm eşyalar; filter: mevcut loot filtresi; quality: Unique LP veya seçilen T7; materials: shard/rune/glyph. autopickup ayrıca açılır.",
        params=[parameter("mode", "enum", choices=["all", "filter", "quality", "materials"])], default="all", reset="lootmode all",
        api="tools.loot_crafting_backend.loot_mode(mode)", current="liveState.lootread.mode", doc="docs/loot-crafting.md")
    add("loot_lp", "loot", "Unique minimum LP", "number", "lootlp {value}", "session",
        "Yalnız quality modu; LP eşiği ile T7 koşulu OR olarak birleştirilir. Weaver's Will ayrı bir LP koşulu değildir.", params=[value(0, 4)],
        default=2, reset="lootlp 2", api="tools.loot_crafting_backend.minimum_lp(value)", current="liveState.lootread.minimumLP", doc="docs/loot-crafting.md")
    for id, cmd, label, field, api in (
        ("loot_t7", "loott7", "T7 eşya toplama", "t7", "t7"),
        ("loot_filter", "lootfilter", "Quality modunda loot filtresine uy", "respectFilter", "respect_filter"),
    ):
        add(id, "loot", label, "toggle", cmd+" {value}", "session", "Quality moduna aittir; filter modu filtreye her zaman uyar.",
            params=[value(0, 1)], default=1, reset=cmd+" 1", api=f"tools.loot_crafting_backend.{api}(value)", current=f"liveState.lootread.{field}", doc="docs/loot-crafting.md")
    add("loot_affixes", "loot", "Aranan T7 affix ID'leri", "selector", "lootaffixes {ids}", "session",
        "Virgülle ayrılmış en fazla 64 runtime affix ID; none herhangi bir T7 demektir. T7 açık olmalı.",
        params=[parameter("ids", "single_token", source="liveState.lootread.affixes.id")], default="none", reset="lootaffixes none",
        api="tools.loot_crafting_backend.affixes_csv(ids)", doc="docs/loot-crafting.md")
    items[-1]["parameters"][0]["maximumLength"] = 383
    add("loot_category", "loot", "Toplama kategorisi", "selector", "lootcategory {category} {value}", "session",
        "Crafting materyali, altın, potion, XP/Favor tome ve bone ayrı seçilir; kategori ayarı auto pickup'ı açmaz.",
        params=[parameter("category", "enum", choices=["materials", "gold", "potions", "xp", "favor", "bones"]), value(0, 1)], default=1,
        reset="lootcategory {category} 1", api="tools.loot_crafting_backend.category(category, value)", doc="docs/loot-crafting.md")
    add("loot_reset", "loot", "Toplama seçimlerini sıfırla", "button", "lootreset", "session",
        "Seçimler all/default olur; autopickup açık/kapalı durumu ayrıca kontrol edilir.", api="tools.loot_crafting_backend.reset_loot()", doc="docs/loot-crafting.md")
    add("craft_read", "crafting", "Crafting önizleme / durum", "read", "craftread", "read_only",
        "Normal forge eligibility, eşya/affix/FP, shard craft maliyet aralığı ve glyph ihtimalleri; kesin RNG sonucu vermez. Rune/Insight maliyet aralığı gösterilmez.",
        api="tools.loot_crafting_backend.craft_read()", doc="docs/loot-crafting.md")
    add("craft_fp", "crafting", "FP maliyet katsayısı", "number", "craftfp {value}", "session",
        "0 ücretsiz, 1 normal; örnek 0.5 yarı maliyet, yukarı yuvarlanır. FP üretmez, native eligibility korunur.", params=[parameter("value", "number", 0, 1)],
        default=1, reset="craftfp 1", api="tools.loot_crafting_backend.fp_factor(value)", current="liveState.craftread.fpFactor", requires=("loaded_actor",), doc="docs/loot-crafting.md")
    for glyph, label in (("hope", "Glyph of Hope FP koruma ihtimali"), ("despair", "Glyph of Despair seal ihtimali")):
        add("craft_"+glyph, "crafting", label, "selector", "craft"+glyph+" {value}", "session",
            "Yüzde 0–100 veya reset; native glyph/affix uygunluğu gerekir. Despair T1–T4; Greater Hope ayrı desteklenmez.",
            params=[parameter("value", "number_or_reset", 0, 100)], default=None, reset="craft"+glyph+" reset",
            api=f'tools.loot_crafting_backend.glyph_chance("{glyph}", value)', current=f"liveState.craftread.{glyph}Percent", requires=("loaded_actor",), doc="docs/loot-crafting.md")
    add("craft_shards", "crafting", "Craft sırasında affix shard koru", "toggle", "craftshards {value}", "session",
        "Gerçekten tüketilen bir shardı normal AddShard API ile geri verir.", params=[value(0, 1)],
        default=0, reset="craftshards 0", api="tools.loot_crafting_backend.preserve_shards(value)", current="liveState.craftread.preserveShards", requires=("loaded_actor",), doc="docs/loot-crafting.md")
    for key, field in (("runes", "preserveRunes"), ("glyphs", "preserveGlyphs"), ("level", "bypassLevel")):
        add("craft_"+key, "crafting","Craft "+key,"toggle","craft"+key+" {value}","session",
            "Local normal forge only; other item and equipment rules remain.", params=[value(0,1)], default=0,
            reset="craft"+key+" 0", api=f"tools.loot_crafting_backend.{ {'runes':'preserve_runes','glyphs':'preserve_glyphs','level':'bypass_level'}[key]}(value)",
            current="liveState.craftread."+field, requires=("loaded_actor",), doc="docs/ce-feature-additions.md")
    add("craft_forge", "crafting","Forge selected item","button","craftforge {save_id}","game_save",
        "Perform exactly one normal craft on the current game forge selection. Requires an eligible item/material and matching offline character; snapshot before craft.",
        params=[save_id], api="tools.loot_crafting_backend.forge(save_id)", requires=("loaded_actor",), doc="docs/ce-feature-additions.md")
    add("map_reveal","general","Reveal zone map","toggle","mapreveal {value}","session",
        "Display the whole minimap/overlay using a fog shader substitution. Does not change exploration saves or spawn enemies. Off restores the current native fog texture.",
        params=[value(0,1)], default=0,reset="mapreveal 0",api="tools.loot_crafting_backend.reveal_map(value)",current="liveState.mapread.enabled",requires=("loaded_actor",),doc="docs/ce-feature-additions.md")
    add("map_read","general","Map display status","read","mapread","read_only","Read map visibility and active hooks.",api="tools.loot_crafting_backend.map_read()",doc="docs/ce-feature-additions.md")
    add("craft_reset", "crafting", "Crafting ayarlarını normale döndür", "button", "craftreset", "session",
        "FP=1, doğal glyph şansları, normal shard tüketimi; tüm crafting hook'ları kaldırılır.", api="tools.loot_crafting_backend.reset_craft()", doc="docs/loot-crafting.md")
    add("sheet_catalog", "stats", "Tüm C satırlarını oku", "read", "sheetstats", "read_only",
        "206 doğrulanmış satır; güncel sheet instance gerektiğinde kullanılabilir.", response="text", doc="docs/stat-editor.md")
    add("sheet_row", "stats", "C satırı düzenleyici", "stat_editor", "sheetstat {row} {mode} {value}", "actor",
        "Mod katkısını değiştirir. Secondary modifier için sheetstat {row} modifier {mode} {value}. Row ID instance'a aittir.",
        params=[parameter("row", "integer", 0, None, source="fresh sheetstats row ID"), stat_mode, stat_value],
        default=0, reset="sheetstat {row} reset", response="text", requires=stat_requires, doc="docs/stat-editor.md")
    add("raw_stat", "stats", "Full-key stat düzenleyici", "stat_editor", "statraw {sp} {tags} {special} {extra} {mode} {value}", "actor",
        "134 SP; kimlik SP/tags/special/extra. increased/more >= −1; nötr katkı tüm modlarda 0. Player/AbilityProperty tags indeks olabilir.",
        params=[parameter("sp", "integer", source="characterStats.properties.id"), parameter("tags", "integer", -2147483648, 2147483647),
                parameter("special", "integer", 0, 255), parameter("extra", "integer", -2147483648, 2147483647), stat_mode, stat_value],
        default=0, reset="statraw {sp} {tags} {special} {extra} reset", response="text", requires=stat_requires, doc="docs/stat-editor.md")
    add("stat_alias", "stats", "Hazır stat adları", "stat_editor", "stat {name} {value}", "actor",
        "characterStats.aliases içindeki isim/mode/birimleri kullan. Okumak için stat {name}.",
        params=[parameter("name", "enum", source="characterStats.aliases.name"), stat_value], default=0,
        response="text", requires=stat_requires, doc="docs/stat-editor.md")
    items[-1]["resetCommandSource"] = "characterStats.aliases[name].resetCommand"
    add("stat_reset", "stats", "Tüm mod stat katkılarını kaldır", "button", "statreset", "actor",
        "Yalnız EpochPact'in sahip olduğu kayıtları kaldırır; aynı key'i kullanan tüm UI kontrollerini yenile.", response="text", requires=stat_requires, doc="docs/stat-editor.md")
    add("sheet_open", "stats", "C panelini aç / kapat", "toggle", "sheetopen {value}", "view",
        "Normal UIBase aç/kapat; salt veri yenilemesi değildir.", params=[value(0, 1)], response="text", requires=stat_requires, doc="docs/stat-editor.md")
    add("resistance_labels", "stats", "Direnç yazılarını oku", "read", "sheetread", "read_only",
        "Yedi TMP_Text; sheet kapalı/yoksa kullanılamaz, inactive label eski olabilir.", response="text", doc="docs/stat-editor.md")
    add("progress_read", "campaign", "Görev / waypoint kataloğu", "read", "progressread", "read_only",
        "Yüklü id, seviye/XP/ödüller, eligibility ve gerçek waypoint sahneleri.", api="tools.progression_backend.read()", doc="docs/progression.md")
    for id, label, cmd, description in (
        ("campaign_complete", "Ana + yan kampanyayı tamamla, en az seviye 55", "questscomplete",
         "Henüz bitmemiş eligible kampanya görevleri ve normal ödülleri; minimum 55; End of Time, MonolithHub, M_Rest waypointleri. Tekrar ödül vermez."),
        ("waypoints_unlock", "Tüm gerçek waypointleri aç", "waypointsunlock",
         "Gerçek waypoint sahnelerini açar; Monolith timeline difficulty unlock ayrı işlemdir."),
    ):
        add(id, "campaign", label, "button", cmd + " {save_id}", "game_save", description,
            params=[save_id], api=f'tools.progression_backend.apply("{cmd}", save_id)',
            requires=("loaded_id_matches", "snapshot_before_write"), doc="docs/progression.md")
    add("monolith_read", "monolith", "Monolith kataloğu / güncel run", "read", "monolithread", "read_only",
        "10 timeline / 20 difficulty; sınırlar, unlock/run, editable ve seçim.", api="tools.monolith_backend.read()", doc="docs/monolith.md")
    hub = ("loaded_id_matches", "monolithread.editable=true", "snapshot_before_write")
    add("monolith_unlock", "monolith", "Normal + Empowered timeline'ları aç", "button", "monolithunlock {save_id}", "game_save",
        "Tüm normal/Empowered unlock; mevcutları atlar.", params=[save_id], api="tools.monolith_backend.unlock(save_id)", requires=hub, doc="docs/monolith.md")
    add("monolith_select", "monolith", "Timeline / difficulty seç", "selector", "monolithselect {save_id} {timeline} {difficulty}", "game_save",
        "Normal panel seçimi; echo başlatmaz/teleport etmez. Python empowered parametresi bool.",
        params=[save_id, timeline, difficulty], api="tools.monolith_backend.select(save_id, timeline, empowered)", requires=hub, doc="docs/monolith.md")
    for id, label, high, source in (
        ("corruption", "Corruption", 65535, "selected timeline.difficulties: minCorruption/maxCorruption"),
        ("stability", "Stability", 2147483647, "selected timeline.difficulties.maxStability"),
    ):
        v = value(0, high); v["runtimeBoundsSource"] = source
        add(id, "monolith", label, "number", id + " {save_id} {timeline} {difficulty} {value}", "game_save",
            "Seçili run/difficulty için mutlak değer; UI sınırlarını runtime asset'ten al. Python empowered bool.",
            params=[save_id, timeline, difficulty, v], api=f"tools.monolith_backend.{id}(save_id, timeline, empowered, value)", requires=hub, doc="docs/monolith.md")
        items[-1]["currentSource"] = f"liveState.monolithread.timelines[id=timeline].difficulties[index=difficultyIndex].run.{id}"
    mult("stability_multiplier", "monolith", "Stability kazanım çarpanı", "stabilitymult", 100,
         "Pozitif doğal kazanç; kayıpları/elle mutlak değer ayarını çarpmaz.", "tools.monolith_backend.multiplier(value)", "liveState.monolithread.stabilityMultiplier", "docs/monolith.md")
    add("cof_read", "cof", "CoF kataloğu / güncel durum", "read", "cofread", "read_only",
        "Rank/Favor/Reputation/yuvalar/lensler/ödül uygunluğu ve tuning telemetrisi.", api="tools.cof_backend.read()", doc="docs/cof.md")
    cof_persist = ("loaded_id_matches", "snapshot_before_write")
    add("cof_join", "cof", "CoF'ye katıl", "button", "cofjoin {save_id}", "game_save",
        "MG'den geçiş ayrı açık seçim: cofjoin {save_id} switch; mevcut üyelik bırakılır.", params=[save_id],
        api="tools.cof_backend.join(save_id, switch_from_merchant=False)", requires=cof_persist, doc="docs/cof.md")
    for id, label, cmd, low, high, function, description in (
        ("cof_rank", "CoF rank", "cofrank", 1, 12, "rank", "Rank azaltma yuva/bonusları kapatabilir; rank içi Reputation sıfırlanır."),
        ("cof_favor", "Favor bakiyesi", "coffavor", 0, 999999, "favor", "Mutlak bakiye; prophecy/Rep kazancı üretmez."),
        ("cof_reputation_grant", "Reputation EKLE", "cofreputation", 0, 1000000, "reputation", "Eklenecek miktar; mutlak bakiye ayarı değildir; gain çarpanını bypass eder."),
    ):
        add(id, "cof", label, "number", cmd + " {save_id} {value}", "game_save", description,
            params=[save_id, value(low, high)], api=f"tools.cof_backend.{function}(save_id, value)", requires=cof_persist, doc="docs/cof.md")
        if id == "cof_reputation_grant": items[-1]["default"] = 0
        else: items[-1]["currentSource"] = f"liveState.cofread.cof.{function}"
    add("cof_lenses_unlock", "cof", "Rank'ın izin verdiği lensleri aç", "button", "coflenses {save_id}", "game_save",
        "Henüz alınmamış uygun lensleri normal satın alma yoluyla açar.", params=[save_id],
        api="tools.cof_backend.unlock_lenses(save_id)", requires=cof_persist, doc="docs/cof.md")
    for preview in (True, False):
        cmd = "cofpreview" if preview else "cofprophecy"
        add(cmd, "cof", "Prophecy değişikliğini önizle" if preview else "Prophecy ödül / lens seç",
            "preview" if preview else "selector", cmd + " {save_id} {slot} {reward} {lens}", "read_only" if preview else "game_save",
            "none seçimi kaldırır. Ödül değişince şarj sıfırlanır; lens değişikliğini önizle. Aynı ödül/lens iki yuvada olamaz.",
            params=[save_id, slot, reward, lens], api=f"tools.cof_backend.prophecy(save_id, slot, reward_id, lens_id, preview={preview})",
            requires=("loaded_id_matches",) if preview else cof_persist, doc="docs/cof.md")
    add("cof_charges", "cof", "Tamamlanmış prophecy şarjları", "number", "cofcharges {save_id} {slot} {value}", "game_save",
        "0–99 şarj; hemen eşya üretmez. Azaltma kesirli ilerlemeyi de sıfırlar.", params=[save_id, slot, value(0, 99)],
        api="tools.cof_backend.charges(save_id, slot, value)", current="liveState.cofread.slots[index=slot].charges",
        requires=cof_persist, doc="docs/cof.md")
    for id, label, cmd, high, function, description, current in (
        ("cof_favor_multiplier", "Favor kazanım çarpanı", "coffavormult", 100, "favor_multiplier", "Normal pozitif Favor; doğal türetilen Reputation ve prophecy akışı devam eder.", "liveState.cofread.favorMultiplier"),
        ("cof_reputation_multiplier", "Reputation kazanım çarpanı", "cofrepmult", 100, "reputation_multiplier", "Favor kazanım/harcamasından gelen Rep; elle ekleme hariç. Favor kazancında iki çarpan birleşir.", "liveState.cofread.reputationMultiplier"),
        ("cof_charge_multiplier", "Prophecy şarj hızı", "cofchargemult", 100, "charge_multiplier", "Favor/Rep bakiyesinden bağımsız; Tyranny harcama maliyetini artırmaz.", "liveState.cofread.tuning.settings.charge"),
        ("cof_reward_multiplier", "Prophecy ödül adedi", "cofrewardmult", 25, "reward_multiplier", "Normal spawn başına temel adet üst sınırı 250; rank12/Duplication birleşir; ek şarj tüketmez.", "liveState.cofread.tuning.settings.reward"),
        ("cof_exalted_multiplier", "Exalted roll katsayısı", "cofexaltedmult", 100, "exalted_multiplier", "Native coefficient × ayar (rank12: 1.5); Rare→Exalted gerçek şansına da uygulanır. Yüzde değildir.", "liveState.cofread.tuning.settings.exalted"),
        ("cof_t7_multiplier", "T7 roll katsayısı", "coft7mult", 100, "t7_multiplier", "Native coefficient × ayar (rank12: 2); seviye/tier uygunluğu oyunda kalır. Yüzde değildir.", "liveState.cofread.tuning.settings.t7"),
        ("cof_lp_multiplier", "Unique LP roll katsayısı", "coflpmult", 100, "lp_multiplier", "Yalnız native CoF bonusuna uygun Unique. LP 0–4; Weaver's Will istisnası; garanti LP adedi değildir.", "liveState.cofread.tuning.settings.lp"),
    ):
        mult(id, "cof", label, cmd, high, description, f"tools.cof_backend.{function}(value)", current, "docs/cof.md", requires=("cof_member_to_enable",))
    for source, label in (("enemy", "Düşman çift eşya ihtimali (%)"), ("echo", "Normal Monolith çift eşya ihtimali (%)")):
        add("cof_double_" + source, "cof", label, "number", f"cofdouble {source} {{value}}", "session",
            "Ayar doğrudan yüzde; 0 etkisiz çift şans, reset normal rank bonusu. Echo: eşya ödülü, Tomb/Gold/XP istisnaları normal.",
            params=[parameter("value", "number_or_reset", 0, 100)], default=None, reset=f"cofdouble {source} reset",
            api=f'tools.cof_backend.double_drop_chance("{source}", percent_or_None)',
            current=f"liveState.cofread.tuning.settings.{source}", requires=("cof_member_to_enable",), doc="docs/cof.md")
    for lens_name, label, description in (
        ("celerity", "Celerity lens gücü", "+%50 ekstra şarj katkısını ölçekler: x3 → +%150 (toplam x2.5)."),
        ("charity", "Charity lens gücü", "Diğer yuvaya +%10 ekstra şarj katkısı: x5 → +%50."),
        ("duplication", "Duplication lens gücü", "%40 ek ödül şansını ölçekler; %100'e sınırlandırılır."),
    ):
        mult("cof_lens_" + lens_name, "cof", label, "coflensmult " + lens_name, 100, description,
             f'tools.cof_backend.lens_multiplier("{lens_name}", value)', f"liveState.cofread.tuning.settings.{lens_name}",
             "docs/cof.md", requires=("cof_member_to_enable", "corresponding_lens_equipped_for_effect"))
    add("factions_read", "factions", "CoF / MG / Knights / Weaver bilgileri", "read", "factionread", "read_only",
        "Dört faction'ın rank asset'leri/durumları; Weaver earned points ve 13+40=53 sınırı. MG/Weaver mutation API hazır değil.",
        api='tools.progression_backend.request("factionread")', doc="research/weaver-1.5.md")
    for id, label, cmd, description, response in (
        ("session_read", "Bağlantı / geçiş durumu", "sessionread", "InGame ve transitioning=false kontrolü.", "json"),
        ("status", "Tüm mod ayarları / sayaçlar", "status", "Offline gate, etkin çarpanlar ve hata/hook durumları.", "text"),
        ("player_read", "Yüklü karakter kimliği", "playerread", "İsim/seviye/IsOffline; save id için progressread kullan.", "text"),
        ("characters", "Çevrimdışı karakterler", "characters", "Normal karakter seçim paneli okunur.", "text"),
    ):
        add(id, "session", label, "read", cmd, "read_only", description, response=response, doc="GEMINI.md")
    add("play_offline", "session", "Play Offline", "button", "playoffline", "view", "Normal Login paneli; geçiş bittikten sonra çalışır.", response="text", requires=("Login_and_not_transitioning",), doc="GEMINI.md")
    add("load_character", "session", "Çevrimdışı karakter yükle", "selector", "loadname {name} {level}", "view",
        "Normal seçim tile double-click; boşluksuz tam isim, isteğe bağlı seviye. Sonra yüklü id'yi tekrar oku.",
        params=[parameter("name", "single_token", source="characters"), parameter("level", "optional_integer", 1, 100)],
        response="text", requires=("CharacterSelect_and_not_transitioning",), doc="GEMINI.md")
    add("undo", "recovery", "Son işlemin yedeğini geri yükle", "button", None, "recovery",
        "Snapshot ve yüklü karakteri doğrular, oyunu normal kapatır, geri yükler. UI kullanıcının seçtiği işleme bağlamalı.",
        params=[parameter("backup", "path", source="mutation reply.backup")], api="tools.progression_backend.undo(Path(backup))", doc="docs/progression.md")
    for item in items:
        if item["widget"] == "stat_editor":
            item["valueLimitsSource"] = "characterStats.modeLimits[alias.mode]" if item["id"] == "stat_alias" else "characterStats.modeLimits[mode]"
    return items


def enum_catalog(field_table):
    names = {"SP": "properties", "AT": "tags", "AilmentID": "ailments", "CharacterStatDisplay.DisplayType": "displayTypes", "ProphecySlot.LensType": "lensTypes",
             "AbilityID": "abilityIds", "ConditionalDamageProperty": "conditionalDamageProperties", "TrackerPropertyID": "trackerProperties", "IdolAltarPropertyID": "idolAltarProperties",
             "QuestType": "questTypes", "QuestState": "questStates", "ZoneChapterManager.Chapter": "questChapters",
             "ItemRarity": "itemRarities", "ItemCreationProperties.RarityGroup": "itemCreationRarityGroups", "BazaarItemRarity": "bazaarRarities"}
    enums = {value: [] for value in names.values()}
    with field_table.open(encoding="utf-8-sig", newline="") as f:
        for row in csv.reader(f, delimiter="\t"):
            if len(row) >= 8 and row[0] == "LE.dll" and row[2] in names and row[5] == "const" and row[7]:
                enums[names[row[2]]].append({"id": int(row[7], 0), "name": row[3]})
    if any(not values for values in enums.values()): raise ValueError("Güncel dump enum'ları eksik.")
    return enums


KEY = r"SP=(\d+) \(([^)]+)\) tags=(-?\d+) special=(\d+) extra=(-?\d+)"
def stat_key(match):
    return dict(sp=int(match[0]), property=match[1], tags=int(match[2]), special=int(match[3]), extra=int(match[4]))


def raw_prefix(key):
    return f'statraw {key["sp"]} {key["tags"]} {key["special"]} {key["extra"]}'


def sheet_rows(text):
    header = re.match(r"sheetstats: (\d+) display components;", text)
    if not header: raise ValueError("Doğrulanmış sheetstats çıktısı bekleniyor.")
    result = []
    for line in text.splitlines():
        match = re.match(r"(\d+) name=(.*?) " + KEY, line)
        if not match: continue
        key = stat_key(match.groups()[2:])
        flags = dict(re.findall(r"(display|ignoreAdded|ignoreIncreased|exact|base|quotient|inverse|attackRate|cap|derived)=([^ ]+)", line.split(" cachedText=", 1)[0]))
        flags = {k: None if v == "none" else float(v) if k in ("base", "cap") else int(v) for k, v in flags.items()}
        mod = re.search(r"modifier=\{" + KEY + r"\}", line)
        modifier_key = stat_key(mod.groups()) if mod else None
        derived = bool(flags.get("derived") or flags.get("quotient") or flags.get("inverse") or flags.get("attackRate"))
        result.append(dict(rowIdAtCapture=int(match[1]), objectName=match[2], key=key,
            sharedKey=f'{key["sp"]}:{key["tags"]}:{key["special"]}:{key["extra"]}', displayMetadata=flags,
            modifierKey=modifier_key, derived=derived, modes=["added", "increased", "more"],
            readCommand=raw_prefix(key), setCommand=raw_prefix(key)+" {mode} {value}", resetCommand=raw_prefix(key)+" reset",
            modifierSetCommand=raw_prefix(modifier_key)+" {mode} {value}" if modifier_key else None))
    if len(result) != int(header[1]): raise ValueError("Sheet satırı parse sayısı başlıkla eşleşmiyor.")
    return result


def aliases(properties):
    source = (ROOT / "native/core/player.cpp").read_text(encoding="utf-8")
    body = re.search(r"const StatDef kStatDefs\[\] = \{(.*?)\n\};", source, re.S)[1]
    tags = {name: int(n) for name, n in re.findall(r"(AT_\w+)\s*=\s*(\d+)", source)}
    names = {p["id"]: p["name"] for p in properties}
    fractions = {"fire", "cold", "lightning", "void", "necrotic", "poison", "physical", "allres", "wardretention", "block", "endurance", "critavoid", "glancing", "parry", "healthleech", "reflect", "damagereflected", "firepen", "coldpen", "lightningpen", "physicalpen", "voidpen", "necroticpen", "poisonpen"}
    result = []
    for name, sp, expression, increased in re.findall(r'\{"([^"]+)",\s*(\d+),\s*([^,]+),\s*(true|false)\}', body):
        bits = 0
        for term in expression.split("|"):
            term = term.strip(); bits |= tags[term] if term.startswith("AT_") else int(term)
        mode = "increased" if increased == "true" else "added"
        key = dict(sp=int(sp), property=names[int(sp)], tags=bits, special=0, extra=0)
        fractional = mode == "increased" or name in fractions
        result.append(dict(name=name, key=key, mode=mode, default=0, unit="fraction" if fractional else "raw_game_units",
            percentUiScale=0.01 if fractional else None, readCommand="stat " + name, setCommand="stat " + name + " {value}",
            resetCommand=raw_prefix(key)+" reset", sharedKey=f'{sp}:{bits}:0:0'))
    if not result: raise ValueError("Native stat alias tablosu boş.")
    return result


def refresh():
    session = json.loads(le_session.send("sessionread", timeout=15))
    if not session.get("ok") or session.get("state") != "InGame" or session.get("transitioning"):
        raise RuntimeError("Önce offline karakterde normal InGame durumuna gir.")
    result = {"sessionread": session}
    for command in READ_COMMANDS[1:]:
        text = le_session.send(command, timeout=30)
        try: result[command] = json.loads(text)
        except json.JSONDecodeError: result[command] = text
    return result


def validate(catalog):
    stats = catalog["characterStats"]
    ids = {p["id"] for p in stats["properties"]}
    enum_line = catalog["liveState"]["statraw"].splitlines()[1]
    runtime = {int(i): name for i, name in re.findall(r"(?:^|\s)(\d+)=(\w+)", enum_line)}
    if runtime != {p["id"]: p["name"] for p in stats["properties"]}: raise ValueError("Dump ve runtime SP eşleşmiyor.")
    native_aliases = catalog["liveState"]["stat"].splitlines()[0].removeprefix("stat names:").split()
    if native_aliases != [a["name"] for a in stats["aliases"]]: raise ValueError("Native alias listesi eşleşmiyor.")
    for row in stats["sheetRows"]:
        for key in (row["key"], row["modifierKey"]):
            if key and (key["sp"] not in ids or not 0 <= key["special"] <= 255): raise ValueError("Geçersiz sheet full key.")
    controls = catalog["controls"]
    if len({c["id"] for c in controls}) != len(controls): raise ValueError("Tekrarlanan kontrol id.")
    native = (ROOT / "native/core/commands.cpp").read_text(encoding="utf-8")
    for c in controls:
        if c["group"] not in GROUPS: raise ValueError("Geçersiz grup.")
        if c["commandTemplate"] and '"'+c["commandTemplate"].split()[0]+'"' not in native: raise ValueError("Native komut bulunamadı.")
        for p in c["parameters"]:
            if p.get("maximum") is not None and p.get("minimum", -FLOAT_MAX) > p["maximum"]:
                raise ValueError("Ters değer sınırı.")
        if c["pythonApi"]:
            call = ast.parse(c["pythonApi"], mode="eval").body
            module_name, function_name = c["pythonApi"].split("(", 1)[0].rsplit(".", 1)
            module = importlib.import_module(module_name if __package__ else module_name.removeprefix("tools."))
            function = getattr(module, function_name)
            inspect.signature(function).bind(*[None for _ in call.args], **{kw.arg: None for kw in call.keywords})
        for template in (c["commandTemplate"], c["resetCommand"]):
            placeholders = set(re.findall(r"\{(\w+)\}", template or ""))
            if placeholders - {p["name"] for p in c["parameters"]}:
                raise ValueError(f"Şablonda tanımsız parametre: {c['id']}")
    normal_lines, research_depth = [], 0
    for line in native.splitlines():
        if line.startswith(("#ifdef EPOCHPACT_RESEARCH", "#if defined(EPOCHPACT_RESEARCH)")): research_depth += 1
        elif line.startswith("#endif") and research_depth: research_depth -= 1
        elif not research_depth: normal_lines.append(line)
    roots = set(re.findall(r'cmd\s*==\s*"(\w+)"', "\n".join(normal_lines)))
    exposed = {c["commandTemplate"].split()[0] for c in controls if c["commandTemplate"]}
    # Private transport helpers used by existing backends, not additional user
    # features. Never expose a second travel/mutation button for these helpers.
    internal = {"identityread", "frameread", "framereset", "monolithpanelready", "monolithrest"}
    if roots - exposed - internal: raise ValueError(f"UI kataloğunda eksik normal komut: {roots - exposed - internal}")
    player = catalog["liveState"]["progressread"]["player"]
    for cmd in ("cofread", "monolithread", "factionread"):
        other = catalog["liveState"][cmd]["player"]
        if other["id"] != player["id"] or other["name"] != player["name"]: raise ValueError("Snapshot karakteri değişmiş.")


def build(snapshot, field_table, fallback):
    for cmd in ("progressread", "cofread", "monolithread", "factionread"):
        if not isinstance(snapshot.get(cmd), dict) or not snapshot[cmd].get("ok"): raise ValueError(f"{cmd} snapshot eksik/hatalı.")
    if "CharacterData.IsOffline=true" not in snapshot["playerread"]: raise ValueError("Snapshot offline doğrulanmadı.")
    live_sheet = snapshot.get("sheetstats", "")
    is_live = live_sheet.startswith("sheetstats:")
    text = live_sheet if is_live else fallback.read_text(encoding="utf-8-sig")
    enums = enum_catalog(field_table)
    rows = sheet_rows(text)
    now = datetime.now(timezone(timedelta(hours=7))).isoformat(timespec="seconds")
    current = {}
    for name, val in re.findall(r"^(xp|gold|drops|density|rarity|speed|cooldown): x([\d.]+)", snapshot["status"], re.M): current[name] = float(val)
    auto = re.search(r"^autopickup: (on|off)\b", snapshot["status"], re.M)
    if auto: current["autopickup"] = int(auto[1] == "on")
    result = dict(schemaVersion=1, project="EpochPact", gameVersion=snapshot.get("craftread", {}).get("gameVersion", "1.5.0.1"), generatedAt=now, timezone="Asia/Bangkok",
        groups=[dict(id=id, label=label) for id, label in GROUPS.items()], controls=controls(),
        contract=dict(transport="tools.le_session.send(command); JSON actions use existing *_backend.py wrappers",
            serializeAllIpc=True, onlineSupported=False, defaultVsCurrent="default = neutral/startup value; current = timestamped snapshot, never a saved profile",
            statNeutral=0, moreFormula="total multiplier contribution = 1 + value", percentageConversion="percent UI / 100 for fraction stats; CoF double chances use direct 0..100",
            lifetimes={"session":"Process lifetime; reapply desired UI profile after restart.", "actor":"Current actor only; reconcile after zone/character reload.",
                       "game_save":"Normal save; snapshot included in mutation result.", "view":"Game UI/navigation state.", "read_only":"No save/stat edits.", "recovery":"Normal close and validated snapshot restoration."}),
        characterStats=dict(**{k:v for k,v in enums.items() if k in ("properties", "tags", "ailments", "displayTypes")}, aliases=aliases(enums["properties"]), sheetRows=rows,
            sheetSource=dict(kind="live" if is_live else "previously_verified_capture", path="sheetstats" if is_live else str(fallback.relative_to(ROOT)),
                rowIdsValidForCurrentPlayer=is_live, liveReadResult=None if is_live else live_sheet,
                note="Key metadata is mapped; capture-time labels are omitted. Regenerate row IDs when sheet is available; raw keys avoid stored row indices."),
            modeLimits={"added":{"minimum":-FLOAT_MAX,"maximum":FLOAT_MAX,"neutral":0}, "increased":{"minimum":-1,"maximum":FLOAT_MAX,"neutral":0}, "more":{"minimum":-1,"maximum":FLOAT_MAX,"neutral":0}}),
        currentValues=current, liveState=snapshot, lensTypes=enums["lensTypes"],
        referenceEnums={k:v for k,v in enums.items() if k not in ("properties", "tags", "ailments", "displayTypes", "lensTypes")},
        researchOnly=[
            dict(id="merchants_guild_management", label="Merchant's Guild yönetimi", availability="read_only", readCommand="factionread", documentation="research/factions-1.5.md"),
            dict(id="weaver_rank_amber_tree", label="Weaver rank / Amber / ağaç yazma", availability="researched_not_implemented", readCommand="factionread", documentation="research/weaver-1.5.md",
                 details="Rank/puanlar okunuyor; Amber cüzdanı ve rank/Amber/node/Woven yazma API'leri hazır değil. 13 rank puanı + 40 echo puanı = 53."),
            dict(id="permanent_profiles", label="Kalıcı mod profilleri", availability="ui_work_remaining", documentation="GEMINI.md", details="Mevcut native ayarlar profil olarak kaydedilmez; kullanıcı UI katmanında yapacak."),
        ], referenceFiles=["GEMINI.md","research/stat-map.md","research/findings.md","docs/stat-editor.md","docs/monster-density.md","docs/progression.md","docs/monolith.md","docs/cof.md","docs/loot-crafting.md","docs/ce-feature-additions.md","research/factions-1.5.md","research/weaver-1.5.md"],
        sources=dict(fieldTable=str(field_table), fieldTableSha256=hashlib.sha256(field_table.read_bytes()).hexdigest(), nativeAliases="native/core/player.cpp"))
    validate(result)
    plain = lambda s: re.sub(r"<[^>]+>", "", s)
    result["optionCatalogs"] = dict(
        campaignQuests=[q for q in snapshot["progressread"]["quests"] if q["eligible"]],
        waypoints=[w for w in snapshot["progressread"]["waypoints"] if not w["noWaypoint"]],
        timelines=snapshot["monolithread"]["timelines"],
        prophecyRewards=[dict(r, uiLabel=plain(r["name"])) for r in snapshot["cofread"]["rewards"]],
        lenses=[dict(l, uiLabel=plain(l["name"]), uiEffect=plain(l["effect"])) for l in snapshot["cofread"]["lenses"]],
        factions=snapshot["factionread"]["factions"])
    result["counts"] = dict(controls=len(result["controls"]), properties=len(enums["properties"]), statAliases=len(result["characterStats"]["aliases"]), sheetRows=len(rows),
        distinctSheetKeys=len({r["sharedKey"] for r in rows}), atTags=len(enums["tags"]), ailmentIds=len(enums["ailments"]),
        quests=len(snapshot["progressread"]["quests"]), eligibleCampaignQuests=sum(q["eligible"] for q in snapshot["progressread"]["quests"]),
        sceneMarkers=len(snapshot["progressread"]["waypoints"]), waypoints=len(result["optionCatalogs"]["waypoints"]),
        timelines=len(snapshot["monolithread"]["timelines"]), lenses=len(snapshot["cofread"]["lenses"]), prophecyRewards=len(snapshot["cofread"]["rewards"]))
    return result


def guide(catalog):
    """Keep the readable index synchronized with the machine catalog."""
    counts = catalog["counts"]
    life = {"session":"Oturum", "actor":"Yüklü aktör", "game_save":"Oyun kaydı", "view":"Görünüm", "read_only":"Okuma", "recovery":"Geri alma"}
    esc = lambda s: str(s).replace("|", "\\|").replace("\n", " ")
    lines = [
        "# EpochPact — UI için tek özellik ve değer kataloğu", "",
        "Ana veri dosyası: [ui/catalog.json](../ui/catalog.json). Bu rehber ve JSON,",
        "[tools/export_ui_catalog.py](../tools/export_ui_catalog.py) ile aynı metadata'dan üretilir.",
        "Native komutlar, Python API'leri, parametre sınırları, nötr başlangıç değerleri,",
        "kalıcılık, gerçek seçenek ID'leri ve kaynak dosyalar burada bir arada.", "",
        f"Üretim: `{catalog['generatedAt']}`; oyun okumalarının zamanı: `{catalog['snapshotCapturedAt']}` (Asia/Bangkok).",
        "`default` nötr/başlangıç değeridir; `liveState`/`currentValues` yalnız tarihli okumadır.",
        "JSON yüklemek hiçbir ayarı oyuna uygulamaz; kayıtlı mod profili oluşturmaz.", "",
        "## İçerik", "", "| Katalog | Adet |", "|---|---:|",
    ]
    for label, field in (("Panel kontrolü / işlem", "controls"), ("SP stat türü", "properties"), ("Hazır stat adı", "statAliases"),
                         ("Doğrulanmış C satırı", "sheetRows"), ("C satırlarının farklı temel full key'i", "distinctSheetKeys"),
                         ("AT tag adı", "atTags"), ("Ailment ID", "ailmentIds"), ("Kampanya tamamlama kapsamındaki görev", "eligibleCampaignQuests"),
                         ("Gerçek waypoint", "waypoints"), ("Monolith timeline", "timelines"), ("Lens", "lenses"), ("Prophecy ödül asset'i", "prophecyRewards")):
        lines.append(f"| {label} | {counts[field]} |")
    lines += ["", "## JSON alanları", "", "| Alan | UI kullanımı |", "|---|---|",
        "| `groups`, `controls` | Türkçe panel grupları, sabit kontrol ID'si, widget, komut şablonu, Python API, cevap biçimi, parametreler, reset ve kalıcılık. |",
        "| `characterStats.properties` | 134 SP adı ve gerçek ID. |",
        "| `characterStats.aliases` | 72 hazır isim; full key, hangi mode'a yazdığı, birim dönüşümü ve reset komutu. |",
        "| `characterStats.sheetRows` | 206 satır; full key, secondary modifier, paylaşılmış key, display/cap/derived metadata ve raw komutlar. |",
        "| `characterStats.tags`, `ailments`, `displayTypes` | AT bitleri, AilmentID ve oyun format kodları. |",
        "| `referenceEnums` | AbilityID, ConditionalDamageProperty, TrackerPropertyID, IdolAltarPropertyID; QuestType/QuestState/chapter ve farklı rarity enum'ları. |",
        "| `optionCatalogs` | Filtrelenmiş 85 kampanya görevi / 109 waypoint; 10 timeline; 123 ödül; 12 lens; faction/rank verileri. |",
        "| `liveState` | Ham ve tarihli session/progression/Monolith/CoF/faction okumaları. İşlemden önce yeniden oku. |",
        "| `currentValues` | `status` içinde bildirilen genel ayarların tarihli değerleri; nihai combat toplamları değildir. |",
        "| `researchOnly` | MG/Weaver yazma işlemleri ve kalıcı profiller için hazır olmayan kısımların durumu. |",
        "| `referenceFiles`, `sources` | Derin araştırma ve runtime dump kaynağı/hash'i. |", "",
        "## UI'nin izleyeceği kurallar", "",
        "1. `sessionread` ile geçişin bittiğini doğrula; değişiklikler offline karakterde yapılır.",
        "2. Kalıcı işlemlerin `save_id` değerini güncel `progressread`/ilgili modül okumasından al; örnek `0` kimliğini sabitleme.",
        "3. Native IPC tek kanaldır. İstekleri sıraya koy; aynı anda birden çok komut yazma.",
        "4. JSON cevaplı işlemlerde `ok` kontrol et, hata/backup bilgisini göster. Text cevaplı stat/genel komutları JSON sanarak parse etme.",
        "5. `actor` stat ayarlarını aktör/karakter/harita yenilenmesinde uzlaştır; `session` çarpanlarını yeniden başlatınca istenen profil ile uygula.",
        "6. `game_save` butonlarını profil yüklenince otomatik tekrar çalıştırma. Bunlar ilerleme, ödül veya bakiye değiştirir.",
        "7. Aynı `sharedKey`/`statBinding` için iki ayrı kalıcı katkı oluşturma. `speed`, `stat movespeed` ve raw Movespeed aynı mod kaydını kullanır.",
        "8. Katalogları/scene taramalarını ihtiyaç olduğunda yenile; her frame tekrar isteme.", "",
        "Login/karakter seçimindeki bağlantı okumaları ve `playoffline`, yüklü offline aktör koşulu istemez.",
        "Kontrolün `requirements` alanını kullan; tüm kontrolleri yalnız InGame durumunda açma.", "",
        "### C satırlarının durumu", "",
        ("Eşlemeler bu snapshot'taki canlı C ekranından okundu." if catalog["characterStats"]["sheetSource"]["rowIdsValidForCurrentPlayer"] else
         "206 eşleme son doğrulanmış C ekranı çıktısından alındı. Son okumada C sheet instance'ı mevcut değildi."),
        "`sheetSource.kind` ve `rowIdsValidForCurrentPlayer` kaynağı açıkça taşır.",
        "Önceki satırların cached yazıları canlı değer gibi verilmez.",
        "`setCommand`/`readCommand` full key ile `statraw` kullanır.",
        "C paneli normal kullanımda hazır olduğunda `sheetstats` ile yeni Row ID'leri alınabilir.",
        "Stored `rowIdAtCapture` değerlerini yeni oyuna körlemesine gönderme.", "",
        "`added` düz katkı, `increased` yüzde artış katkısı, `more` ek çarpan katkısıdır.",
        "Üçünde de nötr değer **0**. `more 0.5` ×1.5, `more -0.5` ×0.5 demektir;",
        "`increased` ve `more` alt sınırı −1. Nihai karakter statını doğrudan atamıyorsun.",
        "Armor mitigation, cap, quotient/inverse, attack rate ve koşullu/derived satırlar",
        "oyunun hesabını izler. Level/XP/isim ve level'dan türeyen minion power combat",
        "modifier editor'ün dışında. `tags` PlayerProperty/AbilityProperty için indeks olabilir;",
        "AT bitlerini bu indekslere OR'lama. Secondary modifier full key'leri JSON'da ayrı.", "",
        "### Birim örnekleri", "", "| UI isteği | Gönderilecek değer / komut |", "|---|---|",
        "| Tüm dirençlere +65 yüzde puan | `stat allres 0.65` |",
        "| Strength'e +80 katkı | `stat strength 80` |",
        "| Bow / melee attack speed +500% increased | `stat bowattackspeed 5` / `stat meleeattackspeed 5` |",
        "| Parry +50 yüzde puan | `stat parry 0.5` |",
        "| Damage reflected +1000% | `stat reflect 10` |",
        "| +25% increased / +50% more | `increased 0.25` / `more 0.5` |",
        "| CoF düşman çift drop %65 | `cofdouble enemy 65` (100'e bölünmez) |",
        "| CoF T7 x5 | `coft7mult 5` (katsayı, yüzde değildir) |", "",
        "Yalnız `fraction` birimli alias'larda UI yüzde değeri /100 yapılır.",
        "Diğer `raw_game_units` değerlerinin tümünü yüzde gibi dönüştürme.",
        "`displayTypes` format bilgisi girdi birimiyle aynı şey değildir. Reference enum",
        "ID'leri kendi türüne aittir; ItemRarity ve ItemCreationRarityGroup numaraları karıştırılmaz.", "",
    ]
    for group, group_label in GROUPS.items():
        lines += [f"## {group_label}", "", "| Kontrol ID / adı | Komut | Aralık / seçenek | Başlangıç | Kalıcılık |", "|---|---|---|---|---|"]
        selected = [item for item in catalog["controls"] if item["group"] == group]
        for item in selected:
            ranges = []
            for p in item["parameters"]:
                if p["name"] in ("save_id", "name", "backup"): continue
                if "choices" in p: detail = "/".join(map(str, p["choices"]))
                elif p.get("maximum") is not None: detail = f'{p.get("minimum", "—")}…{p["maximum"]}'
                else: detail = p.get("optionsSource", p["type"])
                if p.get("runtimeBoundsSource"): detail += " (runtime sınırı)"
                if p["type"] == "number_or_reset": detail += "/reset"
                if p["type"] == "integer_or_none": detail += "/none"
                ranges.append(p["name"]+": "+detail)
            default = item["default"]
            if default is None: default = "normal rank" if item["id"].startswith("cof_double_") else "—"
            cmd = f'`{esc(item["commandTemplate"])}`' if item["commandTemplate"] else "Python API"
            lines.append(f'| `{item["id"]}` — {esc(item["label"])} | {cmd} | {esc("; ".join(ranges) or "—")} | {default} | {life[item["lifetime"]]} |')
        lines.append("")
        for item in selected:
            entry = f'- **{item["label"]}:** {item["description"]}'
            if item["pythonApi"]: entry += f' API: `{item["pythonApi"]}`.'
            if item["resetCommand"]: entry += f' Reset: `{item["resetCommand"]}`.'
            if item.get("resetCommandSource"): entry += f' Reset kaynağı: `{item["resetCommandSource"]}`.'
            lines.append(entry)
        lines.append("")
    lines += ["## Seçenek listeleri ve kalıcılık", "",
        "- `optionCatalogs.waypoints` yalnız `noWaypoint=false` kayıtları içerir: 160 harita sahne işaretinden 109 gerçek waypoint.",
        "- 85 eligible kampanya kaydı (41 ana, 44 yan). 148 toplam quest asset'inin test/repeatable/endgame/Minilith kapsamı bu butona dahil değildir.",
        "- Monolith kontrolünde `editable` gerçek runtime koşuludur. Düzenleme EoT/MonolithHub/M_Rest'te; normal corruption 0–50, Empowered 100–65535. Stability üst sınırı seçili difficulty asset'indedir.",
        "- Prophecy asset'in sırası ID değildir; `available=true` seçeneklerini göster. Yuva rank'ları 1/3/6/9. Lens rank/purchased koşulu ve mevcut seçimleri güncel okumadan al; değişikliği preview ile kontrol et.",
        "- CoF rank/Reputation/lens paylaşımı kasa/cycle/solo kurallarını izler. Üyelik ve prophecy karakter kaydını izler; UI profili ile bu ilerleme kayıtlarını karıştırma.",
        "- CoF Exalted/T7 native alanları doğrudan roll katsayısıdır (rank12: 1.5/2). LP uygun native bonusu ölçekler; LP 0–4 ve Weaver's Will istisnaları oyunda kalır.",
        "- Weaver/MG için `factionread` hazır. Weaver rank/Amber/tree yazma, MG yönetimi ve kalıcı mod profili henüz hazır backend kontrolü değildir.", "",
        "## Hazır 72 stat adı", "", "Aynı full key'e bağlı alias'lar aynı katkıyı değiştirir.", "",
        "| İsim | SP | Tags | Mode | Girdi birimi |", "|---|---:|---:|---|---|"]
    for a in catalog["characterStats"]["aliases"]:
        lines.append(f'| `{a["name"]}` | {a["key"]["sp"]} {a["key"]["property"]} | {a["key"]["tags"]} | {a["mode"]} | {a["unit"]} |')
    lines += ["", "## 134 SP türü", "", "Her SP için tags/special/extra ile ayrı full key oluşturulabilir; tek isim farklı saldırı/minion/ailment bağlamlarını kapsar.", "",
              "| ID | SP | ID | SP | ID | SP |", "|---:|---|---:|---|---:|---|"]
    properties = catalog["characterStats"]["properties"]
    for start in range(0, len(properties), 3):
        cells = []
        for p in properties[start:start+3]: cells.extend([str(p["id"]), p["name"]])
        cells.extend([""] * (6-len(cells)))
        lines.append("| " + " | ".join(cells) + " |")
    lines += ["", "## UI bağlantısı", "", "Proje kökünden Python örneği (aşağıdaki çağrılar UI olayında çalıştırılır):", "", "```python",
        "import json", "from pathlib import Path", "from tools import cof_backend, monolith_backend, progression_backend, le_session", "",
        'catalog = json.loads(Path("ui/catalog.json").read_text(encoding="utf-8"))',
        "", "def on_complete_campaign_click():", '    save_id = progression_backend.read()["player"]["id"]',
        '    return progression_backend.apply("questscomplete", save_id)', "",
        "def on_unlock_waypoints_click():", '    save_id = progression_backend.read()["player"]["id"]',
        '    return progression_backend.apply("waypointsunlock", save_id)', "",
        "def on_reward_multiplier_change(value):", '    return cof_backend.reward_multiplier(value)', "",
        '# Text cevaplı kontrol örneği, yalnız UI olayında çağrılır:', "def on_density_x3_click():",
        '    return le_session.send("density 3", timeout=15)', "```", "",
        "Komut şablonlarına yalnız doğrulanmış parametre koy. `integer_or_none` için Python `None`,",
        "native metin komutunda `none`; çift drop reset için Python `None`, native `reset` kullan.",
        "Kazanç ve bakiyeyi aynı input'a bağlama: `cofreputation` EKLER, `coffavor` mutlak bakiye yazar.", "",
        "## Kataloğu yenileme", "", "```powershell", "py -3 tools/export_ui_catalog.py --refresh", "```", "",
        "Yalnız okumaları çalıştırır. Stats/görev/ödül/waypoint/faction değişikliği, oyun restart'ı veya C panelini açma yapmaz.",
        "Progression okuması kendi normal harita refresh işlemini tamamlayıp önceki açık/kapalı duruma döner.",
        "C yoksa son doğrulanmış `research/live/character-sheet-catalog.txt` metadata'sı kullanılır ve kaynak açıkça etiketlenir.",
        "Exporter runtime SP/alias listesi, tüm normal komutların kapsamı, Python API imzaları, full key'ler, ID tekrarı ve snapshot karakter eşleşmesini doğrular.", "",
        "## Ayrıntılı kaynaklar", ""]
    for path in catalog["referenceFiles"]: lines.append(f"- [{path}](../{path})")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--refresh", action="store_true", help="Read-only IPC snapshot; no stat/action writes, no UI opening.")
    source.add_argument("--snapshot", type=Path, help="Existing read-only snapshot JSON.")
    parser.add_argument("--fields", type=Path, default=le_session.GAME / "EpochPact/dump/fields.tsv")
    parser.add_argument("--sheet-fallback", type=Path, default=ROOT / "research/live/character-sheet-catalog.txt")
    parser.add_argument("--output", type=Path, default=ROOT / "ui/catalog.json")
    parser.add_argument("--guide-output", type=Path, default=ROOT / "docs/ui-catalog.md")
    args = parser.parse_args()
    snapshot = refresh() if args.refresh else json.loads(args.snapshot.read_text(encoding="utf-8-sig"))
    catalog = build(snapshot, args.fields, args.sheet_fallback)
    captured = datetime.now(timezone(timedelta(hours=7))) if args.refresh else datetime.fromtimestamp(args.snapshot.stat().st_mtime, timezone(timedelta(hours=7)))
    catalog["snapshotCapturedAt"] = captured.isoformat(timespec="seconds")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(catalog, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    args.guide_output.parent.mkdir(parents=True, exist_ok=True)
    args.guide_output.write_text(guide(catalog), encoding="utf-8")
    print(json.dumps(catalog["counts"], ensure_ascii=False, indent=2))
    print("written:", args.output)
    print("written:", args.guide_output)


if __name__ == "__main__":
    main()
