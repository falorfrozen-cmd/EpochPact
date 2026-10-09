# EpochPact kod incelemesi — 2026-10-09

Bu dosyayı düzeltmeyi yapacak ajana ver. Her madde bağımsız; önem sırasına göre dizili.

**Doğrulanan durum (bu incelemede çalıştırıldı):**
- `native\build.bat`: /W4'te sıfır uyarı, sıfır hata.
- Native testler: hook 48/48, xp 24/24, stat_key 40/40, density 37/37, monolith 22/22, cof 48/48, loot_crafting 6260/6260.
- Python: `py -3 -m unittest tools.test_ui_bridge tools.test_le_session tools.test_progression_backend tools.test_monolith_backend tools.test_cof_backend tools.test_loot_crafting_backend tools.test_collection_backend` → 94/94.
- JS: `test_stat_model.cjs` 9, `test_ui_readiness.cjs` 48, `test_ui_session.cjs` 8 → hepsi geçti.
- Oyun içi (canlı) test yapılmadı.

Her düzeltmeden sonra aynı komutlar tekrar çalıştırılmalı.

---

## 1. Kodun yarısı git'te yok (KRİTİK)

`git status`: 95 dosya hiç takip edilmiyor, 20 dosya değişmiş ama commit edilmemiş. Son commit `1ef1949` (rarity/autopickup dönemi).
`native/core` içindeki 58 dosyanın 27'si takip dışı: `cof.cpp`, `cof_tuning.cpp`, `collection.cpp`, `crafting.cpp`, `map_view.cpp`, `monolith.cpp`, `progression.cpp`, `smart_loot.cpp`, `stat_editor.cpp`, `factions.cpp`, `density.cpp`, `managed.hpp` vb. `tools/`, `ui/` ve `docs/` dosyalarının çoğu da öyle. GitHub'a (`origin`) bunların hiçbiri gitmemiş.

**Yapılacak:** `.gitignore`'u kontrol et (`build/`, `*.obj`, `research/live/`, `research/game-data/`, `live/ui/` zaten dışarıda), sonra her şeyi mantıklı commit'lere bölüp push et. Bu yapılmadan başka bir değişikliğe başlama.

## 2. Yedekler ve IPC dosyası sınırsız büyüyor (YÜKSEK)

Oyun klasöründe ölçülen değerler (`<game>\EpochPact\`):
- `backups\progression\`: **958 klasör, ~600 MB, 58 bin dosya**. [progression.cpp:393](../native/core/progression.cpp) `WriteSnapshot` her kayıt değişikliğinde (monolith, CoF, quest, craft) bütün `Saves` klasörünü kopyalıyor ve eskileri hiç silmiyor.
- `ipc\out.txt`: **52 MB**. [commands.cpp:374](../native/core/commands.cpp) her komutu `out.txt` dosyasına ekliyor. Ama Python tarafı artık `reply.json` (nonce'lu, `protocol.json` v2) okuyor, yani `out.txt` sadece yazılıyor, okunmuyor.
- `research/live/saves-backups` (repo içinde, OneDrive'da senkronize): [le_session.py](../tools/le_session.py) `backup_saves` için de saklama limiti yok.

**Yapılacak:**
- `WriteSnapshot`: yeni yedek yazıldıktan sonra en yeni N tanesi (örneğin 30) dışındakileri sil. Silerken sadece `manifest.json` içeren, `backups\progression\` altındaki klasörlere dokun. `tools/progression_backend.py` restore listesinin bundan etkilenmediğini test et.
- `out.txt`: nonce'lu komutlarda yazmayı bırak veya çekirdek açılırken dosyayı kısalt (örneğin 1 MB üstündeyse sıfırla). Eski (nonce'suz) protokol hâlâ `out.txt` okuyor, onu bozma.
- `core.log`: açılışta 5 MB üstündeyse `core.old.log` olarak döndür.
- `le_session.py backup_saves`: son 20 yedeği tut.

## 3. Cooldown çarpanı düşmanları da etkiliyor ve bazı yeteneklerde çarpan karesi oluyor (ORTA, canlı doğrulama gerek)

[player.cpp:177](../native/core/player.cpp) `d_getCooldown`, `ChargeManager.getCooldown` metodunu **global olarak** hook'luyor ve `self` değerinin oyuncuya ait olup olmadığını kontrol etmiyor. `research/findings.md:165` bu metodun bir AI fallback çağrı yeri olduğunu söylüyor. Bu durumda düşman/minion cooldown'ları da `cooldown N` ile kısalıyor olabilir.

Ayrıca `getCooldown` uzunluğu kullanan bir oyuncu yeteneği, [player.cpp:161](../native/core/player.cpp) `d_chargeTick` içinde ölçeklenmiş deltaTime ile de geri sayılıyorsa toplam hızlanma N değil N² oluyor.

**Yapılacak:** `d_getCooldown` içinde `self`'in oyuncunun `PlayerChargeManager` nesnesi olduğunu kontrol et (örneğin `getPlayerActor` → charge manager karşılaştırması, ya da sınıf kontrolü `object_get_class(self) == PlayerChargeManager`). Oyuncu değilse orijinal değeri döndür. Ardından tick ile uzunluğun birlikte uygulanıp uygulanmadığını canlıda ölç. Uygulanıyorsa ikisinden birini kaldır, README'deki "N kat hızlı" tanımına uy.

## 4. Monolith ve CoF yedeği oyunun ana thread'inde diske yazılıyor (ORTA)

Quest/waypoint ([progression.cpp:525-566](../native/core/progression.cpp)) ve craft ([crafting.cpp:365-379](../native/core/crafting.cpp)) yedekleri doğru yapıyor: `Capture` ana thread'de, `WriteSnapshot` (dosya kopyalama) komut thread'inde.
Ama [monolith.cpp:192](../native/core/monolith.cpp) `Mutation` → `Snapshot` → `progression::SnapshotCurrent` ve [cof.cpp:98](../native/core/cof.cpp) `Mutation` → `progression::SnapshotCurrent`, işin tamamını `Run(...)` içinde, yani Unity ana thread'inde yapıyor. Bütün `Saves` klasörünün kopyalanması o kare boyunca oyunu donduruyor (OneDrive/antivirüs varsa yüzlerce ms).

**Yapılacak:** Monolith ve CoF mutasyonlarını progression/crafting kalıbına çevir. Önce `RunSteps`/`Run` içinde `CaptureSnapshotCurrent`, sonra komut thread'inde `WriteCapturedSnapshot`, sonra tekrar ana thread'de kimlik kontrolüyle (`CheckId` + actor değişmedi mi) asıl değişiklik. Mevcut "yedek yazılmadan değişiklik yapılmaz" kuralı korunmalı.

## 5. Frame hook'u her komut sonrası takılıp sökülüyor (DÜŞÜK-ORTA, performans)

[mainthread.cpp:158](../native/core/mainthread.cpp) `Housekeep`, `EventSystem.Update` hook'unu 2 sn boşta kalınca söküyor; bir sonraki komutta yeniden takılıyor. [hook.cpp:222](../native/core/hook.cpp) `Freeze` her takma/sökmede süreçteki **bütün thread'leri** askıya alıyor. UI'dan ara ara gelen her komut kısa bir takılma yaratabilir. `KeepTicking` de açılışta hook'u zaten takıyor.

**Yapılacak:** Hook'u bir kez takıp kalıcı bırak. `Detour` başında atomic bir `g_pending` sayacı kontrol et, sıfırsa mutex almadan doğrudan `g_orig` çağır. `Housekeep` içindeki sökmeyi kaldır. `mainthread.hpp` yorumunu ("removed again after two idle seconds") güncelle. `frameread` telemetrisiyle önce/sonra karşılaştır.

## 6. Oyun klasörü sabit kodlanmış (DÜŞÜK)

[le_session.py:34](../tools/le_session.py) `GAME = C:\Program Files (x86)\Steam\steamapps\common\Last Epoch`. Steam kütüphanesi başka diskteyse UI ve bütün araçlar çalışmaz.

**Yapılacak:** Önce `EPOCHPACT_GAME_DIR` ortam değişkeni, yoksa Steam `libraryfolders.vdf` (`HKCU\Software\Valve\Steam\SteamPath` + `steamapps\libraryfolders.vdf`) içinde `Last Epoch` ara, en son mevcut sabit yol. `test_le_session.py`'ye bir test ekle.

## 7. Komut dosyası okuma yarışı (DÜŞÜK)

[commands.cpp:44](../native/core/commands.cpp) `ReadAndDelete` önce okuyor, sonra `DeleteFileW` yapıyor. Python o arada yeni bir `cmd.txt` yerleştirirse (sadece önceki komut zaman aşımına uğradıktan sonra mümkün) yeni komut okunmadan siliniyor. Kullanıcı tarafında sonuç "zaman aşımı" olarak görünür.

**Yapılacak:** Okumadan önce dosyayı `MoveFileExW(cmd.txt → cmd.processing)` ile al, sonra oku ve sil. Böylece yeni gelen `cmd.txt` korunur.

## 8. Yapısal / bakım (DÜŞÜK, isteğe bağlı)

- **Tekrarlanan yardımcılar:** `managed.hpp` içinde `Run`, `Json`, `Text` var. Ama `progression.cpp` (`Run`, `Json`, `Text`), `stat_editor.cpp` (`Run`, `String`), `player.cpp` (`OnMain`), `research.cpp` (`OnMain`), `density.cpp` (`OnMainThread`) kendi kopyalarını tutuyor. Hepsini `managed::` sürümlerine indir; davranış farkı varsa (timeout, hata formatı) parametre yap.
- **Komut tablosu:** [commands.cpp](../native/core/commands.cpp) `Execute`, 250+ satırlık bir `if (cmd == ...)` zinciri. Yeni komutu yanlış `#ifdef` bloğuna koymak kolay; `player` build'inde bir komutun sessizce düşmesi fark edilmeyebilir. İsim → handler tablosuna (`std::unordered_map<std::string_view, Handler>`) çevirmek, her modülün kendi komutlarını kaydetmesi önerilir. Acil değil.
- **`ui/app.js`:** 709 satır ama satırların çoğu binlerce karakterlik tek satır HTML şablonları (örnekler: 360, 364, 371, 498). Okunabilirlik ve diff için sayfa başına küçük render fonksiyonlarına böl. `esc()` tutarlı kullanılmış ve CSP sıkı, güvenlik sorunu görülmedi.
- **Build çıktısı:** `build.bat`, `player` ve research build'lerini aynı `build\EpochPact.Core.dll` dosyasına yazıyor. Yanlış sürümü kurmak kolay. `le_session.py install` hangi sürümün kurulduğunu loglasın veya çıktı dosya adları ayrılsın.

---

## İyi olan, dokunulmaması gerekenler
- Hook motoru: trampoline'ler serbest bırakılmıyor (Remove güvenli). Askıdaki thread varken bellek ayırmıyor. Thread'ler patch aralığındaysa tekrar deniyor.
- Bütün oyun çağrıları `game::Guarded` (SEH + C++) içinde. Setter'ların hepsi `Run` içinde try/catch'li, komut thread'inde yakalanmayan istisna görülmedi.
- Online kapısı (`IsOfflinePlay`) hem setter'larda hem detour'larda kontrol ediliyor.
- UI sunucusu: yalnızca 127.0.0.1, Host + Origin kontrolü, POST için oturum token'ı, CSP, tek worker ile sıralı IPC, süreçler arası mutex.
- Kayıt değişikliklerinden önce yedek alınıyor; yedek başarısızsa değişiklik yapılmıyor.
