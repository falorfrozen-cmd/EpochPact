# EpochPact — UI için tek özellik ve değer kataloğu

Ana veri dosyası: [ui/catalog.json](../ui/catalog.json). Bu rehber ve JSON,
[tools/export_ui_catalog.py](../tools/export_ui_catalog.py) ile aynı metadata'dan üretilir.
Native komutlar, Python API'leri, parametre sınırları, nötr başlangıç değerleri,
kalıcılık, gerçek seçenek ID'leri ve kaynak dosyalar burada bir arada.

Üretim: `2026-10-09T18:02:52+07:00`; oyun okumalarının zamanı: `2026-10-09T14:49:57+07:00` (Asia/Bangkok).
`default` nötr/başlangıç değeridir; `liveState`/`currentValues` yalnız tarihli okumadır.
JSON yüklemek hiçbir ayarı oyuna uygulamaz; kayıtlı mod profili oluşturmaz.

## İçerik

| Katalog | Adet |
|---|---:|
| Panel kontrolü / işlem | 78 |
| SP stat türü | 134 |
| Hazır stat adı | 72 |
| Doğrulanmış C satırı | 206 |
| C satırlarının farklı temel full key'i | 169 |
| AT tag adı | 30 |
| Ailment ID | 151 |
| Kampanya tamamlama kapsamındaki görev | 85 |
| Gerçek waypoint | 109 |
| Monolith timeline | 10 |
| Lens | 12 |
| Prophecy ödül asset'i | 123 |

## JSON alanları

| Alan | UI kullanımı |
|---|---|
| `groups`, `controls` | Türkçe panel grupları, sabit kontrol ID'si, widget, komut şablonu, Python API, cevap biçimi, parametreler, reset ve kalıcılık. |
| `characterStats.properties` | 134 SP adı ve gerçek ID. |
| `characterStats.aliases` | 72 hazır isim; full key, hangi mode'a yazdığı, birim dönüşümü ve reset komutu. |
| `characterStats.sheetRows` | 206 satır; full key, secondary modifier, paylaşılmış key, display/cap/derived metadata ve raw komutlar. |
| `characterStats.tags`, `ailments`, `displayTypes` | AT bitleri, AilmentID ve oyun format kodları. |
| `referenceEnums` | AbilityID, ConditionalDamageProperty, TrackerPropertyID, IdolAltarPropertyID; QuestType/QuestState/chapter ve farklı rarity enum'ları. |
| `optionCatalogs` | Filtrelenmiş 85 kampanya görevi / 109 waypoint; 10 timeline; 123 ödül; 12 lens; faction/rank verileri. |
| `liveState` | Ham ve tarihli session/progression/Monolith/CoF/faction okumaları. İşlemden önce yeniden oku. |
| `currentValues` | `status` içinde bildirilen genel ayarların tarihli değerleri; nihai combat toplamları değildir. |
| `researchOnly` | MG/Weaver yazma işlemleri ve kalıcı profiller için hazır olmayan kısımların durumu. |
| `referenceFiles`, `sources` | Derin araştırma ve runtime dump kaynağı/hash'i. |

## UI'nin izleyeceği kurallar

1. `sessionread` ile geçişin bittiğini doğrula; değişiklikler offline karakterde yapılır.
2. Kalıcı işlemlerin `save_id` değerini güncel `progressread`/ilgili modül okumasından al; örnek `0` kimliğini sabitleme.
3. Native IPC tek kanaldır. İstekleri sıraya koy; aynı anda birden çok komut yazma.
4. JSON cevaplı işlemlerde `ok` kontrol et, hata/backup bilgisini göster. Text cevaplı stat/genel komutları JSON sanarak parse etme.
5. `actor` stat ayarlarını aktör/karakter/harita yenilenmesinde uzlaştır; `session` çarpanlarını yeniden başlatınca istenen profil ile uygula.
6. `game_save` butonlarını profil yüklenince otomatik tekrar çalıştırma. Bunlar ilerleme, ödül veya bakiye değiştirir.
7. Aynı `sharedKey`/`statBinding` için iki ayrı kalıcı katkı oluşturma. `speed`, `stat movespeed` ve raw Movespeed aynı mod kaydını kullanır.
8. Katalogları/scene taramalarını ihtiyaç olduğunda yenile; her frame tekrar isteme.

Login/karakter seçimindeki bağlantı okumaları ve `playoffline`, yüklü offline aktör koşulu istemez.
Kontrolün `requirements` alanını kullan; tüm kontrolleri yalnız InGame durumunda açma.

### C satırlarının durumu

Eşlemeler bu snapshot'taki canlı C ekranından okundu.
`sheetSource.kind` ve `rowIdsValidForCurrentPlayer` kaynağı açıkça taşır.
Önceki satırların cached yazıları canlı değer gibi verilmez.
`setCommand`/`readCommand` full key ile `statraw` kullanır.
C paneli normal kullanımda hazır olduğunda `sheetstats` ile yeni Row ID'leri alınabilir.
Stored `rowIdAtCapture` değerlerini yeni oyuna körlemesine gönderme.

`added` düz katkı, `increased` yüzde artış katkısı, `more` ek çarpan katkısıdır.
Üçünde de nötr değer **0**. `more 0.5` ×1.5, `more -0.5` ×0.5 demektir;
`increased` ve `more` alt sınırı −1. Nihai karakter statını doğrudan atamıyorsun.
Armor mitigation, cap, quotient/inverse, attack rate ve koşullu/derived satırlar
oyunun hesabını izler. Level/XP/isim ve level'dan türeyen minion power combat
modifier editor'ün dışında. `tags` PlayerProperty/AbilityProperty için indeks olabilir;
AT bitlerini bu indekslere OR'lama. Secondary modifier full key'leri JSON'da ayrı.

### Birim örnekleri

| UI isteği | Gönderilecek değer / komut |
|---|---|
| Tüm dirençlere +65 yüzde puan | `stat allres 0.65` |
| Strength'e +80 katkı | `stat strength 80` |
| Bow / melee attack speed +500% increased | `stat bowattackspeed 5` / `stat meleeattackspeed 5` |
| Parry +50 yüzde puan | `stat parry 0.5` |
| Damage reflected +1000% | `stat reflect 10` |
| +25% increased / +50% more | `increased 0.25` / `more 0.5` |
| CoF düşman çift drop %65 | `cofdouble enemy 65` (100'e bölünmez) |
| CoF T7 x5 | `coft7mult 5` (katsayı, yüzde değildir) |

Yalnız `fraction` birimli alias'larda UI yüzde değeri /100 yapılır.
Diğer `raw_game_units` değerlerinin tümünü yüzde gibi dönüştürme.
`displayTypes` format bilgisi girdi birimiyle aynı şey değildir. Reference enum
ID'leri kendi türüne aittir; ItemRarity ve ItemCreationRarityGroup numaraları karıştırılmaz.

## Genel oyun ayarları

| Kontrol ID / adı | Komut | Aralık / seçenek | Başlangıç | Kalıcılık |
|---|---|---|---|---|
| `xp` — Öldürme / mote XP çarpanı | `xp {value}` | value: 1…100 | 1 | Oturum |
| `gold` — Yerden toplanan altın çarpanı | `gold {value}` | value: 1…100 | 1 | Oturum |
| `drops` — Genel eşya drop adedi çarpanı | `drops {value}` | value: 1…25 | 1 | Oturum |
| `density` — Monster density | `density {value}` | value: 1…5 | 1 | Oturum |
| `rarity` — Temel rarity yükseltme çarpanı | `rarity {value}` | value: 1…10 | 1 | Oturum |
| `speed` — Hareket hızı çarpanı | `speed {value}` | value: 1…5 | 1 | Yüklü aktör |
| `cooldown` — Cooldown / charge hız çarpanı | `cooldown {value}` | value: 1…10 | 1 | Oturum |
| `density_read` — Density ölçümü | `densityread` | — | — | Okuma |
| `map_reveal` — Reveal zone map | `mapreveal {value}` | value: 0…1 | 0 | Oturum |
| `map_read` — Map display status | `mapread` | — | — | Okuma |

- **Öldürme / mote XP çarpanı:** GainExpFromEnemyOrMote kazancı; görev XP ödüllerini çarpmaz. Reset: `xp 1`.
- **Yerden toplanan altın çarpanı:** Normal altın pickup kazancına uygulanır. Reset: `gold 1`.
- **Genel eşya drop adedi çarpanı:** ItemDrop.itemMultiplier; CoF düşman hook'u ile ortak dispatcher. Reset: `drops 1`.
- **Monster density:** Gelecekteki uygun normal paketlerin ortalama boyutu; mevcut paketler değişmez, boss/özel spawn hariç. Reset: `density 1`.
- **Temel rarity yükseltme çarpanı:** Her normal rarity roll'unu (x−1)/x ihtimalle bir kademe yükseltir; Exalted/T7 kontrolü ayrı. Reset: `rarity 1`.
- **Hareket hızı çarpanı:** Movespeed increased katkısı = x−1; movespeed alias/raw stat ile aynı full key'i paylaşır. Reset: `speed 1`.
- **Cooldown / charge hız çarpanı:** Normal charge tick hızını ve cooldown sorgusunu ölçekler. Reset: `cooldown 1`.
- **Density ölçümü:** Paket/generator/planlanan-gözlenen spawn ve dışlama sayaçları.
- **Reveal zone map:** Display the whole minimap/overlay using a fog shader substitution. Does not change exploration saves or spawn enemies. Off restores the current native fog texture. API: `tools.loot_crafting_backend.reveal_map(value)`. Reset: `mapreveal 0`.
- **Map display status:** Read map visibility and active hooks. API: `tools.loot_crafting_backend.map_read()`.

## C ekranı / karakter statları

| Kontrol ID / adı | Komut | Aralık / seçenek | Başlangıç | Kalıcılık |
|---|---|---|---|---|
| `sheet_catalog` — Tüm C satırlarını oku | `sheetstats` | — | — | Okuma |
| `sheet_row` — C satırı düzenleyici | `sheetstat {row} {mode} {value}` | row: fresh sheetstats row ID; mode: added/increased/more; value: -3.4028234663852886e+38…3.4028234663852886e+38 | 0 | Yüklü aktör |
| `raw_stat` — Full-key stat düzenleyici | `statraw {sp} {tags} {special} {extra} {mode} {value}` | sp: characterStats.properties.id; tags: -2147483648…2147483647; special: 0…255; extra: -2147483648…2147483647; mode: added/increased/more; value: -3.4028234663852886e+38…3.4028234663852886e+38 | 0 | Yüklü aktör |
| `stat_alias` — Hazır stat adları | `stat {name} {value}` | value: -3.4028234663852886e+38…3.4028234663852886e+38 | 0 | Yüklü aktör |
| `stat_reset` — Tüm mod stat katkılarını kaldır | `statreset` | — | — | Yüklü aktör |
| `sheet_open` — C panelini aç / kapat | `sheetopen {value}` | value: 0…1 | — | Görünüm |
| `resistance_labels` — Direnç yazılarını oku | `sheetread` | — | — | Okuma |

- **Tüm C satırlarını oku:** 206 doğrulanmış satır; güncel sheet instance gerektiğinde kullanılabilir.
- **C satırı düzenleyici:** Mod katkısını değiştirir. Secondary modifier için sheetstat {row} modifier {mode} {value}. Row ID instance'a aittir. Reset: `sheetstat {row} reset`.
- **Full-key stat düzenleyici:** 134 SP; kimlik SP/tags/special/extra. increased/more >= −1; nötr katkı tüm modlarda 0. Player/AbilityProperty tags indeks olabilir. Reset: `statraw {sp} {tags} {special} {extra} reset`.
- **Hazır stat adları:** characterStats.aliases içindeki isim/mode/birimleri kullan. Okumak için stat {name}. Reset kaynağı: `characterStats.aliases[name].resetCommand`.
- **Tüm mod stat katkılarını kaldır:** Yalnız EpochPact'in sahip olduğu kayıtları kaldırır; aynı key'i kullanan tüm UI kontrollerini yenile.
- **C panelini aç / kapat:** Normal UIBase aç/kapat; salt veri yenilemesi değildir.
- **Direnç yazılarını oku:** Yedi TMP_Text; sheet kapalı/yoksa kullanılamaz, inactive label eski olabilir.

## Loot ve toplama

| Kontrol ID / adı | Komut | Aralık / seçenek | Başlangıç | Kalıcılık |
|---|---|---|---|---|
| `autopickup` — Otomatik pickup | `autopickup {value}` | value: 0…1 | 0 | Oturum |
| `loot_read` — Akıllı toplama durumu / affix kataloğu | `lootread` | — | — | Okuma |
| `loot_mode` — Toplama modu | `lootmode {mode}` | mode: all/filter/quality/materials | all | Oturum |
| `loot_lp` — Unique minimum LP | `lootlp {value}` | value: 0…4 | 2 | Oturum |
| `loot_t7` — T7 eşya toplama | `loott7 {value}` | value: 0…1 | 1 | Oturum |
| `loot_filter` — Quality modunda loot filtresine uy | `lootfilter {value}` | value: 0…1 | 1 | Oturum |
| `loot_affixes` — Aranan T7 affix ID'leri | `lootaffixes {ids}` | ids: liveState.lootread.affixes.id | none | Oturum |
| `loot_category` — Toplama kategorisi | `lootcategory {category} {value}` | category: materials/gold/potions/xp/favor/bones; value: 0…1 | 1 | Oturum |
| `loot_reset` — Toplama seçimlerini sıfırla | `lootreset` | — | — | Oturum |

- **Otomatik pickup:** Normal pickup API'leri; eşya, altın, potion, tome ve bone. Tarama aralığı 0.75 saniye. Reset: `autopickup 0`.
- **Akıllı toplama durumu / affix kataloğu:** Seçimler ve sayaçlar; runtime affix ID/ad listesi. acceptedRequests envantere giriş garantisi değildir. API: `tools.loot_crafting_backend.loot_read()`.
- **Toplama modu:** all: tüm eşyalar; filter: mevcut loot filtresi; quality: Unique LP veya seçilen T7; materials: shard/rune/glyph. autopickup ayrıca açılır. API: `tools.loot_crafting_backend.loot_mode(mode)`. Reset: `lootmode all`.
- **Unique minimum LP:** Yalnız quality modu; LP eşiği ile T7 koşulu OR olarak birleştirilir. Weaver's Will ayrı bir LP koşulu değildir. API: `tools.loot_crafting_backend.minimum_lp(value)`. Reset: `lootlp 2`.
- **T7 eşya toplama:** Quality moduna aittir; filter modu filtreye her zaman uyar. API: `tools.loot_crafting_backend.t7(value)`. Reset: `loott7 1`.
- **Quality modunda loot filtresine uy:** Quality moduna aittir; filter modu filtreye her zaman uyar. API: `tools.loot_crafting_backend.respect_filter(value)`. Reset: `lootfilter 1`.
- **Aranan T7 affix ID'leri:** Virgülle ayrılmış en fazla 64 runtime affix ID; none herhangi bir T7 demektir. T7 açık olmalı. API: `tools.loot_crafting_backend.affixes_csv(ids)`. Reset: `lootaffixes none`.
- **Toplama kategorisi:** Crafting materyali, altın, potion, XP/Favor tome ve bone ayrı seçilir; kategori ayarı auto pickup'ı açmaz. API: `tools.loot_crafting_backend.category(category, value)`. Reset: `lootcategory {category} 1`.
- **Toplama seçimlerini sıfırla:** Seçimler all/default olur; autopickup açık/kapalı durumu ayrıca kontrol edilir. API: `tools.loot_crafting_backend.reset_loot()`.

## Crafting

| Kontrol ID / adı | Komut | Aralık / seçenek | Başlangıç | Kalıcılık |
|---|---|---|---|---|
| `craft_read` — Crafting önizleme / durum | `craftread` | — | — | Okuma |
| `craft_fp` — FP maliyet katsayısı | `craftfp {value}` | value: 0…1 | 1 | Oturum |
| `craft_hope` — Glyph of Hope FP koruma ihtimali | `crafthope {value}` | value: 0…100/reset | — | Oturum |
| `craft_despair` — Glyph of Despair seal ihtimali | `craftdespair {value}` | value: 0…100/reset | — | Oturum |
| `craft_shards` — Craft sırasında affix shard koru | `craftshards {value}` | value: 0…1 | 0 | Oturum |
| `craft_runes` — Craft runes | `craftrunes {value}` | value: 0…1 | 0 | Oturum |
| `craft_glyphs` — Craft glyphs | `craftglyphs {value}` | value: 0…1 | 0 | Oturum |
| `craft_level` — Craft level | `craftlevel {value}` | value: 0…1 | 0 | Oturum |
| `craft_forge` — Forge selected item | `craftforge {save_id}` | — | — | Oyun kaydı |
| `craft_reset` — Crafting ayarlarını normale döndür | `craftreset` | — | — | Oturum |

- **Crafting önizleme / durum:** Normal forge eligibility, eşya/affix/FP, shard craft maliyet aralığı ve glyph ihtimalleri; kesin RNG sonucu vermez. Rune/Insight maliyet aralığı gösterilmez. API: `tools.loot_crafting_backend.craft_read()`.
- **FP maliyet katsayısı:** 0 ücretsiz, 1 normal; örnek 0.5 yarı maliyet, yukarı yuvarlanır. FP üretmez, native eligibility korunur. API: `tools.loot_crafting_backend.fp_factor(value)`. Reset: `craftfp 1`.
- **Glyph of Hope FP koruma ihtimali:** Yüzde 0–100 veya reset; native glyph/affix uygunluğu gerekir. Despair T1–T4; Greater Hope ayrı desteklenmez. API: `tools.loot_crafting_backend.glyph_chance("hope", value)`. Reset: `crafthope reset`.
- **Glyph of Despair seal ihtimali:** Yüzde 0–100 veya reset; native glyph/affix uygunluğu gerekir. Despair T1–T4; Greater Hope ayrı desteklenmez. API: `tools.loot_crafting_backend.glyph_chance("despair", value)`. Reset: `craftdespair reset`.
- **Craft sırasında affix shard koru:** Gerçekten tüketilen bir shardı normal AddShard API ile geri verir. API: `tools.loot_crafting_backend.preserve_shards(value)`. Reset: `craftshards 0`.
- **Craft runes:** Local normal forge only; other item and equipment rules remain. API: `tools.loot_crafting_backend.preserve_runes(value)`. Reset: `craftrunes 0`.
- **Craft glyphs:** Local normal forge only; other item and equipment rules remain. API: `tools.loot_crafting_backend.preserve_glyphs(value)`. Reset: `craftglyphs 0`.
- **Craft level:** Local normal forge only; other item and equipment rules remain. API: `tools.loot_crafting_backend.bypass_level(value)`. Reset: `craftlevel 0`.
- **Forge selected item:** Perform exactly one normal craft on the current game forge selection. Requires an eligible item/material and matching offline character; snapshot before craft. API: `tools.loot_crafting_backend.forge(save_id)`.
- **Crafting ayarlarını normale döndür:** FP=1, doğal glyph şansları, normal shard tüketimi; tüm crafting hook'ları kaldırılır. API: `tools.loot_crafting_backend.reset_craft()`.

## Kampanya ve waypoint

| Kontrol ID / adı | Komut | Aralık / seçenek | Başlangıç | Kalıcılık |
|---|---|---|---|---|
| `progress_read` — Görev / waypoint kataloğu | `progressread` | — | — | Okuma |
| `campaign_complete` — Ana + yan kampanyayı tamamla, en az seviye 55 | `questscomplete {save_id}` | — | — | Oyun kaydı |
| `waypoints_unlock` — Tüm gerçek waypointleri aç | `waypointsunlock {save_id}` | — | — | Oyun kaydı |

- **Görev / waypoint kataloğu:** Yüklü id, seviye/XP/ödüller, eligibility ve gerçek waypoint sahneleri. API: `tools.progression_backend.read()`.
- **Ana + yan kampanyayı tamamla, en az seviye 55:** Henüz bitmemiş eligible kampanya görevleri ve normal ödülleri; minimum 55; End of Time, MonolithHub, M_Rest waypointleri. Tekrar ödül vermez. API: `tools.progression_backend.apply("questscomplete", save_id)`.
- **Tüm gerçek waypointleri aç:** Gerçek waypoint sahnelerini açar; Monolith timeline difficulty unlock ayrı işlemdir. API: `tools.progression_backend.apply("waypointsunlock", save_id)`.

## Monolith / endgame

| Kontrol ID / adı | Komut | Aralık / seçenek | Başlangıç | Kalıcılık |
|---|---|---|---|---|
| `echo_read` — Monolith Navigator | `echoread {save_id} {timeline} {difficulty}` | timeline: 1…254; difficulty: normal/empowered | — | Okuma |
| `echo_focus` — Show Echo in game | `echofocus {save_id} {timeline} {difficulty} {index}` | timeline: 1…254; difficulty: normal/empowered; index: 0…2147483647 | — | Görünüm |
| `monolith_read` — Monolith kataloğu / güncel run | `monolithread` | — | — | Okuma |
| `monolith_unlock` — Normal + Empowered timeline'ları aç | `monolithunlock {save_id}` | — | — | Oyun kaydı |
| `monolith_select` — Timeline / difficulty seç | `monolithselect {save_id} {timeline} {difficulty}` | timeline: 1…254; difficulty: normal/empowered | — | Oyun kaydı |
| `corruption` — Corruption | `corruption {save_id} {timeline} {difficulty} {value}` | timeline: 1…254; difficulty: normal/empowered; value: 0…65535 (runtime sınırı) | — | Oyun kaydı |
| `stability` — Stability | `stability {save_id} {timeline} {difficulty} {value}` | timeline: 1…254; difficulty: normal/empowered; value: 0…2147483647 (runtime sınırı) | — | Oyun kaydı |
| `stability_multiplier` — Stability kazanım çarpanı | `stabilitymult {value}` | value: 1…100 | 1 | Oturum |

- **Monolith Navigator:** Search rewards on an existing Echo web. This does not generate new Echoes. API: `tools.monolith_backend.echoes(save_id, timeline, empowered)`.
- **Show Echo in game:** Focus a visible Echo in the normal game map. Does not start or complete it. API: `tools.monolith_backend.focus_echo(save_id, timeline, empowered, index)`.
- **Monolith kataloğu / güncel run:** 10 timeline / 20 difficulty; sınırlar, unlock/run, editable ve seçim. API: `tools.monolith_backend.read()`.
- **Normal + Empowered timeline'ları aç:** Tüm normal/Empowered unlock; mevcutları atlar. API: `tools.monolith_backend.unlock(save_id)`.
- **Timeline / difficulty seç:** Normal panel seçimi; echo başlatmaz/teleport etmez. Python empowered parametresi bool. API: `tools.monolith_backend.select(save_id, timeline, empowered)`.
- **Corruption:** Seçili run/difficulty için mutlak değer; UI sınırlarını runtime asset'ten al. Python empowered bool. API: `tools.monolith_backend.corruption(save_id, timeline, empowered, value)`.
- **Stability:** Seçili run/difficulty için mutlak değer; UI sınırlarını runtime asset'ten al. Python empowered bool. API: `tools.monolith_backend.stability(save_id, timeline, empowered, value)`.
- **Stability kazanım çarpanı:** Pozitif doğal kazanç; kayıpları/elle mutlak değer ayarını çarpmaz. API: `tools.monolith_backend.multiplier(value)`. Reset: `stabilitymult 1`.

## Circle of Fortune

| Kontrol ID / adı | Komut | Aralık / seçenek | Başlangıç | Kalıcılık |
|---|---|---|---|---|
| `cof_read` — CoF kataloğu / güncel durum | `cofread` | — | — | Okuma |
| `cof_join` — CoF'ye katıl | `cofjoin {save_id}` | — | — | Oyun kaydı |
| `cof_rank` — CoF rank | `cofrank {save_id} {value}` | value: 1…12 | — | Oyun kaydı |
| `cof_favor` — Favor bakiyesi | `coffavor {save_id} {value}` | value: 0…999999 | — | Oyun kaydı |
| `cof_reputation_grant` — Reputation EKLE | `cofreputation {save_id} {value}` | value: 0…1000000 | 0 | Oyun kaydı |
| `cof_lenses_unlock` — Rank'ın izin verdiği lensleri aç | `coflenses {save_id}` | — | — | Oyun kaydı |
| `cofpreview` — Prophecy değişikliğini önizle | `cofpreview {save_id} {slot} {reward} {lens}` | slot: 0…3; reward: 0…65535/none; lens: 0…11/none | — | Okuma |
| `cofprophecy` — Prophecy ödül / lens seç | `cofprophecy {save_id} {slot} {reward} {lens}` | slot: 0…3; reward: 0…65535/none; lens: 0…11/none | — | Oyun kaydı |
| `cof_charges` — Tamamlanmış prophecy şarjları | `cofcharges {save_id} {slot} {value}` | slot: 0…3; value: 0…99 | — | Oyun kaydı |
| `cof_favor_multiplier` — Favor kazanım çarpanı | `coffavormult {value}` | value: 1…100 | 1 | Oturum |
| `cof_reputation_multiplier` — Reputation kazanım çarpanı | `cofrepmult {value}` | value: 1…100 | 1 | Oturum |
| `cof_charge_multiplier` — Prophecy şarj hızı | `cofchargemult {value}` | value: 1…100 | 1 | Oturum |
| `cof_reward_multiplier` — Prophecy ödül adedi | `cofrewardmult {value}` | value: 1…25 | 1 | Oturum |
| `cof_exalted_multiplier` — Exalted roll katsayısı | `cofexaltedmult {value}` | value: 1…100 | 1 | Oturum |
| `cof_t7_multiplier` — T7 roll katsayısı | `coft7mult {value}` | value: 1…100 | 1 | Oturum |
| `cof_lp_multiplier` — Unique LP roll katsayısı | `coflpmult {value}` | value: 1…100 | 1 | Oturum |
| `cof_double_enemy` — Düşman çift eşya ihtimali (%) | `cofdouble enemy {value}` | value: 0…100/reset | normal rank | Oturum |
| `cof_double_echo` — Normal Monolith çift eşya ihtimali (%) | `cofdouble echo {value}` | value: 0…100/reset | normal rank | Oturum |
| `cof_lens_celerity` — Celerity lens gücü | `coflensmult celerity {value}` | value: 1…100 | 1 | Oturum |
| `cof_lens_charity` — Charity lens gücü | `coflensmult charity {value}` | value: 1…100 | 1 | Oturum |
| `cof_lens_duplication` — Duplication lens gücü | `coflensmult duplication {value}` | value: 1…100 | 1 | Oturum |

- **CoF kataloğu / güncel durum:** Rank/Favor/Reputation/yuvalar/lensler/ödül uygunluğu ve tuning telemetrisi. API: `tools.cof_backend.read()`.
- **CoF'ye katıl:** MG'den geçiş ayrı açık seçim: cofjoin {save_id} switch; mevcut üyelik bırakılır. API: `tools.cof_backend.join(save_id, switch_from_merchant=False)`.
- **CoF rank:** Rank azaltma yuva/bonusları kapatabilir; rank içi Reputation sıfırlanır. API: `tools.cof_backend.rank(save_id, value)`.
- **Favor bakiyesi:** Mutlak bakiye; prophecy/Rep kazancı üretmez. API: `tools.cof_backend.favor(save_id, value)`.
- **Reputation EKLE:** Eklenecek miktar; mutlak bakiye ayarı değildir; gain çarpanını bypass eder. API: `tools.cof_backend.reputation(save_id, value)`.
- **Rank'ın izin verdiği lensleri aç:** Henüz alınmamış uygun lensleri normal satın alma yoluyla açar. API: `tools.cof_backend.unlock_lenses(save_id)`.
- **Prophecy değişikliğini önizle:** none seçimi kaldırır. Ödül değişince şarj sıfırlanır; lens değişikliğini önizle. Aynı ödül/lens iki yuvada olamaz. API: `tools.cof_backend.prophecy(save_id, slot, reward_id, lens_id, preview=True)`.
- **Prophecy ödül / lens seç:** none seçimi kaldırır. Ödül değişince şarj sıfırlanır; lens değişikliğini önizle. Aynı ödül/lens iki yuvada olamaz. API: `tools.cof_backend.prophecy(save_id, slot, reward_id, lens_id, preview=False)`.
- **Tamamlanmış prophecy şarjları:** 0–99 şarj; hemen eşya üretmez. Azaltma kesirli ilerlemeyi de sıfırlar. API: `tools.cof_backend.charges(save_id, slot, value)`.
- **Favor kazanım çarpanı:** Normal pozitif Favor; doğal türetilen Reputation ve prophecy akışı devam eder. API: `tools.cof_backend.favor_multiplier(value)`. Reset: `coffavormult 1`.
- **Reputation kazanım çarpanı:** Favor kazanım/harcamasından gelen Rep; elle ekleme hariç. Favor kazancında iki çarpan birleşir. API: `tools.cof_backend.reputation_multiplier(value)`. Reset: `cofrepmult 1`.
- **Prophecy şarj hızı:** Favor/Rep bakiyesinden bağımsız; Tyranny harcama maliyetini artırmaz. API: `tools.cof_backend.charge_multiplier(value)`. Reset: `cofchargemult 1`.
- **Prophecy ödül adedi:** Normal spawn başına temel adet üst sınırı 250; rank12/Duplication birleşir; ek şarj tüketmez. API: `tools.cof_backend.reward_multiplier(value)`. Reset: `cofrewardmult 1`.
- **Exalted roll katsayısı:** Native coefficient × ayar (rank12: 1.5); Rare→Exalted gerçek şansına da uygulanır. Yüzde değildir. API: `tools.cof_backend.exalted_multiplier(value)`. Reset: `cofexaltedmult 1`.
- **T7 roll katsayısı:** Native coefficient × ayar (rank12: 2); seviye/tier uygunluğu oyunda kalır. Yüzde değildir. API: `tools.cof_backend.t7_multiplier(value)`. Reset: `coft7mult 1`.
- **Unique LP roll katsayısı:** Yalnız native CoF bonusuna uygun Unique. LP 0–4; Weaver's Will istisnası; garanti LP adedi değildir. API: `tools.cof_backend.lp_multiplier(value)`. Reset: `coflpmult 1`.
- **Düşman çift eşya ihtimali (%):** Ayar doğrudan yüzde; 0 etkisiz çift şans, reset normal rank bonusu. Echo: eşya ödülü, Tomb/Gold/XP istisnaları normal. API: `tools.cof_backend.double_drop_chance("enemy", percent_or_None)`. Reset: `cofdouble enemy reset`.
- **Normal Monolith çift eşya ihtimali (%):** Ayar doğrudan yüzde; 0 etkisiz çift şans, reset normal rank bonusu. Echo: eşya ödülü, Tomb/Gold/XP istisnaları normal. API: `tools.cof_backend.double_drop_chance("echo", percent_or_None)`. Reset: `cofdouble echo reset`.
- **Celerity lens gücü:** +%50 ekstra şarj katkısını ölçekler: x3 → +%150 (toplam x2.5). API: `tools.cof_backend.lens_multiplier("celerity", value)`. Reset: `coflensmult celerity 1`.
- **Charity lens gücü:** Diğer yuvaya +%10 ekstra şarj katkısı: x5 → +%50. API: `tools.cof_backend.lens_multiplier("charity", value)`. Reset: `coflensmult charity 1`.
- **Duplication lens gücü:** %40 ek ödül şansını ölçekler; %100'e sınırlandırılır. API: `tools.cof_backend.lens_multiplier("duplication", value)`. Reset: `coflensmult duplication 1`.

## Faction / Weaver bilgileri

| Kontrol ID / adı | Komut | Aralık / seçenek | Başlangıç | Kalıcılık |
|---|---|---|---|---|
| `factions_read` — CoF / MG / Knights / Weaver bilgileri | `factionread` | — | — | Okuma |

- **CoF / MG / Knights / Weaver bilgileri:** Dört faction'ın rank asset'leri/durumları; Weaver earned points ve 13+40=53 sınırı. MG/Weaver mutation API hazır değil. API: `tools.progression_backend.request("factionread")`.

## Bağlantı ve karakter

| Kontrol ID / adı | Komut | Aralık / seçenek | Başlangıç | Kalıcılık |
|---|---|---|---|---|
| `session_read` — Bağlantı / geçiş durumu | `sessionread` | — | — | Okuma |
| `status` — Tüm mod ayarları / sayaçlar | `status` | — | — | Okuma |
| `player_read` — Yüklü karakter kimliği | `playerread` | — | — | Okuma |
| `characters` — Çevrimdışı karakterler | `characters` | — | — | Okuma |
| `play_offline` — Play Offline | `playoffline` | — | — | Görünüm |
| `load_character` — Çevrimdışı karakter yükle | `loadname {name} {level}` | level: 1…100 | — | Görünüm |

- **Bağlantı / geçiş durumu:** InGame ve transitioning=false kontrolü.
- **Tüm mod ayarları / sayaçlar:** Offline gate, etkin çarpanlar ve hata/hook durumları.
- **Yüklü karakter kimliği:** İsim/seviye/IsOffline; save id için progressread kullan.
- **Çevrimdışı karakterler:** Normal karakter seçim paneli okunur.
- **Play Offline:** Normal Login paneli; geçiş bittikten sonra çalışır.
- **Çevrimdışı karakter yükle:** Normal seçim tile double-click; boşluksuz tam isim, isteğe bağlı seviye. Sonra yüklü id'yi tekrar oku.

## Yedek / geri alma

| Kontrol ID / adı | Komut | Aralık / seçenek | Başlangıç | Kalıcılık |
|---|---|---|---|---|
| `undo` — Son işlemin yedeğini geri yükle | Python API | — | — | Geri alma |

- **Son işlemin yedeğini geri yükle:** Snapshot ve yüklü karakteri doğrular, oyunu normal kapatır, geri yükler. UI kullanıcının seçtiği işleme bağlamalı. API: `tools.progression_backend.undo(Path(backup))`.

## Unique Atlas

| Kontrol ID / adı | Komut | Aralık / seçenek | Başlangıç | Kalıcılık |
|---|---|---|---|---|
| `atlas_read` — Unique Atlas | `atlasread` | — | — | Okuma |

- **Unique Atlas:** Read the game's Unique/Set catalog and the loaded stash, inventory and equipment. Wishlist stays local. API: `tools.collection_backend.atlas()`.

## Stash Assistant

| Kontrol ID / adı | Komut | Aralık / seçenek | Başlangıç | Kalıcılık |
|---|---|---|---|---|
| `stash_read` — Stash Assistant | `stashread` | — | — | Okuma |

- **Stash Assistant:** Compare owned equipment and duplicate uniques. Protection is a local reminder; no items are changed. API: `tools.collection_backend.stash()`.

## Seçenek listeleri ve kalıcılık

- `optionCatalogs.waypoints` yalnız `noWaypoint=false` kayıtları içerir: 160 harita sahne işaretinden 109 gerçek waypoint.
- 85 eligible kampanya kaydı (41 ana, 44 yan). 148 toplam quest asset'inin test/repeatable/endgame/Minilith kapsamı bu butona dahil değildir.
- Monolith kontrolünde `editable` gerçek runtime koşuludur. Düzenleme EoT/MonolithHub/M_Rest'te; normal corruption 0–50, Empowered 100–65535. Stability üst sınırı seçili difficulty asset'indedir.
- Prophecy asset'in sırası ID değildir; `available=true` seçeneklerini göster. Yuva rank'ları 1/3/6/9. Lens rank/purchased koşulu ve mevcut seçimleri güncel okumadan al; değişikliği preview ile kontrol et.
- CoF rank/Reputation/lens paylaşımı kasa/cycle/solo kurallarını izler. Üyelik ve prophecy karakter kaydını izler; UI profili ile bu ilerleme kayıtlarını karıştırma.
- CoF Exalted/T7 native alanları doğrudan roll katsayısıdır (rank12: 1.5/2). LP uygun native bonusu ölçekler; LP 0–4 ve Weaver's Will istisnaları oyunda kalır.
- Weaver/MG için `factionread` hazır. Weaver rank/Amber/tree yazma, MG yönetimi ve kalıcı mod profili henüz hazır backend kontrolü değildir.

## Hazır 72 stat adı

Aynı full key'e bağlı alias'lar aynı katkıyı değiştirir.

| İsim | SP | Tags | Mode | Girdi birimi |
|---|---:|---:|---|---|
| `strength` | 19 Strength | 0 | added | raw_game_units |
| `vitality` | 20 Vitality | 0 | added | raw_game_units |
| `intelligence` | 21 Intelligence | 0 | added | raw_game_units |
| `dexterity` | 22 Dexterity | 0 | added | raw_game_units |
| `attunement` | 23 Attunement | 0 | added | raw_game_units |
| `health` | 7 Health | 0 | added | raw_game_units |
| `mana` | 8 Mana | 0 | added | raw_game_units |
| `healthregen` | 17 HealthRegen | 0 | added | raw_game_units |
| `manaregen` | 18 ManaRegen | 0 | added | raw_game_units |
| `movespeed` | 9 Movespeed | 0 | increased | fraction |
| `fire` | 13 FireResistance | 0 | added | fraction |
| `cold` | 14 ColdResistance | 0 | added | fraction |
| `lightning` | 15 LightningResistance | 0 | added | fraction |
| `void` | 26 VoidResistance | 0 | added | fraction |
| `necrotic` | 27 NecroticResistance | 0 | added | fraction |
| `poison` | 28 PoisonResistance | 0 | added | fraction |
| `physical` | 64 PhysicalResistance | 0 | added | fraction |
| `allres` | 30 AllResistances | 0 | added | fraction |
| `armor` | 10 Armour | 0 | added | raw_game_units |
| `dodge` | 11 DodgeRating | 0 | added | raw_game_units |
| `stunavoid` | 12 StunAvoidance | 0 | added | raw_game_units |
| `wardretention` | 16 WardRetention | 0 | added | fraction |
| `block` | 29 BlockChance | 0 | added | fraction |
| `blockeffect` | 53 BlockEffectiveness | 0 | added | raw_game_units |
| `endurance` | 75 Endurance | 0 | added | fraction |
| `endurancethreshold` | 76 EnduranceThreshold | 0 | added | raw_game_units |
| `critavoid` | 89 CritAvoidance | 0 | added | fraction |
| `glancing` | 62 GlancingBlowChance | 0 | added | fraction |
| `parry` | 121 ParryChance | 0 | added | fraction |
| `wardregen` | 92 WardRegen | 0 | added | raw_game_units |
| `warddecay` | 119 WardDecayThreshold | 0 | added | raw_game_units |
| `healthleech` | 51 HealthLeech | 0 | added | fraction |
| `freezerate` | 67 FreezeRateMultiplier | 0 | increased | fraction |
| `cooldownrecovery` | 70 IncreasedCooldownRecoverySpeed | 0 | increased | fraction |
| `increasedleech` | 102 IncreasedLeechRate | 0 | increased | fraction |
| `reflect` | 86 PercentReflect | 0 | added | fraction |
| `damagereflected` | 86 PercentReflect | 0 | added | fraction |
| `critchance` | 4 CriticalChance | 0 | increased | fraction |
| `critmulti` | 5 CriticalMultiplier | 0 | increased | fraction |
| `castspeed` | 3 CastSpeed | 0 | increased | fraction |
| `attackspeed` | 2 AttackSpeed | 0 | increased | fraction |
| `meleeattackspeed` | 2 AttackSpeed | 512 | increased | fraction |
| `bowattackspeed` | 2 AttackSpeed | 2048 | increased | fraction |
| `throwingattackspeed` | 2 AttackSpeed | 1024 | increased | fraction |
| `damage` | 0 Damage | 0 | increased | fraction |
| `meleedamage` | 0 Damage | 512 | increased | fraction |
| `bowdamage` | 0 Damage | 2048 | increased | fraction |
| `spelldamage` | 0 Damage | 256 | increased | fraction |
| `throwingdamage` | 0 Damage | 1024 | increased | fraction |
| `dotdamage` | 0 Damage | 4096 | increased | fraction |
| `firedamage` | 0 Damage | 8 | increased | fraction |
| `colddamage` | 0 Damage | 4 | increased | fraction |
| `lightningdamage` | 0 Damage | 2 | increased | fraction |
| `physicaldamage` | 0 Damage | 1 | increased | fraction |
| `voiddamage` | 0 Damage | 16 | increased | fraction |
| `necroticdamage` | 0 Damage | 32 | increased | fraction |
| `poisondamage` | 0 Damage | 64 | increased | fraction |
| `firepen` | 59 Penetration | 8 | added | fraction |
| `coldpen` | 59 Penetration | 4 | added | fraction |
| `lightningpen` | 59 Penetration | 2 | added | fraction |
| `physicalpen` | 59 Penetration | 1 | added | fraction |
| `voidpen` | 59 Penetration | 16 | added | fraction |
| `necroticpen` | 59 Penetration | 32 | added | fraction |
| `poisonpen` | 59 Penetration | 64 | added | fraction |
| `miniondamage` | 0 Damage | 8192 | increased | fraction |
| `minionhealth` | 7 Health | 8192 | added | raw_game_units |
| `minionmovespeed` | 9 Movespeed | 8192 | increased | fraction |
| `minionattackspeed` | 2 AttackSpeed | 8704 | increased | fraction |
| `minioncastspeed` | 3 CastSpeed | 8192 | increased | fraction |
| `minioncritchance` | 4 CriticalChance | 8192 | increased | fraction |
| `minioncritmulti` | 5 CriticalMultiplier | 8192 | increased | fraction |
| `maxcompanions` | 61 MaximumCompanions | 0 | added | raw_game_units |

## 134 SP türü

Her SP için tags/special/extra ile ayrı full key oluşturulabilir; tek isim farklı saldırı/minion/ailment bağlamlarını kapsar.

| ID | SP | ID | SP | ID | SP |
|---:|---|---:|---|---:|---|
| 0 | Damage | 1 | AilmentChance | 2 | AttackSpeed |
| 3 | CastSpeed | 4 | CriticalChance | 5 | CriticalMultiplier |
| 6 | DamageTaken | 7 | Health | 8 | Mana |
| 9 | Movespeed | 10 | Armour | 11 | DodgeRating |
| 12 | StunAvoidance | 13 | FireResistance | 14 | ColdResistance |
| 15 | LightningResistance | 16 | WardRetention | 17 | HealthRegen |
| 18 | ManaRegen | 19 | Strength | 20 | Vitality |
| 21 | Intelligence | 22 | Dexterity | 23 | Attunement |
| 24 | ManaBeforeHealthPercent | 25 | ChannelCost | 26 | VoidResistance |
| 27 | NecroticResistance | 28 | PoisonResistance | 29 | BlockChance |
| 30 | AllResistances | 31 | DamageTakenAsPhysical | 32 | DamageTakenAsFire |
| 33 | DamageTakenAsCold | 34 | DamageTakenAsLightning | 35 | DamageTakenAsNecrotic |
| 36 | DamageTakenAsVoid | 37 | DamageTakenAsPoison | 38 | HealthGain |
| 39 | WardGain | 40 | ManaGain | 41 | AdaptiveSpellDamage |
| 42 | IncreasedAilmentDuration | 43 | IncreasedAilmentEffect | 44 | IncreasedHealing |
| 45 | IncreasedStunChance | 46 | AllAttributes | 47 | IncreasedPotionDropRate |
| 48 | PotionHealth | 49 | PotionSlots | 50 | HasteOnHitChance |
| 51 | HealthLeech | 52 | ElementalResistance | 53 | BlockEffectiveness |
| 54 | None | 55 | IncreasedStunImmunityDuration | 56 | StunImmunity |
| 57 | ManaDrain | 58 | AbilityProperty | 59 | Penetration |
| 60 | CurrentHealthDrain | 61 | MaximumCompanions | 62 | GlancingBlowChance |
| 63 | CullPercentFromPassives | 64 | PhysicalResistance | 65 | CullPercentFromWeapon |
| 66 | ManaCost | 67 | FreezeRateMultiplier | 68 | IncreasedChanceToBeFrozen |
| 69 | ManaEfficiency | 70 | IncreasedCooldownRecoverySpeed | 71 | ReceivedStunDuration |
| 72 | NegativePhysicalResistance | 73 | ChillRetaliationChance | 74 | SlowRetaliationChance |
| 75 | Endurance | 76 | EnduranceThreshold | 77 | NegativeArmour |
| 78 | NegativeFireResistance | 79 | NegativeColdResistance | 80 | NegativeLightningResistance |
| 81 | NegativeVoidResistance | 82 | NegativeNecroticResistance | 83 | NegativePoisonResistance |
| 84 | NegativeElementalResistance | 85 | Thorns | 86 | PercentReflect |
| 87 | ShockRetaliationChance | 88 | LevelOfSkills | 89 | CritAvoidance |
| 90 | PotionHealthConvertedToWard | 91 | WardOnPotionUse | 92 | WardRegen |
| 93 | OverkillLeech | 94 | ManaBeforeWardPercent | 95 | IncreasedStunDuration |
| 96 | MaximumHealthGainedAsEnduranceThreshold | 97 | ChanceToGain30WardWhenHit | 98 | PlayerProperty |
| 99 | ManaSpentGainedAsWard | 100 | AilmentConversion | 101 | PerceivedUnimportanceModifier |
| 102 | IncreasedLeechRate | 103 | MoreFreezeRatePerStackOfChill | 104 | IncreasedDropRate |
| 105 | IncreasedExperience | 106 | PhysicalAndVoidResistance | 107 | NecroticAndPoisonResistance |
| 108 | DamageTakenBuff | 109 | IncreasedChanceToBeStunned | 110 | DamageTakenFromNearbyEnemies |
| 111 | BlockChanceAgainstDistantEnemies | 112 | ChanceToBeCrit | 113 | DamageTakenWhileMoving |
| 114 | ReducedBonusDamageTakenFromCrits | 115 | DamagePerStackOfAilment | 116 | IncreasedAreaForAreaSkills |
| 117 | GlobalConditionalDamage | 118 | ArmourMitigationAppliesToDamageOverTime | 119 | WardDecayThreshold |
| 120 | EffectOfAilmentOnYou | 121 | ParryChance | 122 | CircleOfFortuneLensEffect |
| 123 | TrackerProperty | 124 | UnimportanceModifier | 125 | FreezeImmunity |
| 126 | ChanceToCastForAbility | 127 | ChanceToCastForTags | 128 | AilmentImmunity |
| 129 | AbilityRetaliationChance | 130 | IdolAltarProperty | 131 | GlobalConditionalPenetration |
| 132 | GlobalConditionalCritChance | 133 | GlobalConditionalCritMulti |  |  |

## UI bağlantısı

Proje kökünden Python örneği (aşağıdaki çağrılar UI olayında çalıştırılır):

```python
import json
from pathlib import Path
from tools import cof_backend, monolith_backend, progression_backend, le_session

catalog = json.loads(Path("ui/catalog.json").read_text(encoding="utf-8"))

def on_complete_campaign_click():
    save_id = progression_backend.read()["player"]["id"]
    return progression_backend.apply("questscomplete", save_id)

def on_unlock_waypoints_click():
    save_id = progression_backend.read()["player"]["id"]
    return progression_backend.apply("waypointsunlock", save_id)

def on_reward_multiplier_change(value):
    return cof_backend.reward_multiplier(value)

# Text cevaplı kontrol örneği, yalnız UI olayında çağrılır:
def on_density_x3_click():
    return le_session.send("density 3", timeout=15)
```

Komut şablonlarına yalnız doğrulanmış parametre koy. `integer_or_none` için Python `None`,
native metin komutunda `none`; çift drop reset için Python `None`, native `reset` kullan.
Kazanç ve bakiyeyi aynı input'a bağlama: `cofreputation` EKLER, `coffavor` mutlak bakiye yazar.

## Kataloğu yenileme

```powershell
py -3 tools/export_ui_catalog.py --refresh
```

Yalnız okumaları çalıştırır. Stats/görev/ödül/waypoint/faction değişikliği, oyun restart'ı veya C panelini açma yapmaz.
Progression okuması kendi normal harita refresh işlemini tamamlayıp önceki açık/kapalı duruma döner.
C yoksa son doğrulanmış `research/live/character-sheet-catalog.txt` metadata'sı kullanılır ve kaynak açıkça etiketlenir.
Exporter runtime SP/alias listesi, tüm normal komutların kapsamı, Python API imzaları, full key'ler, ID tekrarı ve snapshot karakter eşleşmesini doğrular.

## Ayrıntılı kaynaklar

- [GEMINI.md](../GEMINI.md)
- [research/stat-map.md](../research/stat-map.md)
- [research/findings.md](../research/findings.md)
- [docs/stat-editor.md](../docs/stat-editor.md)
- [docs/monster-density.md](../docs/monster-density.md)
- [docs/progression.md](../docs/progression.md)
- [docs/monolith.md](../docs/monolith.md)
- [docs/cof.md](../docs/cof.md)
- [docs/loot-crafting.md](../docs/loot-crafting.md)
- [docs/ce-feature-additions.md](../docs/ce-feature-additions.md)
- [research/factions-1.5.md](../research/factions-1.5.md)
- [research/weaver-1.5.md](../research/weaver-1.5.md)
