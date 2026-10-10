# Akıllı toplama ve crafting

Bu kontroller offline oyunda çalışır. Ayarlar yalnız oturum belleğinde tutulur;
craft sonucu oluşan eşya ve tüketilen materyaller oyunun normal kayıt sistemindedir.
Yeni oturumda auto pickup kapalı, seçim `all`, crafting ayarları normaldir.
UI işlemleri için `tools/loot_crafting_backend.py`, komut/parametre sözleşmesi için
`ui/catalog.json` kullanılmalıdır. `lootread` ve `craftread` sonuçlarını canlı okuyun.

## Toplama

`autopickup 1` normal pickup çağrılarını 750 ms aralıklarla çalıştırır.
Toplama seçimleri auto pickup'ı kendiliğinden açmaz. Envanter doluysa eşyanın
alınıp alınmayacağına oyun karar verir; hiçbir eşya silinmez, satılmaz veya parçalanmaz.
Kategori anahtarları yalnız EpochPact'in ek toplama çağrılarını yönetir; oyunun
kendi yakın mesafeli otomatik toplamasını engellemez. Oyunun uygunluk ve mesafe
kuralları korunur. Örneğin 1.5.2 testinde Ancient Bones 10 birim uzaktan native
çağrıya rağmen toplanmadı; 5 birimde toplandı. "Kapalı" olması oyuncunun normal
yakın mesafeli toplamasını kapatacağı anlamına gelmez.

| Komut | Anlam |
|---|---|
| `lootmode all` | Mevcut tüm eşya toplama davranışı |
| `lootmode filter` | Oyunun mevcut loot filtresinden geçen eşya |
| `lootmode quality` | Unique LP eşiği **veya** aranan T7 affix |
| `lootmode materials` | Yalnız affix shard, rune ve glyph; diğer pickup kategorileri ayrıca ayarlanır |
| `lootlp 2` | Quality için minimum LP, 0–4 |
| `loott7 1` | Quality için T7 koşulunu aç; 0 kapatır |
| `lootaffixes 1,2,3` | Aranan T7 affix ID'leri; ID/ad listesi `lootread.affixes` |
| `lootaffixes none` | Herhangi bir T7 affix; en fazla 64 ayrı ID seçilir |
| `lootfilter 1` | Quality seçiminde mevcut loot filtresini de uygula |
| `lootcategory materials 0` | Shard/rune/glyph toplamasını kapat |
| `lootcategory gold 0` | Altın toplamasını kapat |
| `lootcategory potions 0` | Potion toplamasını kapat |
| `lootcategory xp 0` | XP tome toplamasını kapat |
| `lootcategory favor 0` | Favor tome toplamasını kapat |
| `lootcategory bones 0` | Ancient Bones toplamasını kapat |
| `lootreset` | Seçimleri varsayılana döndür; auto pickup durumu değişmez |
| `lootread` | Ayar, sayaç ve runtime affix kataloğu |

Filter modu filtreye daima uyar. Quality modunda filtre ayrıca kapatılabilir.
Yüklü bir filtre yoksa oyun gibi tüm eşyalar filtreyi geçer. Geçici “filtreyi göster”
tuşu yerine tanımlı filtre kuralları sorgulanır. Filtre sorgusu başarısızsa eşya
alınmaz. Seçili affix'lerden **en az birinin** T7 olması yeterlidir. Oyun belleğinde
T7 sıfır tabanlı `affixTier=6` olarak saklanır. Weaver's Will, LP değildir.
Sayaçtaki `acceptedRequests` seçimden geçen talep sayısıdır; envantere giren eşya
sayısının garantisi değildir.

## Crafting

| Komut | Anlam |
|---|---|
| `craftfp 0.5` | Gerçek FP kaybını yarıya indir, yukarı yuvarla |
| `craftfp 0` | FP kaybı yok |
| `craftfp 1` | Normal FP maliyeti |
| `crafthope 75` | Basic Glyph of Hope için %75 FP kaybını önleme |
| `crafthope reset` | Oyunun doğal Hope ihtimali |
| `craftdespair 50` | Native uygun T1–T4 affix için %50 seal ihtimali |
| `craftdespair reset` | Oyunun doğal Despair hesabı |
| `craftshards 1` | Gerçek craft'ta tüketilen bir affix shardını geri ver |
| `craftshards 0` | Normal shard tüketimi |
| `craftrunes 0\|1` | Normal tüketim / gerçekten tüketilen bir rune'u geri ver |
| `craftglyphs 0\|1` | Normal tüketim / gerçekten tüketilen bir glyph'i geri ver |
| `craftlevel 0\|1` | Normal affix yükseltme seviye sınırı / sadece yerel Forge uygunluk kontrolünde sınırı kaldır |
| `craftforge <offline save id>` | Seçili eşya ve materyalle bir normal Forge işlemi; öncesinde canlı eşya/kayıt yedeği |
| `craftreset` | Bütün crafting ayarlarını nötr yap ve hook'ları kaldır |
| `craftread` | Forge'daki eşya ve işlemin önizlemesi, ayarlar ve hook/sayaç durumu |

Bu ayarlar normal Forge çağrısına uygulanır; affix, sealed-slot ve FP uygunluk
kontrolleri devam eder. Seviye sınırı yalnız `craftlevel 1` ile seçili affix'in
crafting uygunluk kontrolünde kaldırılır; karakter/eşya seviyesi ve ekipman
giyme sınırı değiştirilmez. FP=0 eşyaya yeni FP üretmez. Rune/glyph tüketimi ancak
ilgili koruma açılırsa değiştirilir; koruma materyal yoktan üretmez, ilk materyal
gerekir. Greater Hope ihtimalini ayarlama, özel crafting panelleri, Eternity Cache ve Weaver craftları
bu kontrolün kapsamında değildir. Rune of Creation gibi eşyanın FP'sini özellikle
sıfırlayan işlemler, normal rastgele FP **maliyetinden** ayrı oyun kurallarıdır.

`craftread.preview` normal forge eligibility mesajını, FP ve affix'leri verir.
Normal shard ekleme/yükseltme için maliyet aralığı ve varsa Hope/Despair ihtimali
gösterilir. Rune ve Glyph of Insight maliyet aralığı gösterilmez. Kritik başarı,
lucky roll, özel FP türü ve Hope gibi koşullar gerçek sonuçta etkili olabilir.
Önizleme RNG tüketmez ve kesin gelecek craft sonucunu vaat etmez.

```python
from tools import loot_crafting_backend as mod

mod.loot_mode("quality")
mod.minimum_lp(2)
mod.affixes([])               # herhangi bir T7
mod.respect_filter(True)
mod.fp_factor(0.5)
mod.glyph_chance("hope", 75)
mod.glyph_chance("despair", None)  # normal şans
mod.preserve_shards(True)
mod.preserve_runes(True)
mod.preserve_glyphs(True)
mod.bypass_level(True)
preview = mod.craft_read()
# Yalnız açık bir kullanıcı işleminde: mod.forge(preview['preview']['player']['id'])
```

## Uygulama ve doğrulama

Adresler mevcut runtime metadata'sından adla çözülür. Bugünkü GameAssembly değişimi
sonrasında dump yenilendi; eski RVA'lar yeni hook kurmak için kullanılmadı.
Pickup snapshot'ları GC ile köklenir ve çağrılar managed exception kontrolünden geçer.
Crafting yalnız yerel offline actor'ın gerçek manager/forge eşyasına uygulanır.
FP setter hook'u sadece bu eşyanın normal maliyet çağrısında etkilidir; oyunun
çıktıdaki `potentialLost` hesabı da gerçek değiştirilmiş kaybı görür. Shard geri
verme, storage + modifier-slot toplamının gerçekten bir azaldığını doğrular.
Rune/glyph koruması depodaki bütün materyal konteynerleri + iki Forge materyal
yuvasının toplamını sayar; otomatik yuva doldurma ve doğal koruma ikinci bir
materyal verilmesine neden olmaz. Hook hatası olan bir Forge cevabı başarısız
döner, fakat craft gerçekleşmiş olabilir: yedek yolu korunur, işlem tekrar edilmez.

2026-10-09 eklemelerinin kapsamı ve ayrı canlı kanıtı: `docs/ce-feature-additions.md`.

Canlı kontroller yalnız `EpCraftTest` adlı ayrı solo clone'da yapılır; research
`crafttest` komutları başka karakterde reddedilir. `tools/loot_crafting_live_check.py`
test öncesi kayıtları manifest/hashes ile yedekler; test sonunda oyun kapalıyken
orijinal kayıtlar eksiksiz geri yüklenir.

2026-10-08 canlı doğrulama, oyunun `Application.version = 1.5.2` sürümünde geçti:

- Normal native filtre SHOW/HIDE, materyal toplama ve yerdeki ekipmanın korunması.
- Normal fabrika ile üretilmiş Unique LP0 reddi / LP2 kabulü ve Exalted üzerindeki
  aranan T7 affix seçimi; normal envanter kapasitesine uyma.
- Gerçek craft'ta Basic Hope %0/%100, Despair %0/%100, FP maliyeti 0/0.5/1,
  shard koruması açık/kapalı. Reset sonrası tüm crafting hook'ları kaldırıldı.
- Crafting/toplama fault sayaçları sıfır. Orijinal 54 kayıt dosyasının test öncesi
  yedekle birebir aynı olduğu doğrulandı; ayrı test karakterinin dosyaları kaldırıldı.

Kanıt: `research/live/loot-crafting-live-check.json`,
`research/live/loot-crafting-test-manifest.json`. Native kural testleri 6260/6260.
Bu sonuçlar özel rune, Weaver/Eternity Cache veya gelecekteki oyun sürümlerini
doğrulama anlamına gelmez.

Arayüzde **General settings → Smart pickup / Crafting controls** bölümlerindedir.
LP/T7/filtre ve FP/shard ayarları **Apply changes** ile gönderilir; mod, affix,
kategori ve glyph ihtimalleri kendi butonlarından uygulanır. Hope/Despair input'u
boş bırakıldığında oyunun doğal hesabı geri gelir. T7 isim listesi **Read live**
ile yüklenir; Ctrl ile birden fazla affix seçilebilir. Oturum ayarları yeniden
başlatmada varsayılana döner.

Native/Python `craftread` verisindeki `preview` nesnesi, HTTP arayüz adaptöründe
`forgePreview` adıyla korunur; UI'nin tasarım önizlemesi `preview=true` bayrağı
ile karışmaz. Headless Edge arayüz kontrolü çoklu affix, tam LP adımı, FP/shard
taslağı, glyph input/reset ve mod reset değerlerini doğruladı; bu kontrol gerçek
IPC veya kayıt kullanmaz. Kanıt: `research/live/loot-crafting-ui-check.json` ve
`docs/ui-evidence/loot-crafting-preview.png`.
