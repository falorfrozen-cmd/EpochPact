# The Woven / The Weaver araştırması

5 Ekim 2026: güncel resmî oyun rehberi, 1.5 notları ve kurulu oyunun IL2CPP
metadata/asset verileri incelendi. Kullanıcının Sezon 2'den hatırladığı örümcek
rank sistemi **The Woven**; uygulama sınıfı `LE.Factions.TheWeaver`, faction ID 3.
Sezon 2 kapsamı [resmî Tombs of the Erased sayfasında](https://lastepoch.com/tombs-of-erased/).

## Nasıl ilerliyor?

Cemetery of the Erased tamamlandıktan sonra Haven of Silk'teki Masque ile
katılım sağlanır. Memory Amber kazanmak ve harcamak rank ilerlemesine katkı verir.
Tombs/Cemeteries, Silken Cocoons ve Weaver düşmanları Amber kaynaklarıdır.
Rank, Weaver Tree puanları ve Masque'nin Woven Echo seçeneklerini açar.
[Resmî The Woven açıklaması](https://support.lastepoch.com/hc/en-us/articles/46363350715547-The-Woven)

The Woven bir **endgame faction**; CoF/MG üyeliğiyle birlikte çalışır. Endgame
faction'lar birbirlerini de dışlamaz.
[Resmî Endgame Factions açıklaması](https://support.lastepoch.com/hc/en-us/articles/46361863355931-What-are-Endgame-Factions)

## Weaver Tree ve Woven Echoes

Ağaç puanları iki ayrı kaynaktan gelir: rank ve belirli Woven Echo tamamlamaları.
Ağaç, Monolith karşılaşma türlerini/sıklığını, ödülleri ve bazı zorlukları değiştirir.
Arena türlerini kapatma ve belirli eşya türlerini hedefleme gibi seçimler vardır.
[Resmî Weaver Tree açıklaması](https://support.lastepoch.com/hc/en-us/articles/46361875366811-What-is-the-Weaver-Tree)

Woven Echo'lar ayrı envanter sekmesinde tutulur. Cemetery'deki Altar to the Weaver
ile echo web'e yerleştirilir; Masque'den Amber karşılığı da alınabilir. Bazılarında
mevcut LP'si olan Unique eşyayı daha yüksek LP'ye yükseltmeyi deneme gibi özel
işlemler bulunur.
[Resmî Woven Echoes açıklaması](https://support.lastepoch.com/hc/en-us/articles/46361884620443-What-are-Woven-Echoes)

Memory Amber ayrıca ağaç puanlarını geri almada kullanılır.
[Resmî terimler](https://support.lastepoch.com/hc/en-us/articles/46361668314523-Common-Terminology)

## Kurulu oyunda bulunan 10 rank

**Canlı doğrulama:** 10 rank toplam 13 puan, Woven Echo'lar en fazla 40 puan;
kurulu build'deki toplam sınır **53**. Rank 1–8 birer, rank 9 iki ve rank 10 üç
puan verir. Bu değerler normal `get_MaxWeaverPoints*` ve rank asset'lerinden
okundu; `research/live/weaver-live-read.json` kanıtıdır.

`FactionData.FactionRanks` asset dizisi 10 kayıt içerir. Her rank'ın
`optionalRankValue` alanı Weaver puanını taşır; toplam puan sınırını oyunun
`get_MaxWeaverPoints*` fonksiyonları hesaplar. Rank bonus metinlerinden:

| Rank | Ağaç puanına ek erişim/etki |
|---|---|
| 1–2 | Temel rank ve puan ilerlemesi |
| 3 | Class-specific idol'ları Woven Enchanter'da reforge etme |
| 4 | Idol enchanting sonucunda daha yüksek Weaver's Touch olasılığı |
| 5 | Weaver idol'larını reforge etme |
| 6 | Puan ilerlemesi |
| 7 | Reforge sırasında ikinci Weaver affix olasılığına %10 artış |
| 8 | Puan ilerlemesi |
| 9 | Echo'larda %10 more Memory Amber drop'u |
| 10 | Son rank ve rank kaynaklı puan ilerlemesi |

Rank 10, bütün Woven Echo tamamlamaları veya bütün ağaç node'larıyla aynı durum
değildir. `completedWovenEchoTypes`, earned points ve ağaç dağılımı ayrı kayıtlar.
`factionread` bu turda rank/echo puanı sınırlarını ve kazanılmış miktarları da okur.
Bu araştırmada Weaver rank'ı, Amber, ağaç veya Woven Echo kayıtları değiştirilmedi.

## 1.5 değişiklikleri

Imprint drop'ları iyileştirildi. Rage of Morditas için Ubiquity of Rage,
Seekers of Ice and Blood ve Portents of Blood node'ları eklendi. Bunlar karşılaşma
sıklığı, özel loot ve ödül/zorluk dağılımına dokunur.
[Resmî 1.5 notları](https://lastepoch.com/patchnotes/)

Purged Horizon node'u doğal Shade corruption kazanımını kapatır. Monolith
corruption ayarını gelecekte Weaver UI ile birleştirirken bu durumu göstermeliyiz.
[Resmî corruption açıklaması](https://support.lastepoch.com/hc/en-us/articles/46363316833051-Why-is-my-corruption-not-going-above-100)

## Kod erişim haritası

Adresler araştırma kanıtıdır; backend isimle runtime çözmelidir.

| İşlem/veri | API | İncelenen RVA |
|---|---|---|
| Rank/Favor/üyelik | Normal `Faction` ve `FactionTracker` API'leri | CoF araştırma notunda |
| Rank puanı | `CalculateWeaverPointsUpToRank(int)` | `0x20DB770` |
| Toplam kazanılmış puan | `get_EarnedWeaverPoints` | `0x20E01B0` |
| Rank'tan kazanılmış puan | `get_EarnedWeaverPointsFromRank` | `0x20DFFF0` |
| Echo'dan kazanılmış puan | `get_EarnedWeaverPointsFromWovenEchoes` | `0x20E0050` |
| Toplam / rank / echo sınırı | `get_MaxWeaverPoints*` | `0x20E0440`, `0x20E03A0`, `0x20E0430` |
| Woven Echo tamamlanması | `CompleteWovenEcho(WovenEchoType)` | `0x20DB810` |
| Tamamlanma doğrulaması | `HasCompletedWovenEcho` | `0x20DBD40` |
| Ağaç efekt güncellemesi | `OnWeaverTreeChanged`, `OnWeaverTreeNodeChanged` | `0x20DCEC0`, `0x20DCFE0` |
| Node efekti | `UpdateWeaverTreeNode(effect, points)` | `0x20DE5A0` |
| Ağaç respec maliyeti | `GetMemoryAmberRespecCostForWeaverTree` | `0xD82450` |

`LocalTreeData.WeaverTreeData.EarnedWeaverPoints` UInt16, tamamlanan echo türleri
stash verisi, ağaç node dağılımı ayrı tree verisidir. Yalnızca cached efekt alanına
yazmak kayıt/model/ağaç bağlantılarını güncellemez. İleride rank/Amber, kazanılmış
puan ve node dağılımı ayrı backend işlemleri olmalı; her değişiklikten önce tüm
ilgili canlı veriler snapshot'a alınmalı.

Kaynak kanıtları: `research/live/cof-methods.tsv`, `cof-fields.tsv`, önceki
`factionread-test.json` ve yeni canlı doğrulama çıktısı (oyun/save verileri Git dışında).
