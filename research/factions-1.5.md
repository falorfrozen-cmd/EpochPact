# Item Factions: Circle of Fortune ve Merchant's Guild

Araştırma: 5 Ekim 2026. Resmî 1.5/1.5.1 notları ve kurulu oyunun canlı IL2CPP
metadata/asset verileri birlikte incelendi. Eski 1.0–1.4 rehberleri yeni prophecy ve
Bazaar rank sistemini doğru anlatmıyor. Kanıt: `research/live/factionread-test.json`,
runtime `methods.tsv`/`fields.tsv`, `native/core/factions.cpp`.

## Ortak sistem

Bunlar oyuncuların kurduğu klanlar değil, **Item Faction** sistemleri. Karakter CoF
ve MG'den birine üye olabilir; üyelik değiştirilebilir. Favor harcanabilir para,
Reputation rank ilerlemesidir. XP/favor kazanımı ve favor harcaması ayrı reputation
yollarıdır. İlerleme aynı kasaya erişen karakterler arasında paylaşılır; üyelik ve
prophecy seçimi karaktere aittir. Sezon/Legacy ve solo kasa ayrımı korunmalı.

Faction gereksinimli ekipman, başka faction'a geçildiğinde kullanılamayabilir.
Özellikle ekipman gereksinimini zorla kaldırmak ayrı bir özellik olur; sıradan bir
rank/favor ayarıyla eşya işaretlerini değiştirmemek gerekir.
[Resmî Item Factions açıklaması](https://support.lastepoch.com/hc/en-us/articles/46361868931995-What-are-Item-Factions)

## CoF: kurulu oyundan doğrulanan 12 rank bonusu

Rank açıklamaları `FactionData.FactionRanks` asset dizisinden okundu. Aşağıdaki
tablo canlı oyun verilerinin Türkçe özetidir; bonusların tamamı bu çalışmada
tetiklenerek test edilmedi.

| Rank | Etki |
|---|---|
| 1 | Düşman drop'unda %35 çift eşya ihtimali. |
| 2 | En az CoF rank 1 gerektiren eşyada Rune of Ascendance'ın %45 korunma ihtimali. |
| 3 | Idol drop'unda %25 ihtimalle iki ek idol. |
| 4 | Set parçası düşeceğinde bütün setin düşmesi. |
| 5 | Monolith eşya ödülünün %35 çift gelme ihtimali. |
| 6 | Affix'lerin Exalted olma ihtimalinde %50 artış. |
| 7 | Exiled Mage'den çift Experimental eşya. |
| 8 | Unique'lerin LP taşıma ihtimalinin iki katına çıkması. |
| 9 | T7 affix sıklığının iki katı olması. |
| 10 | Boss'a özgü eşyalarda %150 artırılmış drop ihtimali. |
| 11 | Rare olacak eşyanın %25 ihtimalle Exalted'a dönüşmesi. |
| 12 | Prophecy'nin iki kat eşya üretmesi. |

Bu bonuslar aynı tek çarpan değildir: düşman drop'u, echo ödülü, affix üretimi,
boss loot'u ve prophecy üretimi farklı kod yollarına bağlanır.

## CoF: 1.5 prophecy akışı

Reroll kaldırıldı. Dört yuvanın açılma rank'ları 1, 3, 6 ve 9. Seçilen ödül,
favor kazanıldıkça şarj biriktirir; üst sınır 99. Hedef gerçekleşince bir veya birden
fazla şarj harcanarak ödül üretilir. Her yuvada bir ödül ve bir lens seçilir.

| Yuva | Açılma | Hedeflerden biri |
|---|---:|---|
| Mesembria | 1 | Rift Beast / Shrine |
| Dysis | 3 | Nemesis / Lost Cache |
| Eos | 6 | Exiled Mage / Timeline Boss |
| Arctis | 9 | Fateweaver / Omen |

Aynı lens veya ödül, bir karakterin birden fazla yuvasına yerleştirilemez. Lens
açılımı aynı kasayı kullanan karakterlerce paylaşılır.
[Resmî 1.5 notları](https://lastepoch.com/patchnotes/)

## CoF: canlı asset'lerden okunan lens'ler

`ProphecyData.GetUnlockRankForLens`, `GetLocalizedLensEffect` ve ilgili asset
alanları okundu. Yüzdeler bu kurulu build'e aittir.

| Lens | Rank | Etki / ölçülen parametre |
|---|---:|---|
| Tyranny | 3 | Favor tüketerek şarjı hızlandırır; tek asset katsayısı tam formül değildir. |
| Origin | 4 | Prefix tier yükseltme parametresi 0.20. |
| Finality | 4 | Suffix tier yükseltme parametresi 0.20. |
| Celerity | 5 | +%50 şarj hızı. |
| Duplication | 5 | %40 ek ödül üretme ihtimali. |
| Anomaly | 6 | Affix ağırlıklarını sıkıştırır; üs 0.55. |
| Acceleration | 6 | Rare düşmanları ek prophecy hedefi yapar. |
| Prowess | 6 | Skill level affix ağırlığı parametresi 1.5. |
| Charity | 7 | Diğer prophecy yuvalarına +%10 şarj hızı. |
| Celestial Scales | 7 | %80 ek ödül ihtimali, %2 Grole Egg ihtimali. |
| Curiosity | 8 | Unique/Set ağırlıklarını sıkıştırır; üs 0.8. |
| Quality | 9 | LP roll parametresi 1.15, rastgele affix tier yükseltme parametresi 0.20. |

Anomaly/Curiosity nadir ağırlıkları daha sık görülen sonuçlara yaklaştırır. LP roll
katsayısı, doğrudan “%15 LP şansı” olarak etiketlenmemeli; eşya üretim formülü içinde
kullanılır. Set/Unique ve Rare/Exalted ödüllerinde farklı lens etkileri uygulanır.

Tyranny için **1.5.1 düzeltmesi** ayrıca önemlidir: bankalanmış favor yokken 2x temel
şarj; varsa kazanılan favor hızının %10'u kadar tüketimle 2.1x temel şarj. Kurulu
asset'teki `tyrannyLensChargeMultiplier=1.1` değerini tek başına bu toplam çarpan
sanmamak gerekir. Patch davranışı ile kurulu build'in hesap yolu ayrıca ayrılmalı.
[Resmî 1.5.1 düzeltmesi](https://lastepoch.com/1-5-1/patchnotes/)

## Merchant's Guild: güncel ek mekanikler

Üye olan karakter, rank 1'den bütün ekipman türlerini alıp satabilir. Bazaar alışverişi
gold ve favor kullanır; satın alınan eşya tekrar satılamaz ve MG gereksinimi taşır.
Merchant's Token, alışverişin favor maliyetini kaldırır. Oyuncular arası Bazaar
alışverişi çevrimiçi hizmettir; offline modumuz bu hizmete bağlanmaz.
[Resmî MG açıklaması](https://support.lastepoch.com/hc/en-us/articles/46363313103643-Merchant-s-Guild)

1.5'te Black Market Token kaldırıldı. Yeni arayüzde geçmiş satışlardan fiyat
önerisi, fiyat tabanı ve miktarı belirlenebilen otomatik fiyat düşürme bulunur.
[Resmî 1.5 notları](https://lastepoch.com/patchnotes/)

Rank ödülleri artık malzeme/key çantaları. Canlı `FactionData` açıklamalarından:

| Rank | Yeni çanta erişimi |
|---|---|
| 2–4 | Common Shards, Common Glyphs, Common Runes |
| 5 | Dungeon Keys ve Arena Keys |
| 6–8 | Shards, Glyphs, Runes |
| 9–11 | Uncommon Shards, Uncommon Glyphs, Uncommon Runes |
| 12 | Muffins |

`FactionDataMerchantsGuild.GetBagSubtypesForRank` ve `GetRankRequirementForBagSubtype`
ayrı kod yolları. Bunlar NPC faction alışverişine bağlıdır; gerçek oyuncu Bazaar'ı
ile aynı şey değildir. Bu çalışmada offline çanta satın alma denenmedi. MG'yi bütünüyle
“offline hiçbir mekaniği yok” diye sınıflandırmak 1.5 için doğrulanmış bir sonuç değil.

## Mod için erişim haritası

Adresler yalnızca araştırma kanıtıdır; özellikler isimle runtime çözülmelidir.

| Sistem | Normal oyun API'si | İncelenen RVA |
|---|---|---|
| Üyelik | `Faction.Join(bool)`, `Leave()` | `0x20C3D50`, `0x20C3EE0` |
| Favor | `GainFavor(int,bool,bool)`, `TrySpendFavor(int,bool)` | `0x20C3740`, `0x20C4F20` |
| Reputation | `GainReputation(int)` | `0x20C3980` |
| Rank | `SetRank(int,bool)` / `ToggleRanks` | `0x20C4A80`, `0x20C4D00` |
| Kayıt/sync | `FactionTracker.SaveFaction` | `0x20C11E0` |
| Prophecy seçimi | `CircleOfFortune.AttemptApplyProphecyConfiguration` | `0x2083F70` |
| Şarj kazanımı | `CircleOfFortune.AddFavorToSlot` / `ProphecySlot.AddFavor` | `0x2083CA0`, `0x20B0930` |
| Şarj sayısı | `ProphecySlot.AddCharges` | `0x20B0860` |
| Lens açılımı | `TryPurchaseLens` / `HasPurchasedLens` | `0x2088800`, `0x2084AF0` |
| Ödül tetikleme | `ProphecySlot.TryTriggerReward` | `0x20B1910` |

`Rank`, `Favor`, `Reputation` backing field'lerine doğrudan yazmak, rank bonuslarının
ve UI olaylarının çalışmasını garanti etmez. Normal API + snapshot + yeniden açılış
doğrulaması kullanılmalı. Prophecy ödülleri normal eşya üretimiyle verilmeli; çantaya
elle eşya yazma, önceki kaybolan eşya sorununa benzer riskler doğurur.

İlk araştırma turunda yalnız `factionread` eklendi; faction değişiklikleri uygulanmadı.
Sonraki CoF uygulama turu üyelik/rank/favor/reputation, favor kazanım çarpanı,
prophecy ödül–lens seçimi/önizlemesi, lens açılımı ve şarj yönetimini ekledi;
`docs/cof.md` ve `tools/cof_backend.py` güncel arayüzdür. Rank 12 açma ve şarj doldurma
ayrı işlemlerdir. `ForgottenKnights` ve `TheWeaver` ayrı faction verileri;
CoF/MG'nin karşılıklı üyelik kuralı onlara uygulanmaz. Weaver için `weaver-1.5.md`.
