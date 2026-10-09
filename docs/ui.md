# EpochPact arayüzü

Oyuncu dağıtımında `EpochPact.exe` Python kurulumu istemeden masaüstü penceresini açar.
İlk açılışta Browse ile `Last Epoch.exe` seçilir; seçim penceresi Steam klasöründen
başlar, başka disk ve klasörler seçilebilir. Yol `%LOCALAPPDATA%\EpochPact\settings.json`
dosyasında saklanır. Sol menünün üstündeki Game setup bölümünden değiştirilebilir;
Settings sayfasında da aynı panel bulunur.
Oyun kapalıyken Install mod, ardından Launch offline düğmesi kullanılır.
Kurulum mevcut backend ile yalnız doğrulanmış oyuncu DLL'sini yükler; başka bir
modun `version.dll` dosyasını değiştirmez. Kopyalama başarısızsa önceki dosyalar
geri konur. Windows izin istiyorsa Install as administrator açıkça seçilir.
Windows 10/11 x64, WebView2 ve .NET Framework 4.8 gerekir.

Kaynak koddan `EpochPact.cmd` gerçek oyun bağlantısıyla masaüstü penceresini açar.
`EpochPact-Preview.cmd` oyuna erişmeden tasarım ve formları gösterir.
İlk kurulumda Python 3 ve `py -3 -m pip install -r requirements-ui.txt` gerekir.
Mevcut Last Epoch native eklentisi ve `tools/le_session.py` içindeki oyun yolu kullanılır.
Kurulum ve oyun açılışı yalnız kendi düğmelerine basıldığında çalışır.
Açılışta ayarlar ve kayıt işlemleri kullanıcı etkileşimi olmadan uygulanmaz.

Tarayıcı kullanımı: `py -3 epochpact_ui.py --browser`.
Yalnız sunucu: `py -3 epochpact_ui.py --no-open`.
Gerçek bağlantı portu 17884, önizleme başlatıcısının portu 17885’tir.
Masaüstü penceresi ekranın çalışma alanına sığması için büyütülmüş açılır;
Windows ölçeklendirmesinde sabit 1500×1000 boyut ekranın dışına taşmaz.
İstersen pencereyi sonradan küçültebilirsin. İçerik kendi alanında kayar,
alt uygulama çubuğu pencerenin içinde kalır. Void Atlas görselinin odağı
mor kristale ayarlanmıştır.

Arayüz iki temada da tamamen İngilizcedir. `ui/locale-en.json` yalnız görünen
adları ve açıklamaları değiştirir; üretilmiş kataloğun komutları ve sınırları korunur.
`tools/ui_language.py` mevcut backend hatalarını arayüz sınırında İngilizce gösterir.

## Bölümler

- Genel: XP, altın, drop adedi, density, rarity, hız ve cooldown. Map visibility
  bölümünde kayıtlı keşfi değiştirmeyen Reveal zone map bulunur.
- Loot & Pickup: otomatik toplama, native filtre/LP/T7 affix/kategori seçimi,
  durum okuma ve toplama seçimlerini sıfırlama.
- Crafting: ayrı menüde seçili Forge uygunluğu ve açık butonla bir Forge işlemi;
  FP maliyeti, shard/rune/glyph koruması, Basic Hope/Despair ihtimali ve affix
  crafting seviye sınırı. Bölüme girince ve oyundan dönünce yalnız canlı uygunluk
  okunur; craft veya reset otomatik gönderilmez.
- Karakter: katalogdaki 206 C eşlemesi, 72 alias ve 134 SP; kategori, arama,
  sayfalama ve yerel favoriler. Bonus penceresinde slider, −/değer/+ kontrolü,
  Flat bonus / Increase % seçimi, otomatik birim, mevcut bonus ve sıfırlama.
  More % Advanced bölümünde bulunur. Teknik anahtarlar ve ham oyun cevabı
  normal oyuncu görünümünde gösterilmez; bütün statlar All stats ile erişilebilir.
- Kampanya: mevcut backend üzerinden tamamlanmamış görevlerin normal ödülleri,
  en az seviye 55 ve waypoint açma; iki ayrı işlem butonu.
- Monolith: unlock, timeline/zorluk seçimi, runtime Corruption/Stability sınırları,
  stability kazanım çarpanı. Kayıt düzenlemesi uygun hub bölgesinde açılır.
- Unique Atlas / Stash Assistant: gerçek Unique/Set kataloğu, mevcut depo ve
  envanter taraması, eksikler, LP/affix karşılaştırması ve yerel hatırlatıcılar.
  Monolith Navigator mevcut Echo ağında ödül arar; Show in game yalnız seçer.
  Ayrıntılar ve sınırlar: `docs/collection-navigator.md`.
- CoF: rank, mutlak Favor, Reputation ekleme, lens açma, prophecy önizleme/seçim,
  şarj ve katalogdaki 12 ayrı kazanç/drop/kalite/lens ayarı.
- Factions: salt okuma; hazır olmayan MG/Weaver yönetimi için işlem butonu yok.
- Ayarlar: Chronoforge / Void Atlas, taslak profiller ve yedek geçmişi.

## Oyuncu görünümü ve teknik kayıtlar

Normal başlatıcı oyuncu görünümünü açar. Stability, Corruption, crafting ve diğer
işlemlerden sonra ham JSON penceresi yerine kısa İngilizce sonuç mesajı gösterilir.
Hatalar başarı gibi gösterilmez; hata sonrası oluşturulmuş bir yedek varsa Settings
bölümündeki Recovery backups'a yönlendirilir. Geri alma ve kayıt onayları korunur.
Prophecy preview, ödül/lens adlarını ve şarj farkını okunabilir özet olarak gösterir.

Bağlantı tanılama kartları, karakter yükleme/Play Offline araştırma kontrolleri,
Reconcile actor stats, teknik stat anahtarları ve ham cevaplar yalnız açıkça
`py -3 epochpact_ui.py --developer --port 17886` ile açılan geliştirici görünümündedir.
Oyuncu araması bu gizlenen kartları yeniden göstermez. Kullanışlı Refresh live,
bonus düzenleme, map reveal ve tüm mod kontrolleri oyuncu görünümünde korunur.

Gerçek bağlantıdaki istek ve cevaplar yerel `live/ui/operations.log` dosyasına
kaydedilir. Kayıtlar 2 MiB dosya ve iki dönen yedekle sınırlıdır; HTTP oturum
tokenları yazılmaz. Önizleme bu kayıt dosyasını oluşturmaz. Tanılamaları gizlemek
native denetimleri, IPC sıralamasını veya kurtarma yedeklerini kapatmaz.

2026-10-09 oyuncu görünümü kontrolü: 94 Python ve 65 JavaScript testi geçti.
Gerçek arayüzden Stability 400→400 uygulaması kısa sonuç mesajı verdi; ham
JSON penceresi açılmadı. Timeline değerleri aynı kaldı ve yedek geçmişi 2→3 oldu.
İki tema, Settings, tanılama araması, All stats Intelligence okuması ve Crafting /
Loot yenilemeleri tarayıcıda kontrol edildi. Bu kontrol native DLL'yi değiştirmedi
ve bütün oyun mutasyonlarının yeniden test edildiği anlamına gelmez. Kanıt:
`research/live/player-ui-20261009/verification.json`.

## Uygulama kuralları

Sınırlar, varsayılanlar ve native parametreler `ui/catalog.json` dosyasından alınır.
`currentValues` ve `liveState` gerçek modda başlangıç değeri olarak kullanılmaz.
Önizleme bu tarihli okumaları açıkça işaretler ve gerçek IPC’ye erişmez.

General settings ve Overview hızlı kontrollerinde doğrudan düzenlenen genel ayarlar
**otomatik** uygulanır: XP, gold, drops, density, rarity, speed, cooldown ve map reveal.
Bu iki sayfada Apply/Discard bulunmaz. Slider hareketleri 250 ms, sayı girişleri
300 ms birleştirilir; slider bırakılınca, alan değişikliği tamamlanınca, +/−,
Default veya toggle kullanıldığında bekleme kaldırılır. Tek istek çalışır, her
kontrolün henüz gönderilmemiş son değeri tutulur; cevap gelince Applied gösterilir.
Form yeniden çizilmediği için slider ve sayı girişinin odağı korunur.

Geçersiz/boş değer gönderilmez; daha önce sırada kalan değer de iptal edilir.
Her otomatik istek düzenleme anındaki karakter ID/adıyla doğrulanır. Karakter
veya sunucu oturumu değişirse bekleyen işlemler iptal edilir. Hata/kayıp cevap
sonrası otomatik tekrar yoktur; kontrol açık hata ve isteğe bağlı Retry gösterir.
Başka bir sayfanın Apply işlemi bekleyen/başarısız otomatik isteği tekrar göndermez.

Loot, Crafting, CoF ve Monolith oturum ayarları taslağa alınır ve **Apply changes**
ile sırayla gönderilir. **Discard draft**, uygulanmış son değeri geri gösterir;
oyuna komut göndermez. Monolith density aynı native ayarı kullanır, kendi
sayfasındaki taslak davranışı korunur.
Profil yükleme yalnız taslak oluşturur. Profiller görev, bakiye veya ödül işlemi içermez.
Tema değişikliği oyun komutu göndermez.

Kayıt işlemleri kendi butonları ve sonuç ekranları üzerinden çalışır; işlem öncesinde
güncel save ID tekrar okunup ekrandaki ID ile karşılaştırılır. Yedek/hata cevapları
saklanır. Geri alma yalnız bu arayüz oturumundaki backend yedeklerini kabul eder ve
mevcut `progression_backend.undo` davranışını kullanır.

Tek HTTP worker ve mevcut `progression_backend._ipc_lock` bütün IPC isteklerini
sıralar. Timeout/hata sonrasında mutasyon otomatik tekrar gönderilmez.
Başka bir CLI veya uygulama aynı IPC’ye eşzamanlı komut göndermemelidir.

Stat editörü toplam değeri atamaz; EpochPact katkısını düzenler. Mevcut bonus
pencere açılınca uygun bağlantıda otomatik okunur; slider, değer ve bonus türü
yalnız taslağı değiştirir. Gönderim Apply bonus, sıfırlama Reset bonus ile yapılır.
Slider pratik bir aralık sunar; sayı alanı katalogdaki tüm geçerli aralığı kabul eder.
Yüzde dönüşümü yalnız fraction alias/full key veya Increased / More modu için
otomatik yapılır. Added fraction statlarında doğrudan yüzde puanı (pp) kullanılır.
Örneğin +10 pp, %5 değerini oyun sınırlarından önce %15 yapar; +25% Increased
başka yüzde artışlarına eklenir. Raw game units seçimi kullanıcıya sunulmaz.
Gelişmiş anahtar değişince eski anahtarın okuması kullanılmaz; boş veya geçersiz
alanlar gönderilmez. Reset bonus aynı full key'in bütün mod katkılarını kaldırır.
CoF çift drop
değeri zaten 0–100 yüzdedir ve tekrar 100’e bölünmez. Kaydedilmiş C row ID’leri
gönderilmez; temel veya ikincil full key kullanılır.

Hareket hızı ve aynı full key’i kullanan stat kontrolleri tek katkıyı paylaşır.
Aktör katkıları native tarafta kalıcı değildir. Harita/aktör yenilenmesinde bu UI
oturumunda uyguladığın katkıları geliştirici görünümündeki **Reconcile actor stats** ile tekrar bağlayabilirsin.
Bu işlem farklı karaktere katkı kopyalamaz; önce kayıt kimliğini ve adı doğrular.
Uygulama kapanıp açılınca eski aktör katkıları otomatik yüklenmez.

## Doğrulama

`py -3 -m unittest tools.test_collection_backend tools.test_loot_crafting_backend tools.test_ui_bridge tools.test_cof_backend tools.test_monolith_backend tools.test_progression_backend tools.test_le_session`

81 Python testi, 9 stat sunum testi ve 6 oturum/yeniden bağlantı testi geçti.
Kontrol yönlendirmesi 72 katalog girdisini kapsar; tüm stat metadata’sı, birimler, sınırlar, güncel
kimlik, sıra, sıfırlama, hata/backup, başlangıçta sıfır IPC ve mevcut backend recovery.
İngilizce katalog kapsamı, komut sözleşmesinin korunması ve backend hata çevirileri de doğrulanır.
Native çağrılar bu testlerde taklit edilir; oyun kaydına dokunulmaz.

Stat sunum testleri: `node --test tools/test_stat_model.cjs`. Katalogdaki tüm
satırların birimleri, tam anahtar eşleşmesi, slider ile native sınırların ayrımı,
bağlı statların favori kimlikleri ve gerçek native okuma formatı doğrulanır.
Yeni stat akışının tarayıcı / HTTP / adapter sonuçları, iki tema ve küçük pencere
kontrolleri: `docs/stat-ui-verification.json`.

Tarayıcı kanıtları ve kapsamı: `docs/ui-verification.json`, `docs/ui-evidence/`.
Oyun kapalıyken gerçek görev/ödül/Monolith/CoF mutasyonları çalıştırılmadı.
Yeni loot/crafting kontrollerinin ayrı native canlı testi ve Headless Edge form
kontrolü: `docs/loot-crafting-verification.json`. Native crafting önizlemesi HTTP
cevabında `forgePreview` olarak korunur; tasarım önizlemesi bayrağıyla karışmaz.
Yerleşim düzeltmesi iki temada altı farklı pencere boyutunda (720–1800 px genişlik)
ve DPR 1.5 ile doğrulandı: alt çubuk/üç işlem butonu, kaydırılan içerik ve son
navigasyon sekmesi erişilebilir. Kanıt: `docs/ui-layout-verification.json`.

2026-10-08 canlı oyun düzeltmeleri, gerçek kare aralıkları, yüzde statlarının oyun
ekranı doğrulaması ve normal yeniden açılış testi: `docs/responsiveness-fix.md` ve
`docs/responsiveness-verification.json`. Eski UI testleri tek başına kampanya veya
karakter bonuslarının oyunda çalıştığını kanıtlamaz.

2026-10-09 tamamlanan canlı stat, Monolith ve gerçek Echo testlerinin güncel raporu:
`docs/all-mods-current-verification.md`. Son oyuncu DLL'sinde 247 full key için
4.449 native kontrol; iki soğuk açılış, 20 timeline/zorluk seçimi ve 60 ek geçiş
doğrulandı. Gerçek HTTP stat okuması ortalama 112.8 ms, en fazla 129.2 ms sürdü.
Tarayıcıdaki Intelligence +100% uygulaması oyun değerini 4→8 yaptı, reset 4'e
döndürdü. Sunucu yeniden başlayınca açık sekmenin salt okuma isteği yeni oturumla
bir kez yeniden gönderilir; mutasyon hiçbir durumda otomatik tekrarlanmaz.
Bağlantı veya karakter kimliği kaybolunca önceki canlı veriler temizlenir.
Oturum testleri: `node --test tools/test_ui_session.cjs`.

2026-10-09 Monolith Apply kilidi düzeltmesi: aynı karakter bölge değiştirdiğinde
Monolith/Echo önbelleği yenilenir; Refresh live bağlantıyı ve bölüm verisini sırayla
okur. Pasif işlem butonlarının nedeni kartta gösterilir. Yazılan Corruption ve
Stability değerleri yenilemede korunur. Güncel kontroller: 81 Python, 24 JavaScript;
canlı arayüzde dört Apply butonu ve 300 değerinin korunması doğrulandı. Kapsam ve
kanıt: `docs/monolith-apply-readiness.md`. Bu kontrol sırasında kayıt değiştiren
Apply işlemi gönderilmedi.

2026-10-09 Monolith → Echo modifiers bölümüne 1–5× Monster density sliderı eklendi.
Genel density ile aynı taslak/native değeri paylaşır; ayrı Echo-only çarpanı değildir.
Apply changes mevcut density komutunu gönderir; sayfa açılışı veya slider değişimi
IPC mutasyonu yapmaz. Stability gain multiplier aynı bölümde korunur.
83 Python ve 26 JavaScript testi, ayrıca mevcut native density testi 37/37 geçti.
Gerçek arayüzden 3× uygulama native status'ta doğrulandı; 1× sıfırlama hookları
çıkardı. Bu kontrolde yeni Echo veya kayıt değiştiren işlem başlatılmadı.
Kapsam ve ekran görüntüsü: `research/live/monolith-density-ui-20261009/verification.json`.

2026-10-09 Monolith taslak kaybı düzeltmesi: alt-tab odak okumasında geçici kimlik
kaybı/timeout taslağı temizlemez; farklı karakter yalnız güncel offline kimlikle
doğrulanınca temizlenir. Değişmeyen sayfa odak okumasında yeniden çizilmez. Apply
hedefi onaydan önce sabitlenir; başarılı işlem yalnız kendi gönderilmiş taslağını
temizler. 36 JavaScript ve 83 backend testi geçti; eski kodda dört regresyon tekrar
üretildi. Kapsam: `docs/monolith-apply-readiness.md` ve
`research/live/monolith-draft-fix-20261009/verification.json`.

2026-10-09 Crafting/Loot yerleşimi: General yalnız temel ayarlar ve Map visibility;
Loot & Pickup ve Crafting bağımsız menülerdir. Katalog grupları, arama ve Overview
kısayolları aynı yönlendirmeyi kullanır. Crafting maliyet/malzeme, glyph ihtimalleri,
seviye uygunluğu ve tek Forge işlemini ayrı bölümlerde gösterir. Bölüme giriş ve
odak dönüşü yalnız ilgili bölümü okur; taslaklar korunur. 88 Python ve 43 JavaScript
testi geçti. İki temada Crafting'in son kontrolüne erişim doğrulandı; canlı UI
üzerinden rune/glyph açma/reset ve map açma/kapatma native sonucu doğrulandı.
Kanıtlar: `research/live/ce-features-20261009/verification.json` ve `ui-*.txt`.
