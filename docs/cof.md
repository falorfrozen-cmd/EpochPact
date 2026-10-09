# Circle of Fortune yönetimi

Offline karakter için native backend ve sahibinin UI'sine bağlanacak
`tools/cof_backend.py` API'si. Kalıcı karakter profilleri ve yeni UI bu modülün
kapsamı dışında; normal faction kayıtları oyunun kendi kaydetme akışını kullanır.

## Komutlar

Kalıcı işlemlerde `cofread` çıktısındaki **yüklü** karakter kimliği gerekir.

| Komut | İşlem |
|---|---|
| `cofread` | Üyelik, rank, Favor, rank içi Reputation, gerçek bonus alanları, dört prophecy yuvası, 12 lens ve 123 ödül asset'i. |
| `cofjoin <id> [switch]` | Normal faction katılımı. MG üyesiyse yalnız açık `switch` seçeneğiyle ayrılıp CoF'ye geçer. |
| `cofrank <id> <1–12>` | Rank ve normal bonus/yuva açılımlarını günceller; rank değişince rank içi Reputation sıfırlanır. |
| `coffavor <id> <0–999999>` | Favor bakiyesini ayarlar. Prophecy şarjına veya Reputation'a kazanç eklemez. |
| `cofreputation <id> <0–1000000>` | Yazılan miktarda Reputation ekler; kazanç çarpanından etkilenmez ve normal rank yükselmesini çalıştırır. Mutlak bakiye ayarı değildir. |
| `coflenses <id>` | Mevcut rank'ın izin verdiği, henüz alınmamış lens'leri normal `TryPurchaseLens` ile açar. |
| `cofpreview <id> <0–3> <reward\|none> <lens\|none>` | Seçimi doğrular; şarj/progress kaybını kopya üzerinde gösterir, canlı seçimi değiştirmez. |
| `cofprophecy <id> <0–3> <reward\|none> <lens\|none>` | Ödül/lens seçimini normal yapılandırma akışıyla uygular. |
| `cofcharges <id> <0–3> <0–99>` | Seçilmiş prophecy'nin tamamlanmış şarj sayısını ayarlar. |
| `coffavormult <1–100>` | Pozitif CoF Favor kazanımını çarpar; x1 hook'u kaldırır. Oturum ayarıdır. |
| `cofrepmult <1–100>` | Pozitif CoF Reputation kazanımını çarpar; Favor kazanımı ve harcamasından gelen Reputation'a uygulanır. x1 hook'u kaldırır. Oturum ayarıdır. |
| `cofchargemult <1–100>` | Prophecy şarj kazanımını Favor/Reputation bakiyesinden bağımsız artırır. |
| `cofrewardmult <1–25>` | Tamamlanan prophecy'nin normal eşya üretimindeki adedi artırır; ilave şarj harcamaz. |
| `cofdouble enemy\|echo <0–100\|reset>` | Düşman veya normal Monolith echo eşya ödülünün çift gelme ihtimalini yüzde olarak ayarlar. `reset` normal rank bonusunu kullanır. |
| `cofexaltedmult <1–100>` | Normal loot Exalted affix roll katsayısını ve mevcut Rare→Exalted şansını artırır. |
| `coft7mult <1–100>` | Normal loot T7 roll katsayısını artırır; seviye/tier uygunluğu oyunda kalır. |
| `coflpmult <1–100>` | Oyunun CoF LP bonusuna uygun Unique'lerindeki LP roll katsayısını artırır. |
| `coflensmult celerity\|charity\|duplication <1–100>` | Lens'lerin şarj katkısını veya ek ödül olasılığını ayrı artırır. |
| `factionread` | Diğer faction'lar ve Weaver'ın rank/echo puanları dahil salt okunur katalog. |

Örneğin UI `cof_backend.rank(save_id, 12)`, `unlock_lenses(save_id)` ve
`favor(save_id, 100000)` çağrılarını ayrı işlemler olarak kullanabilir.
`prophecy(save_id, slot, reward_id, lens_id, preview=True)` değişiklikten önce
şarj kaybını gösterir. `None` seçimi kaldırır. Seçenek ID'lerini okunan katalogdan
almak gerekir; sıralama index'i reward ID değildir.

Kazanç ayarlarını UI'de iki ayrı kontrol olarak bağla:

```python
from tools import cof_backend

cof_backend.favor_multiplier(5)
cof_backend.reputation_multiplier(3)
```

İkisi de 1–100 arasında ondalık değer kabul eder. Eski
`cof_backend.multiplier(value)` Favor API'sinin uyumluluk alias'ıdır.
`read()` içindeki `favorMultiplier` ve `reputationMultiplier` güncel ayarlardır;
`reputationAtMaxRank` UI'nin rank sınırını göstermesini sağlar. Yeniden başlatmada
iki çarpan da x1 olur; UI/profil katmanı istenen ayarı yeniden uygulayabilir.

## Oyun kuralları ve kayıt

Yuva açılımları rank 1/3/6/9; aynı ödül veya lens iki yuvada kullanılamaz.
`GetAvailableRewardsForRank` yüksek rank'ta alt ödülleri gelişmiş varyantlarıyla
değiştirir. Bu nedenle yalnız `rankRequired <= rank` kontrolü yeterli değildir.
UI `available` alanını izlemeli; backend son yapılandırmayı `IsConfigValid` ile
doğrular. Tüm asset'ler seçim yapılabilir seçenekler değildir.

Şarj ve seçimin davranışı oyun setter'larından gelir. Ödül değiştirmek şarjları
sıfırlar. Lens değiştirme sonucu eski lens ve şarj durumuna bağlıdır; önizleme
gerçek kopya/setter sonucunu gösterir. Şarjı azaltmak `ResetCharges` ile kesirli
ilerlemeyi de sıfırlar. Şarj sayısını yükseltmek eşya ödülünü hemen üretmez;
normal prophecy hedefinin gerçekleşmesi gerekir.

Rank azaltılırken yüksek rank bonusları ve yuvalar kapanır; ilgili seçim ve
şarjlar kaybolabilir. Kurulu oyunda `SetRank` azalma yönünde de artış aralığını
çalıştırdığından, backend normal `ToggleRanks(previous, target, false, false)`
ile azalma yönünü ayrıca uygular. Bonus backing field'lerine elle yazılmaz.

Favor kazanç çarpanı normal `Faction.GainFavor` çağrısının pozitif girişini
değiştirir. Oyunun kendi rank çarpanı, Reputation, lens ve prophecy şarj akışı
devam eder. `ignoreMultiplier` isteyen oyun çağrıları aynen geçer. Favor bakiyesi
999999'a ulaşsa da normal kazanç prophecy ilerlemesini sürdürebilir. Çarpan sadece
yüklü offline karakterin üye olduğu CoF instance'ına uygulanır.

Reputation çarpanı normal `Faction.GainReputation` pozitif girişini değiştirir.
Oyunun Favor kazanımından ve Favor harcamasından ürettiği Reputation aynı yoldan
geçer. Favor x5 ve Reputation x3, Favor kazanımından gelen Reputation'ı aynı rank
içinde x15 yapar; oyun yuvarlamaları küçük miktarlarda fark yaratabilir. Favor
harcamasında yalnız Reputation çarpanı uygulanır; harcanan Favor miktarı değişmez.
Reputation çarpanı Favor bakiyesini veya prophecy şarjını ayrıca artırmaz.

Rank yükselmesi ve bonusları oyunun normal metodu uygular. Maksimum rank'ta
Reputation ilerlemesi oyunun kuralıyla sıfır kalır; çarpan üst rank üretmez.
Yalnız yüklü offline CoF üyesinin instance'ı ölçeklenir; MG/Weaver çağrıları aynen
geçer. Elle Reputation verme ve mutlak Favor bakiyesi ayarı bu çarpanı uygulamaz.
Taşmayı önlemek için artırılmış Reputation girişi mevcut Int32 ilerlemesinin kalan
alanıyla sınırlanır; Favor cüzdan sınırı Reputation için kullanılmaz.

Her kalıcı işlemden önce `FactionTracker.SaveFaction` ve mevcut progression
snapshot sistemi kullanılır: disk kayıtları, canlı karakter, kasa ve global veri.
Başarılı işlem `SaveAndSync(false)` ve normal karakter kaydetme akışını çalıştırır.
Hata cevabı snapshot yolunu taşır. `cof_backend.undo(Path(backup))`, karakter
kimliğini doğrulayıp oyunu normal kapatarak snapshot'ı geri yükler.

Rank/Reputation/lens kasa paylaşımını, üyelik ve prophecy karakter kaydını izler.
Mevsim ve solo kasa ayrımı oyunun kendi faction tracker'ına bırakılır. MG geçişi
backend'de açık seçenek gerektirir; offline Bazaar bağlantısı eklenmedi.

## Araştırma ve doğrulama

`native/tests/cof_test.cpp`: Favor/şarj sınırları ve çarpan aritmetiği.
`tools/test_cof_backend.py`: UI IPC sınırı, açık faction geçişi ve hata/snapshot
aktarımı. Canlı test yalnız ayrı `EpCoFTest` karakterinde yürütülür; hazırlama ve
54 özgün dosyanın hash doğrulamalı geri yüklemesi `tools/cof_live_check.py` ile.

7 Ekim 2026 Reputation çarpanı turu: 28 canlı kontrol geçti. Rank 1'de 20 giriş,
normalde 21 Favor / 42 Reputation; Favor x5 ile 105 / 210; Reputation x3 ile
21 / 126; ikisi birlikte 105 / 630 verdi. Favor'dan türeyen Reputation çarpanları
birleşir. 10 Favor harcaması normalde 20 Reputation, Reputation x3 ile 60 verdi;
her iki durumda da Favor cüzdanından 10 çıktı. Reputation tek başına prophecy
ilerlemesini değiştirmedi. x2.5 yuvarlama, normal rank 2 yükselişi ve bonusu,
rank 12 sınırı, manuel grant bypass, MG kapsam dışı bırakma ve x1 hook kaldırma
doğrulandı; hook fault sayısı 0. Native CoF 25/25, Python API/kurtarma 22/22 ve
IPC geçici dosya kilidi için üç ek test geçti. Bu tur sırasında IPC okumasındaki
geçici Windows dosya kilidi komutu yeniden göndermeden, mevcut süre sınırı içinde
yeniden okunacak şekilde düzeltildi. Yeni test manifest'i
`research/live/cof-repmult-test-manifest.json`; kanıt
`research/live/cof-repmult-20261007/checks.json`. Falor'u normal kapattıktan alınan
54 güncel kayıt dosyası byte düzeyinde geri yüklendi; üç test dosyası arşivlendi.

Araştırma build'indeki `coftestgain <id> <1–10000>`, yalnız adı `EpCoFTest` olan
offline karakterde gerçek `GainFavor` çağrısını çalıştırır; `ignoreRepGain=true`.
Bu çağrı hook ve şarj akışını sınar. Doğal savaş/drop oranı ölçümü değildir.
Detaylı canlı kanıtlar Git dışındaki `research/live/cof-*` dosyalarındadır.

Yeni araştırma probe'ları `coftestgainrep <id> <1–10000>` (Reputation dahil
Favor kazanımı), `coftestrep` (normal Reputation), `coftestotherrep` (MG kapsam
kontrolü) ve `coftestspend` (normal Favor harcaması) aynı `EpCoFTest` isim ve
yüklü offline kimlik kontrolünü kullanır. Oyuncunun UI'sine eklenmez.

5 Ekim 2026: 59 canlı kontrol geçti. Rank 12/1 bonusları ve yuva kilitleri,
12 lens, dört ödül/lens seçimi, 0–99 şarj, salt okunur önizleme, geçersiz seçimler,
rank içi Reputation yükselmesi ve x5 gerçek `GainFavor` akışı doğrulandı.
Rank 1'de aynı 20 giriş x1'de 21, x5'te 105 Favor verdi; şarj ilerlemesi yaklaşık
beş kat arttı ve dolu Favor bakiyesinde de ilerledi. Normal kapatıp açma,
rank 12 / Favor 123456 / 12 lens / dört seçim / 10 ve 7 şarjı korudu.

Bu testte MG'den CoF'ye canlı geçiş veya doğal savaşta prophecy ödül üretimi
sınanmadı; MG geçişinin açık UI/IPC seçeneği gerektirmesi unit test ile doğrulandı.
Katılım öncesi snapshot'a geri dönüp yeniden açma da üyelik/rank/Favor/Reputation,
lens ve yuvaları ilk duruma getirdi. Test sonunda 54 özgün kayıt byte seviyesinde
doğrulanarak geri yüklendi; üç test dosyası arşivlendi. Falor yüklenmedi/ilerletilmedi.

Güncel faction mekanikleri: `research/factions-1.5.md`.
Weaver araştırması: `research/weaver-1.5.md`.

## CoF loot ve lens kontrolleri

UI API'leri `charge_multiplier`, `reward_multiplier`,
`double_drop_chance(source, percent)`, `exalted_multiplier`, `t7_multiplier`,
`lp_multiplier` ve `lens_multiplier(lens, value)`. `source` enemy/echo, `lens`
celerity/charity/duplication olabilir. Yüzde ayarında `None` normal rank ihtimaline
döner. Diğerlerinde x1 normal davranıştır. Bunlar oturum ayarlarıdır.
`read()['tuning']` ayarları, değişmeden duran kaynak katsayılarını, hook durumunu
ve gerçek çağrılardan son kullanılan değerleri/sayaçları taşır.

```python
cof_backend.charge_multiplier(5)
cof_backend.reward_multiplier(3)
cof_backend.double_drop_chance("enemy", 80)
cof_backend.double_drop_chance("echo", 70)
cof_backend.exalted_multiplier(3)
cof_backend.t7_multiplier(2)
cof_backend.lp_multiplier(2)
cof_backend.lens_multiplier("celerity", 3)
cof_backend.lens_multiplier("charity", 5)
cof_backend.lens_multiplier("duplication", 2)
```

Şarj `ProphecySlot.AddFavor` girişinde ölçeklenir; Tyranny'nin normal Favor
harcaması önce `CircleOfFortune.AddFavorToSlot` içinde hesaplanır. Şarj çarpanı
harcanan Favor'ı artırmaz. Celerity x3, +%50 katkıyı +%150 yapar (toplam şarj
katkısı x2.5); Charity x5, diğer yuvadaki +%10 katkıyı +%50 yapar. Bunlar bağımsız
şarj çarpanıyla birlikte çalışır. Native float→Int32 dönüşümü/ilerleme toplamı
taşmayacak şekilde artırılmış giriş sınırlanır; normal 99 şarj sınırı korunur.

Ödül çarpanı yalnız seçili `ProphecySlotReward.SpawnRewardForPlayer` normal
çağrısında eşya adedini artırır, çağrıyı/şarj tüketimini tekrar etmez. Kesirli adet
en yakın tam sayıya yuvarlanır; çağrı başına ölçeklenmiş temel adet 250 ile
sınırlanır. Rank 12 çift ödülü ve Duplication oyunun normal akışıyla birleşir.
Duplication'ın %40 temel olasılığı x3'te %100'e sınırlandırılır. Eşya oluşturma,
faction gereksinimi, affix/LP roll ve yere bırakma oyunda kalır; envantere elle
eşya yazılmaz.

Düşman çift drop'u ortak `ItemDrop.DropItem` dispatcher'ında, yalnız enemy-death
çağrısında uygulanır. Global `drops` kontrolüyle aynı hook paylaşılır; biri
kapatılınca diğerinin kullandığı hook kaldırılmaz. Echo ayarı, normal Monolith
eşya ödülündeki `SpawnEchoSpecificRewards` şansını değiştirir; Tomb kaynaklarına
uygulanmaz. Oyunun Gold/XP echo ödülü istisnaları normal kodda kalır.

**Native alanların birimleri:** GenerateItems Exalted alanı doğrudan katsayıdır
(rank 6 bonusuyla 1.5), fazladan +0.5 backing değeri değildir. T7 alanı da çıplak
olasılık değildir (rank 9 bonusuyla 2). Örneğin Exalted x3: 1.5→4.5, T7 x5:
2→10. Katsayılar normal roll formülüne girer; yüzdeler olarak gösterilmemeli.
Rare→Exalted ve Duplication gibi gerçek olasılıklar ise 0–1'de sınırlandırılır.
`GenerateAffixes` yalnız yüklü CoF üyesinin Normal/Nemesis/RageOfMorditas loot
context'lerinde ölçeklenir; shop/gambling context'leri değişmez.

LP ayarı normal `ItemData.GetUniqueLPRollMultiplierFromCoF` sonucunu, oyun
uygunluğu kontrolünden sonra ölçekler. CoF LP bonusu etkin ve Unique uygunken
normal x2, ayar x3 ile x6 olur. Weaver's Will/ineligible Unique istisnaları ve
LP 0–4 sonuç aralığı oyun tarafından uygulanır. Bu kontrol garantili LP adedi
veya doğrudan yüzde değildir.

Lens, reward asset, faction ve generator alanları yalnız ilgili eşzamanlı normal
metot çağrısı boyunca ödünç alınır; GC kökleri tutulur, dönüşte veya exception'da
geri yüklenir. Nested çağrılar aynı çarpanı tekrar uygulamaz. Rank değişiklikleri
normal kaynak değerlerini günceller; eski bonuslar ayrı kalıcı cache'e alınmaz.
CoF instance/actor ve offline oturum kontrolü her uygulamada yeniden yapılır.

Araştırma build'indeki `coftestcharge`, `coftestreward`, `coftestloot` ve
`coftestground` yalnız `EpCoFTest` üzerinde normal oyun metotlarını sınar.
`coftestloot level` yalnız bu kopyada normal SetLevel'i kullanır. Oyuncu UI'sine
bu araştırma probe'ları eklenmez.

### Loot/lens canlı doğrulaması — 2026-10-08

Altı grup için 42 ayrı son kontrol geçti; native CoF kuralları 48/48, Python
UI/IPC/kurtarma testleri 27/27. Şarj x5 ilerlemeyi x5 artırdı, Favor/Reputation
bakiyesi değişmedi. Celerity, Charity ve bağımsız şarj ayarı birlikte yalnız bir
kez uygulandı. Gerçek prophecy drop'ları: normal 20, ödül x3 ile 60, Duplication
garantili olduğunda 40, ikisi birlikte 120; her denemede bir şarj tüketildi.

Normal echo eşya ödülü %0 çift şansta 1, %100'de 2 eşya üretti. Tam adet
karşılaştırması için araştırma probe'u kataloğun garantili normal eşya roll'unu
ve sıfır ek adet katsayısını kullanır; her simüle echo öncesinde normal
`ResetPlayerRewards` çalıştırılır. Üretim hook'u bu sıfırlamayı çağırmaz; oyunun
aynı echo ödülünü yeniden toplama engeli korunur. İlk test düzeneği hataları
kanıt günlüğünde tutuldu, düzeltme sonrası son kontrollerin tamamı geçti.

LP uygunluk yolunda normal 2, ayar x3 ile 6 döndü; native rank bonusu olmayan
durum 1'de kaldı. Seviye 100 test kopyasının normal loot'unda gerçek T7 affix
oluştu. Exalted ve T7 ayrı açılarak birbirine uygulanmadıkları doğrulandı.
Global drops/CoF enemy kontrolü iki kapatma sırasıyla sınandı. Hatalı girişler
ayarları değiştirmedi; x1/reset sonrasında bütün yeni hook'lar kaldırıldı,
kaynak alanlar değişmeden kaldı ve tuning fault sayısı 0 oldu.

Güncel Falor yedeğinin 54 dosyası byte/hash doğrulamasıyla geri yüklendi,
3 test dosyası arşivlendi. Falor tekrar açıldı: id 0, seviye 8, XP 1016, Z32;
CoF rank 12/Favor 100168, lens/yuva/bonuslar ve savedItems korunmuştu.
Mevcut Favor x5 geri açıldı; Reputation x1 ve yeni kontroller x1/normal bırakıldı.
11 kurtarma/kurulum kontrolü geçti, kurulu DLL son build ile aynı SHA-256'ya sahip.
Kanıt: `research/live/cof-tuning-20261007/checks.json` (`finalChecks`),
`owner-final.json`; güncel manifest `cof-tuning-test-manifest.json` restored=true.
