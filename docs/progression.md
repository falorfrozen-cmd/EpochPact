# Görev tamamlama ve waypoint açma

Çevrimdışı karakter için iki buton eklendi. `tools/EpochPact-Gorevler.cmd`
dosyasını aç veya proje kökünde `py -3 tools/progression_panel.py` çalıştır.
Bu küçük Tkinter paneli mevcut IPC kanalını kullanır; oyuna veya masaüstüne tuş basmaz.
Genel mod arayüzünün tasarımı ayrı bir sonraki çalışma olarak kalıyor.

## Butonların davranışı

- **Tüm görevleri tamamla ve ödülleri ver:** henüz bitmemiş ana ve yan kampanya
  görevlerini oyunun `Quest.completeQuest` yoluyla tamamlar. Görev XP'si, altın,
  görev pasif puanları, idol açılımları ve varsa özellik ödülleri normal oyun
  kurallarıyla verilir. Ardından karakter, kullanıcının seçimi doğrultusunda en az
  **55. seviyeye** çıkarılır. 55 veya üzerindeki karakterin seviyesi düşürülmez.
  End of Time, MonolithHub ve Council Chambers (`M_Rest`) waypointleri de açılır.
- **Tüm waypointleri aç:** çalışma anındaki dünya haritasından gerçek sahnesi ve
  waypointi olan hedefleri bulup `CharacterData.AddUnlockedWaypointScene` ile açar.
  `UIWaypoint.CheckWaypoint` simgeleri anında yeniler. Sadece geçiş/sahne simgesi
  olan, waypoint bulunmayan yerler hedef değildir. Monolith zaman çizgilerinin
  kendine ait ilerleme koşulları oyunun kontrolünde kalır.
- **Son işlemi geri al:** ilgili çevrimdışı karakterin yedeğiyle eşleşmesini
  doğrular, oyunu normal şekilde kaydedip kapatır, işlem öncesi karakteri, altın
  kasasını ve ortak kilit durumunu geri yükler. Oyunu tekrar açmak gerekir.
  Bu işlemden sonra oynanan ilerleme de bu dosyalarda geri alınır; diğer
  karakterlerin dosyaları, diğer kasalar ve kasa sekmeleri geri alınmaz.

Ustalık ve fraksiyon seçimi otomatik yapılmaz. Kampanya dışındaki Monolith/echo,
arena, repeatable ve test görevleri kampanya tamamlama kapsamına alınmaz.
Tamamlanmış görevler yeniden ödül vermez. 55'e seviye desteği ayrıca raporlanır;
bu XP, öldürme XP çarpanından geçmez ve fraksiyon favoru üretmez.
Seviye artışı doğrudan bir alan değişikliğiyle değil, normal `GainExpDirect` /
`LevelUp` olaylarıyla gerçekleşir: seviye pasifleri, yetenek kilitleri ve
karakter istatistikleri oyunun kendi akışında güncellenir.

En az 55 seviyesi kullanıcının seçtiği buton davranışıdır. EHG'nin 2020 tarihli
Monolith açıklaması End of Time'a erişimi gereksinim olarak belirtir. Mevcut
oyun sürümündeki erişim ayrıca test karakterini gerçekten End of Time ve
MonolithHub'a taşıyarak doğrulandı.
[EHG geliştirici açıklaması](https://forum.lastepoch.com/t/end-game-system-update-monolith-of-fate/21650?page=3).

## Native komutlar

```text
progressread
questscomplete <progressread içindeki çevrimdışı kayıt id'si>
waypointsunlock <progressread içindeki çevrimdışı kayıt id'si>
```

Yanıtlar JSON'dur. Kimlik eskiyse işlem reddedilir. İki ayrı offline kontrolü
(`GameplayEnvironment`, yüklü `CharacterData.IsOffline`) ve görev listesinin
yüklü aktöre ait olması doğrulanır. Bilinmeyen/bozuk görev tamamlama rotası varsa
herhangi bir görev uygulanmadan işlem durur. Normal fonksiyon çağrısı bir görevde
başarısız olursa kısmi sonuç, kalan görevler ve yedek yolu raporlanır.

Sınıf, metot, enum ve alanlar güncel runtime metadata adlarıyla çözülür.
Katalog sayıları kodda sabitlenmez. Herhangi bir per-frame harita taraması veya
kalıcı görev hook'u eklenmez; işlemler ana iş parçacığında bir kez çalışır.

## Yedek ve geri alma

Her değişiklikten önce `<oyun>/EpochPact/backups/progression/<pid>-<tick>/`
altında tam kayıt dosyası kopyası ve **canlı** serialize edilmiş karakter,
stash ve global veri alınır. `manifest.json` ancak bütün dosyalar yazıldıktan
sonra oluşur; yedek tamamlanmadan ödül, seviye veya waypoint değişmez.
Diskteki son autosave canlı oyundan geride olabileceği için geri alma canlı
snapshotları kullanır. Geri almadan önce mevcut kayıtlar ayrıca
`research/live/saves-backups/` altında saklanır.

Panel son işlem yolunu `research/live/progression-panel-history.json` içinde
tutar. Geri yükleme CLI'sı, **oyun kapalıyken**:

```text
py -3 tools/progression_backend.py restore "<native yanıtındaki backup klasörü>"
```

Manuel CLI işlemleri panel geçmişine eklenmez; backup yolunu saklamak gerekir.
Geri alma girdileri, karakter/kasa kimliği, kayıt klasörü, JSON ve dosya türleri
yazma başlamadan doğrulanır. Aktif oyuna doğrudan save dosyası yazılmaz.

## 2026-10-05 canlı doğrulama

Falor'un kopyası `EpProgressTest` üzerinde test edildi; asıl karakterin
kampanyası bu test için tamamlanmadı. Mevcut runtime kataloğunda 148 toplam
görev, **85 kampanya kaydı** (41 ana, 44 yan; bunlardan biri gizli) ve **109
gerçek waypoint sahne anahtarı** bulundu. Normal kampanya filtresi test
görevlerini ve `Minilith` bölümünü dışarıda bırakır.

| Ölçüm | Önce | Sonra |
|---|---:|---:|
| Karakter seviyesi | 8 | 55 |
| Tamamlanmış kampanya kaydı | 1 / 85 | 85 / 85 |
| Kümülatif XP | 3.842 | 1.135.707 |
| Görev XP'si | — | +8.045 |
| 55'e ek XP | — | +1.123.820 |
| İzole test kasası altını | 0 | 102.001 |
| Görev pasif puanları | 0 | 15 |
| İdol açılımı | 0 | 8 |

Tüm 109 waypoint kaydedildi ve en az bir harita simgesi etkin olarak okundu.
Oyunu kapatıp açtıktan sonra seviye, XP, altın, 85 görev ve waypointler korundu.
Butonların tekrar uygulanması ödül/XP üretmedi. Kapsam dışı 63 görevin durumu
değişmedi. `SceneList.GetCurrentSceneDetails` ile test karakterinin gerçek
`Z32 -> EoT -> MonolithHub` geçişi doğrulandı; yalnızca geçiş isteği yanıtına
dayanılmadı. MonolithHub birden fazla gate simgesi paylaşır; araştırma komutu
`travel MonolithHub:11` kullanılan aktif hedefi açıkça seçti.

Bu sürümün kampanya kayıtları doğrudan tüm özelliklere ek ödül vermiyor.
`Knowledge of Orobyss` +2 özellik ödülü kampanya dışındaki Minilith görevinde;
bu buton o görevi tamamlamaz. Görev pasif ve idol toplamları normal oyunun
15/8 sınırını izler. Karakter çok düşük seviyedeyken görev XP'si de normal
seviye uyarlamasına tabidir; bu nedenle 55 desteği ayrı yapılır.

Geri alma canlı testte 55'ten 8'e, XP 921'e, iki kayıtlı görev adımına ve iki
önceki waypoint anahtarına döndü. Yeniden açılış ayrıca kontrol edildi.
Kurtarma testleri: `py -3 -m unittest tools.test_progression_backend -v` (13).
Panelin butonları ve yenileme hatasında işlem sonucunun korunması Tkinter ile
kontrol edildi. Native build başarılı; mevcut XP 24/24, stat 33/33 ve density
37/37 testleri geçti. Kanıt dosyaları git dışındaki `research/live/` altında:
`progression-complete-55.json`, `progression-reopened-55.json`,
`progression-endgame-read.json`, `progression-undo-disk.json`,
`progression-undo-reopened.json`, `progression-final-verification.json`.

Asıl save dosyalarına dönerken test öncesi tam yedek kullanılmalı; test sırasında
ayrı solo kasa oluşmuştur. Oyun yeniden başladığı için Falor'un actor-memory
stat bonusları ve density x3 ayrıca tekrar uygulanmalıdır.
