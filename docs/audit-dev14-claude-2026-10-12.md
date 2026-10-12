# EpochPact denetimi — 0.1.2-dev.14 / package.2 (Claude, 2026-10-12)

Kaynak: `fix/review-20261009` @ `34a66eca4088be4cbadeaa9255ff165782ef7fa6`.
Paket: `EpochPact-0.1.2-dev.14-package.2.zip`, SHA-256 `ad2b6316…348ec0`. GitHub release asset digest'i aynı.

**Yöntem:** Her madde için önce kod okundu, sonra kanıt dokümanı ve o kanıtın hangi sürüm/çekirdekte üretildiği kontrol edildi. Ek olarak:
- Paket ZIP'inin içeriği ve hash'leri açılıp kontrol edildi.
- GitHub repo, release ve dal durumu `gh` ile okundu.
- Testler ayrı bir worktree'de (34a66ec) yeniden derlenip çalıştırıldı. Sonuç en altta.

Bu denetimde oyun açılmadı ve canlı gameplay testi yapılmadı.

**Durum kodları:**
- ✅ **Tamamlandı**: kod var ve güncel koda uygulanan kanıt var.
- 🟡 **Kısmen**: kod var, ama kanıt eski sürümden, kapsam eksik ya da bilinen bir açık var.
- ❌ **Eksik**: kodda ya da yayında yok.
- ❔ **Doğrulanmadı**: kod var, ama ilgili kanıt yok.

**Kanıtın yaşı kuralı:** Bir dosya, kanıtı üreten commit'ten sonra değişmediyse eski canlı kanıt aynı kod için geçerli sayıldı. Değiştiyse yalnızca "geçmiş kanıt" sayıldı.
- `build_controls.cpp` son değişikliği dev.9 (`25d3f8e`).
- `items.cpp`, `workshop.cpp`, `stat_editor.cpp`, `smart_loot.cpp` son değişikliği dev.6 (`b22f8a0`).
- `crafting.cpp` son değişikliği dev.13 (`263a7be`).
- `monolith.cpp`, `cof*.cpp` son değişikliği 10-09.
- `progression.cpp` son değişikliği dev.11 (`6d63d84`).
- `convenience.cpp` son değişikliği dev.14.

---

## Öne çıkan yeni bulgular (ChatGPT'nin raporlarında yok)

1. **GitHub `main` dalı hâlâ Round 0'da (`71d5094`, 2026-10-02).**
   - Repo public ve varsayılan dal `main`. Repoyu açan biri 2 Ekim kodunu ve 474 baytlık eski README'yi görüyor.
   - Güncel kod yalnız `fix/review-20261009` dalında. Release tag'leri doğru commit'e bağlı.
2. **dev.14 ZIP içindeki README reddedilen tasarımı anlatıyor.**
   - `docs/player-folder-readme.md:61` → pakette `README.md`: *"horizontal row after Sort, inside the inventory footer"*.
   - Onaylı tasarım ise item grid'in altında ayrı sıra.
   - `docs/player-conveniences.md:46` de hâlâ *"separate vertical strip beside the inventory"* diyor.
3. **Grafik efekti kapatmak periyodik takılma riski taşıyor (ölçülmedi).**
   - `convenience.cpp` `Frame()`, `Graphics()`'i her 1 saniyede çağırıyor.
   - Reflections, bloom, motion blur, AO veya fog açıkken her çağrıda `FindObjectsOfType` ile 1–3 sahne araması yapılıyor (`PostProcessVolume`, `HxVolumetricCamera`, `ReflectionProbe`).
   - Bu, envanterde düzeltilen sorunla aynı desen. O sorunda bir sahne araması bu makinede 31–51 ms ölçülmüştü.
   - dev.5 raporu açıkça "No FPS benchmark was performed" diyor.
4. **Hook kurulum anı takılmaları hâlâ var.** Hook kurulumu bütün thread'leri dondurur; bu makinede kurulum başına ~250 ms ölçüldü.
   - Chest/shrine/portal switch'inin ilk açılışı 5 hook kuruyor (`convenience.cpp:674-685`). Tahminen ~1 sn tek seferlik donma.
   - Campaign complete 2 hook kurup kaldırıyor (`progression.cpp:614-621, 697-714`). Bu süre raporlanan "frame gap" metriğinin dışında kalıyor.
   - Summons sayfasının ilk okuması `ApplyDamage` hook'unu kuruyor (`convenience.cpp:701`).
   - Envanterdeki 0,46 sn sorunuyla aynı sınıf.
5. **dev.14 `version.dll` yeni bir binary ve hiç taranmadı.**
   - Loader 2ba920e'de değişti: DllMain'de thread oluşturuyor, `loader.log` ve `%TEMP%` yazıyor.
   - Hash `d7ca8d9c…352cf`. VirusTotal veya Defender sonucu yok.
   - dev.10'da Defender ML (`Wacatac.C!ml`) tam olarak `version.dll`'i yakalamıştı. Nexus'a göndermeden önce taranmalı.
6. **Nexus sayfası güncel değil.**
   - `docs/nexus/publication.json` hâlâ **0.1.1-alpha.1**; iki dosya *quarantined*.
   - `description.bbcode` alpha.1 ve karantina metnini anlatıyor. dev.14 Nexus'a yüklenmemiş.
7. **Uygulamada "Uninstall" butonu yok.**
   - `le_session.cmd_uninstall` yalnız CLI'dan çalışıyor. README kaldırmayı elle dosya silme olarak anlatıyor.
   - MelonLoader köprüsünün kaldırılması da elle yapılıyor.
8. **Discord destek linki eksik yerlerde.**
   - Var: paket README'si, release notu, Nexus açıklaması, Steam taslağı.
   - Yok: repo kök `README.md` ve uygulama arayüzü.
9. **Küçük tutarsızlıklar:**
   - `ui/catalog.json` ve `GEMINI.md` Auto Pickup taramasını "0.75 s" diyor; kod 250 ms (`items.cpp:310`).
   - `ce-feature-additions.md` (10-09) set/offhand/tree bypass ve projectiles için "Not connected" diyor. Bunlar artık uygulanmış; doküman eskimiş.
   - Eski `docs/promo/epochpact-discord-chronoforge-v1.png` hâlâ "NO PYTHON INSTALL" içeriyor. Yanlışlıkla kullanılma riski var.
   - dev.14 patch-notes posteri yok; son poster dev.6.
   - Repoda iki eski **Pre-release** duruyor: `v0.1.1-alpha.5` ve `v0.1.0-alpha.20261009`.
   - `install-backups/` her güncellemede büyüyor; bir sınırı yok.

---

## A. Düzeltilmesi istenen hatalar (1–27)

| # | Madde | Sonuç | Kod / kanıt | Kalan sınır |
|---|---|---|---|---|
| 1 | Kampanya tamamlamada donma, ödül tekrarı | 🟡 | `progression.cpp:628-692`: kare başına tek quest, analytics ve display batching. Completed quest atlanıyor, tekrar ödül yok. Kanıt (10-08): 84 quest 4,08 sn, en büyük frame gap 53 ms, tekrar çalıştırmada ödül/XP artmadı. | Kanıt eski çekirdekten; dev.11'de dosya değişti, tekrar ölçülmedi. 2 hook kurulum/kaldırma donması metriğe girmiyor (bulgu 4). |
| 2 | Yavaş stat okuma / bağlantı sorgularının kilitlemesi | 🟡 | "Current bonus reading…" metni arayüzde artık yok. Stat okuma ana thread'de; bağlantı yoklaması hazır durumda 10 sn'de bir; Summons 2 sn, World 5 sn (dev.14). Ölçüm (10-08): tam stat okuma ort. 122 ms. | Bağlantı turu 5 ana-thread işi yapıyor (ping/status/session/identity/statraw); dev.14'te ölçülmedi. |
| 3 | `EpochPact.cmd` boş ekran / EXE açılmaması / `exe.config` | ✅ | `EpochPact.exe.config` ZIP'te EXE yanında. `epochpact_desktop.py` CLR hatasını açıklayan mesaj veriyor. `test_desktop_startup.py` config yokken hatayı, varken açılmayı test ediyor. Port çakışma ve konsol hatası ele alınıyor. | `.cmd` yalnız kaynak için ve Python ister; onun için otomatik test yok. |
| 4 | Game setup / dosya seçici | ✅ | `player_setup.py:picker_directory` Steam kökünden başlıyor. Her `Last Epoch.exe` doğrulanıp kabul ediliyor. Launcher kenar çubukta ve Settings'te. | Etkileşimli seçim yalnız kullanıcının daha önceki onayıyla doğrulanmış. |
| 5 | `WinError 5` / `new-2` / salt-okunur / staging / cleanup | 🟡 | `le_session.cmd_install`: staging + hash, salt-okunur bayrak temizleme ve geri yükleme, rollback, cleanup hatası kurulumu bozmuyor. Hata adımı `install_step` ile UI'ya taşınıyor ("…while trying to copy version.dll"), genel mesajla örtülmüyor. Fixture testleri geçti. | `new-2` hatası büyük olasılıkla AV'nin DLL yazımını engellemesi; kod bunu düzeltemez, yalnız doğru raporlar. Başka bir oyuncu makinesinde tekrar sınanmadı. |
| 6 | Yönetici kurulumu | 🟡 | "Install as administrator" ayrı buton. UAC iptali (1223) ayrı mesaj. Çıkış kodları 20/21, sonuç dosyası ve çelişkili sonuç ayrı ayrı raporlanıyor. Rollback yükseltilmiş süreçte aynı transaction'la. | Gerçek UAC tıklaması test edilmedi (dokümanlar da söylüyor). 120 sn sonra "still running" denip ebeveyn çıkıyor; sonuç sonradan doğrulanmıyor. |
| 7 | Connecting / disconnected / not responding / offline bekleme | ✅ | Kök neden lazy loader startup'tı (dev.13.1, `2ba920e`). dev.14'te korunuyor (`version_proxy.cpp:185-201`) ve `loader.log` ekleniyor. Oyuncu dev.13.1'de "It's working" dedi. Faz etiketleri ayrı (`app.js:310`). | Diğer donanım/AV kombinasyonları bilinmiyor. |
| 8 | Oyun yeniden başlayınca | ✅ | `ui_bridge._sync_process`: sessionKey değişince cache, actor, history, temporary resets ve verified temizleniyor. Kanıt: dev.11 canlı restart testi, aynı EXE. | dev.14'te tekrar canlı sınanmadı (kod dev.11'den beri aynı mantık). |
| 9 | Protokol 3, nonce, PID:FILETIME | ✅ | `ipc_protocol.hpp`: bağlı istek yanlış oturumdaysa çalıştırılmadan reddediliyor. `le_session.send_packet`: 128-bit nonce, cevapta PID+sessionKey zorunlu. `protocol.json` opsiyonel. Native `ipc_protocol_test` mevcut. | Parser bağsız (`!` olmayan) ve legacy komutları hâlâ kabul ediyor. GUI her zaman bağlı gönderiyor; CLI uyumluluğu için bilinçli bir sınır. |
| 10 | Ayrı 8 sn bütçeler | ✅ | `ui_bridge.connection()`: ping/status/sessionread/identityread/statraw her biri `timeout=8`. Ortak 7 sn bütçe yok. ZIP simülatörü 8.187 ms'lik gecikmeli okumayı geçti. | Simülatör ölçümü; oyun yanıt süresi değil. |
| 11 | DLL bağlantısı ≠ karakter hazır | ✅ | `identityread` `Current(false)` ile quest listesi istemiyor. Fazlar ayrı: `character_loading`, `waiting_frame`, `read_error`. Gameplay kontrolleri `offline && player` olmadan kapalı. | — |
| 12 | Paket uyuşmazlığı | ✅ | `player_setup.status`: `updateRequired = is_ours_core && hash≠`. UI'da "Mod update required" etiketi var. Kurulum tam SHA-256 eşleşmesi istiyor. | — |
| 13 | Monolith Apply / doğru timeline-difficulty | 🟡 | Snapshot karakter+alan eşleşmeli. Taslak anahtarı `owner:timeline:difficulty:id`. Native `Corruption/Stability(id,timeline,difficulty,…)` sınırları kontrol ediyor. Canlı kanıt 10-09: 20 kombinasyon, 135 UI kontrolü. | app.js ve bağlantı katmanı dev.11'de değişti; Apply butonları o tarihten beri canlı sınanmadı. |
| 14 | Taslakların sıfırlanması | ✅ | `rememberMonolithOwner`: yalnız settled farklı karakter taslağı siliyor. Odak okuması formu yeniden çizmiyor. JS testleri var; 10-09'da alt-tab/refresh senaryoları test edildi. | Game setup'ta oyun yolu değişince taslaklar bilinçli olarak sıfırlanıyor. |
| 15 | Oyuncuya ham JSON / debug araçları | ✅ | `resultDialog`: developer değilse yalnız bildirim (CoF preview özetli). `developerControls` gizli. Frozen EXE `--developer`'ı reddediyor. | — |
| 16 | General Settings otomatik uygulama | ✅ | `flushAutomatic`; save-changing kontroller `game_save` lifetime'lı ve onaylı buton. dev.2 headless DOM testi + JS testleri. | Gerçek WebView2 etkileşimli testi değil. |
| 17 | Auto Pickup tooltip / pulsating | 🟡 | `items.cpp:96-140,296-313`: legacy + V2 tooltip, pending hover rect, tooltip lock; tooltip açıkken vanilla distant tick de atlanıyor. Canlı (dev.2/dev.5): kilitli kart açıkken 8/8 eşya yerde kaldı, kapanınca toplandı. `items.cpp` dev.6'dan beri aynı. | Fiziksel fare/controller hover ve "pulsating" belirtisi ayrıca kanıtlanmadı; testler IPC ile açılan kilitli kartla yapıldı. Katalog 0,75 sn diyor, kod 250 ms. |
| 18 | Chest/shrine/portal takılması | 🟡 | Bootstrap tek tip/kare, sonra Awake/OnInitialize ile kayıt. Bütçe ~1,5 ms, kare başına 1 etkileşim. dev.9 ölçümü: p99 14,3 ms, max 16 ms (tekrar koşu). | İlk açılışta 5 hook (bulgu 4). Sahne başına 6 arama (40–126 ms). Grafik ayarları açıkken saniyelik arama (bulgu 3). Kalabalık savaş/zayıf PC ölçülmedi. |
| 19 | Envanter takılması, `perf/stutter-fix` korunmalı | ✅ | `6150c16` merge; `InventoryVisible()` hızlı yolu, startup hook'lar ve OnEnable panel cache HEAD'de mevcut. dev.14: kapalıyken `inventorySearches` 0. Perf ölçümü: 31,6 ms → 0,04 ms; ilk açılış ~550 ms → 4,4 ms. | — |
| 20 | Buton yerleşimi (grid altı ayrı sıra) | 🟡 | Kod ve oyun içi dev.14 kanıtı var (75–150 %, max bakiye). | **Paketle gelen README ve `player-conveniences.md` eski/yanlış tasarımı anlatıyor (bulgu 2).** |
| 21 | Maksimum para değerleri, UI ölçekleri | ✅ | dev.14: Gold/Bones 2.147.483.647, Favor 999.999; 75/100/125/150 % native scaler; 120 kural kombinasyonu. | Ölçekler native metotla çağrıldı, ayar menüsünden değil. |
| 22 | "Confirm new blessing" etiketi | ✅ | `convenience.cpp:435-449`: klonlarda 4 localization bileşeni kapatılıyor, legacy Text + TMP. dev.9: 5 sn sonra etiketler doğru. | — |
| 23 | Kaybolan butonlar | ✅ | dev.14 (c780d01): kameradan bağımsız, yeniden açılış ve Skills paneli testleri geçti. | Controller ve her panel kombinasyonu sınanmadı. |
| 24 | Stash/Vendor toggle | ✅ | `Inventory()` açık durumu okuyup kapatıyor. dev.14 gerçek buton callback'iyle aç/kapa test edildi. | — |
| 25 | Stash All | 🟡 | `TryQuickMove` (oyunun kendi transferi), karede 4 eşya, önce yedek; ekipman inventory container'da olmadığı için yerinde kalıyor. Dolu stash'te `remaining` sayılıyor. dev.5: 17 taşındı, 11 ekipman yerinde. | dev.14'te yalnız boş envanterle sınandı. Dolu stash durumu hiç canlı test edilmedi. Oyuncuya "stash dolu" ayrıca söylenmiyor, yalnız `remaining` sayısı dönüyor. |
| 26 | Kapanışta geçici ayarlar / tam oyun kapanışı | 🟡 | App kapanışı XP x2→x1'i geri aldı, oyun çalışmaya devam etti (dev.14). İki tam kapanış 0. | dev.11'de iki 0xC0000005, dev.9'da vanilla'da da aynı offset. Çözülmüş sayılamaz; dokümanlar da bunu söylüyor. |
| 27 | Choose Mastery | ✅ | `progression.cpp:737-776`: yalnız `ChosenMastery==0` ve görev tamamsa native `openMasteryPanel`. Mastery atamıyor, ödül vermiyor. dev.3 canlı kanıtı. | progression.cpp dev.11'de değişti; tekrar canlı sınanmadı. |

## B. 12 temel özellik

| # | Özellik | Sonuç | Kanıt | Sınır |
|---|---|---|---|---|
| 1 | Skill XP multiplier | ✅ | dev.5: native gain 11 → 22 (2x); dosya o zamandan beri yalnız eklemelerle değişti. | Düşman öldürerek her ağaçta ölçülmedi. |
| 2 | Passive multiplier, kesirli, katlanmasız | ✅ | `build_rules_test` (bu denetimde de geçti): fractional, quest puanı çarpılmıyor, wrap yok. dev.5: 68 → 94 (1,5x), tekrar 94. | — |
| 3 | Skill points + gear | ✅ | dev.2 ve dev.5: L20 + 1 gear, 2x → 42. Kural testi geçti. | — |
| 4 | Node/chain bypass + mastery eşikleri | ✅ | dev.5: skill 34, passive 48, Weaver 42 normalde reddedildi, bypass ile kabul edildi. Bütçe ve node cap korunuyor. | Mastery değiştirilmiyor. Her ağaç/class sınanmadı. |
| 5 | Weaver çarpan + flat | ✅ | dev.5 + kural testi (flat çarpandan sonra). | — |
| 6 | Offhand | ✅ | dev.5: iki elli staff + staff/shield/catalyst. | Her base type sınanmadı. |
| 7 | Tek parçayla tam set | ✅ | dev.5: Invoker +4, reset ve tekrar açınca çoğalma yok. | Her set sınanmadı. |
| 8 | Zoom | ✅ | dev.2/dev.5 canlı. | — |
| 9 | Blessing max roll + 3/4/5 | 🟡 | dev.5: native generator 5 seçenek, roll byte 255. | Gerçek boss kill ve seçim UI'ı sınanmadı. |
| 10 | Summon stat / süre | 🟡 | dev.6: Totem/Ballista yaş yarı hız, Skeleton normal, Wraith/Zombie decay hook'u. | Abomination doğrulanmadı. Bütün aileler sınanmadı. Summon stat'ları ortak minion bonus editörüyle. |
| 11 | Projectile multiplier + flat | 🟡 | dev.6: Fireball 1→2, 2x+1=3, lobbed Fireball, Ice Bolt, Hammer Throw, Marrow Shards. Beam hariç. | Bütün dönüşüm/channel varyantları sınanmadı. |
| 12 | Geniş custom crafting | 🟡 | dev.6: 64 satır MultiAffix + 6 T7, serileştirme round-trip, restart sonrası aynı, gerçek stat 1,5→2,5. `workshop.cpp` o zamandan beri değişmedi. | `workshop-recipes.txt` ve mod gerekli. Mod olmadan Steam Deck/Cloud doğrulanmadı. Stat satırı dışındaki unique mekanikleri taşınmıyor. |

## C. Diğer oyuncu talepleri (1–23)

| # | Talep | Sonuç | Not |
|---|---|---|---|
| 1 | Monolith density | ✅ | `density` 1–5, Monolith sayfasında. `density_test` geçti. Canlı kanıt 10-09. |
| 2 | Bütün statlar tek tek | 🟡 | 10-09'da 247 anahtarın hepsi native getter ile apply/read/reset edildi (4.449 kontrol). `stat_editor.cpp` dev.6'da değişti, sonra tekrarlanmadı. Koşullu combat etkileri oynanmadı. |
| 3 | Monolith unlock/seçim/Corruption/Stability/gain/Echo | 🟡 | 10-09: 20 kombinasyon, Echo tamamlama/dönüş/ödül, x3 gain. dev.11 bağlantı değişikliğinden sonra tekrarlanmadı. |
| 4 | Reveal zone map | ✅ | `map_view.cpp` 10-09'dan beri aynı; canlı kanıt `ce-feature-additions.md`. |
| 5 | Crafting ayrı menü | ✅ | Ayrı sayfa (`3ea17b9`). |
| 6 | Forge Settings / Materials / Custom Items | ✅ | app.js'de üç akış var. `crafting-ui-validation`. |
| 7 | Stock all materials | ✅ | `crafting.cpp:695-750`: `max(0,target-before)` ile azaltmıyor, yedek alıyor, `SaveCurrent` çağırıyor, 1–1.000.000 sınırı var. dev.4 kanıtı. |
| 8 | Pickup radius + performans | 🟡 | 0–60, dev.5 canlı. Büyük yarıçapın performans ölçümü yok (dokümanda da yok). |
| 9 | Chest/shrine/portal, portal döngüsü | 🟡 | Portal tek kullanımlık (`portals.value=0`), 10 sn grace. Tam portal hedef geçişi dev.9'da tekrarlanmadı. Primal Hunt mağarası uçtan uca sınanmadı. |
| 10 | Grafik efektleri + geri alma | 🟡 | Geri alma dev.5'te canlı. Motion blur sahnede yoktu. **Saniyelik sahne araması riski (bulgu 3), FPS ölçülmedi.** |
| 11 | Camera angle | ✅ | dev.5: pitch 65 ve reset. |
| 12 | Town dışı Stash/Vendor + Stash All | 🟡 | Toggle ✅. Stash All için madde A-25'e bak. |
| 13 | Free respec | ✅ | dev.9: üç ağaç sıfırlandı, gold/material/level/XP değişmedi, yedek alındı. `build_controls.cpp` o zamandan beri aynı. |
| 14 | Stash arama ifadeleri | 🟡 | Kopyalama çalışıyor; oyunun stash kutusuna yapıştırma doğrulanmadı. |
| 15 | Summon canlı statları | ✅ | dev.5 Ballista, dev.9 Skeleton/Golem tabloları. |
| 16 | Gerçek summon DPS | 🟡 | Ölçülen savaş DPS'i, teorik hesaplayıcı değil (UI ve dokümanda doğru söyleniyor). Ballista fixture'ı başarısızdı; yerine skeleton/golem kullanıldı. |
| 17 | Stat/sütun gizleme | ✅ | dev.9 tarayıcı testi, `preferences.js`. |
| 18 | Menü favori/gizleme | ✅ | dev.9 tarayıcı testi. |
| 19 | MelonLoader ile birlikte | 🟡 | 0.7.3 ile kurulum ve güncelleme dev.5–6'da sınandı. **Uygulamada kaldırma butonu yok.** Bridge DLL dev.13'ten kopya; dev.14 çekirdeğiyle sınanmadı. Geçmişte MelonLoader'lı oturumda bir yükleme çökmesi kaydı var. |
| 20 | Wanted T7: arama, sıralama, çift tık | ✅ | dev.13 EXE testi; `affix-picker.js` değişmedi. |
| 21 | Normal forge T6/T7 | ✅ | dev.13: final player core, T5→T7, maliyetler, Exalted, restart sonrası kalıcı. `crafting.cpp` değişmedi. Diğer glyph'ler native sınırda kalıyor (bilinçli). |
| 22 | Steam tanıtımı | 🟡 | Taslak var (`docs/steam-community-draft.md`, Discord linkiyle). **Yayımlanmadı.** |
| 23 | Rusça | — | Kapsam dışı. |

## D. CheatEngine adayları — mevcut karşılık

| Alan | Var | Yok / bilinçli dışarıda |
|---|---|---|
| Player/Gear | Set (tek parça) ve offhand bypass, skill/passive/Weaver puan çarpanları, passive chain + mastery eşiği bypass | Item Dupe; genel item requirement bypass (yalnız forge level bypass var); Primordial limit |
| Combat | XP ×, cooldown ×, hareket hızı, full-key stat editörü (hasar, cast speed, AoE, mana regen anahtarları), projectile × / flat | Zero/infinite mana, auto target, infinite buff duration, ayrı AoE çarpanı |
| Crafting | FP katsayısı, forge level bypass, shard/rune/glyph koruma, Despair, Hope, T6/T7, material stock, custom gear | — |
| Loot | Pickup radius, material kategorileri, drops ×, gold ×, rarity ×, T7/LP filtresi | Garanti perfect roll, garanti T7 |
| Monolith | Stability (mutlak + gain ×), density, corruption, timeline unlock, Echo arama/odak | Echo spawn kontrolü, monster rarity (Monolith'e özel), objective reveal |
| Factions | CoF rank/Favor/Reputation, prophecy charge/ödül/lens, double-item | Merchant's Guild yönetimi, vendor maliyetleri, prophecy kullanım koşulları |
| QoL | Map reveal, waypoint unlock (gerçek waypoint'ler), zoom, camera angle | Random encounter, FOV, Eternity Cache ve Woven Enchanter sınırları |

Not: `ce-feature-additions.md` bu tabloyla çelişiyor (eski).

## E. Dağıtım ve güvenlik

| # | Talep | Sonuç | Not |
|---|---|---|---|
| 1 | Python gerektirmeyen EXE | ✅ | Python 3.13.7 gömülü; dev.14'te PATH'te Python yokken bağlandı. |
| 2 | Manuel ZIP / "Manual Installation" | ✅ | ZIP kökünde `Manual Installation/`, README "choose one method". CopyToGame yok. |
| 3 | Pakete girmemesi gerekenler | ✅ | 963 girdi tarandı: testhost, SDK, research, pdb, log, save, oyun binary'si, .cmd yok. Loader/core/bridge mevcut. |
| 4 | DLL eşliği / manifest | ✅ | ZIP içinden doğrulandı: Core `807c096b…` ve version `d7ca8d9c…` EXE içi ile Manual Installation'da aynı. `build-info.json` hash'leri tutuyor. |
| 5 | Kurulum senaryoları | 🟡 | 21 derlenmiş fixture kontrolü geçti (Unicode, read-only, foreign loader, unsupported build). Gerçek ikinci Steam kütüphanesi, UAC ve uygulamadan kaldırma yok. |
| 6 | GitHub kaynak + paket, public, prerelease yok | 🟡 | Repo public; dev.14 Latest, prerelease değil, asset hash'i doğru. **`main` Round 0'da (bulgu 1).** İki eski Pre-release duruyor. |
| 7 | "Manual Review" adları | ✅ | Paket adlarında yok. (`docs/security/README-REVIEW-MANUAL.md` pakette değil.) |
| 8 | AV / VirusTotal | ❔ | dev.14 binary'leri için yeni tarama yok (bulgu 5). Doğru yaklaşım: hash değiştirip "temiz" ilan edilmemiş. |
| 9 | Nexus karantina talebi | 🟡 | alpha.1 için gönderildi; alpha.5 taslağı hazır. "False positive" iddiası yok, doğru. dev.14 için talep yok. |
| 10 | Nexus sayfası İngilizce, içerik | 🟡 | Açıklama İngilizce ve kapsamlı, ama alpha.1/karantina durumunu anlatıyor; sürümü güncel değil. Kategori "Miscellaneous" (uygunluğu kontrol edilmeli). |
| 11 | Main/Primary, AI etiketi, hash | 🟡 | alpha.1 kaydında Main/Primary ve AI etiketleri işaretli. İndirme karantinada olduğu için indirilen dosya hash'i kontrol edilemedi. dev.14 için yok. |
| 12 | Discord/GitHub metinleri, posterler | 🟡 | Promo v2 ve dev.6 patch posteri temiz (Python/VirusTotal yok, görsel olarak kontrol edildi). dev.14 posteri yok; eski v1 dosyası repoda. |
| 13 | Destek linki | 🟡 | Paket README'si, release, Nexus ve Steam taslağında var; repo README'si ve uygulama içinde yok. |

## F. Denetimde yeniden çalıştırılan testler

Ayrı worktree, `native\build.bat player` ile temiz derleme.

| Paket | Sonuç |
|---|---|
| Native | **17/17 program exit 0**. Ayrıntı: hook 50/50, hook_failure 14/14, items 26/26, mainthread 24/24, lifecycle 8/8, review 73/73, loot/crafting 6260/6260, cof 48/48, monolith 22/22, density 37/37, game_read 12/12, xp 24/24, stat_key 40/40; build_rules, identity, ipc_protocol tamam; convenience 120 kombinasyon PASS. |
| JavaScript | **126/126**. |
| Python | **236/237, 1 error**: `test_desktop_startup…internet_marked_python_runtime…`. |

**Python hatasının nedeni:**
- Türkçe Windows'ta .NET hata mesajı yerelleştirilmiş geliyor.
- `subprocess.run(text=True)` bu çıktıyı cp1254 ile çözemiyor; reader thread'i `UnicodeDecodeError` veriyor ve `stderr=None` dönüyor.
- Aynı test `errors='replace'` ile geçti. Ürün davranışı doğru: config yokken engelleniyor, config varken yükleniyor.
- Bu bir test-harness kusuru. Düzeltme: testte `encoding='oem', errors='replace'`.
- Ana klasörde de tekrarlanıyor. "237 Python passed" iddiası bu makinede şu an tekrarlanamıyor.

**Derleme tekrarlanabilir değil:** aynı commit'ten yeni derlenen Core (`08e74732…`) ve version.dll (`2bf05f5a…`) paketteki hash'lerden farklı (MSVC zaman damgaları). Nexus/AV incelemesinde "kaynaktan derleyip hash karşılaştırma" yapılamaz; bunu talep metninde söylemek gerekir.

Bu testler kural, fixture ve backend testleridir. Gameplay kombinasyonlarının doğrulandığı anlamına gelmez.

## Önerilen sıra (ChatGPT için)

1. `docs/player-folder-readme.md` ve `docs/player-conveniences.md` metinlerini grid-altı sıraya göre düzelt; package.3'te README'yi yenile.
2. `Graphics()` taramasını saniyelikten olay tabanlıya ya da sahne değişimine bağla. Önce qolread'e bir sayaç ekleyip ölç.
3. Chest/shrine/portal, campaign ve summons hook'larını envanterdeki gibi başlangıçta kur; ya da campaign display/analytics hook'larını kalıcı ve pasif bırak.
4. `fix/review-20261009`'u `main`'e merge et ya da varsayılan dalı değiştir. Eski pre-release'leri kaldırmayı veya işaretini değiştirmeyi kullanıcıyla karar ver.
5. dev.14 `version.dll`, `EpochPact.Core.dll`, EXE ve ZIP'i VirusTotal'a gönder. Ardından Nexus sayfasını ve dosyalarını güncelle.
6. Game setup'a "Uninstall mod" butonu ekle (`cmd_uninstall` hazır; Melon köprüsü dahil).
7. Repo README'sine ve uygulama içine Discord linki ekle. Katalogdaki 0,75 sn ifadesini 250 ms yap. `ce-feature-additions.md`'yi güncelle.
8. Canlı tekrar gereken eski kanıtlar: Monolith Apply (dev.11 sonrası), Stash All dolu envanter/dolu stash, campaign süresi (hook süresi dahil), mastery.
