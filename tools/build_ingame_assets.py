"""Build the in-game interface's data, images and fonts from the desktop interface's own sources.

Everything the in-game window draws comes from the same files the desktop app uses: the catalog
(ui/catalog.json), its English texts (ui/locale-en.json), the translations (ui/i18n-*.json), the
page composition (tools/ingame_pages.py, mirroring app.js) and the theme's colours, sizes and
artwork (ui/style.css, ui/assets). Images are composed here at twice the 1440x900 layout size, the
way style.css paints them, so the game only scales and draws them.

Output (--out, default native/build/ingame): ui.json, PNG/JPEG images, two static Inter fonts and
ingame.rc, which native/build.bat links into the player loader and the cores as RCDATA.

usage: py -3 tools/build_ingame_assets.py [--out DIR]
"""
from __future__ import annotations

import argparse
import hashlib
import io
import os
import json
import math
import re
import subprocess
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

try:
    from tools import ingame_pages
except ImportError:  # run as a script
    import ingame_pages  # type: ignore

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "ui"
ASSETS = UI / "assets"
S = 2          # export scale: images are made at twice the 1440x900 layout size
SS = 4         # supersampling for shapes before they are reduced to S
VERSION = 1

# :root of ui/style.css (Chronoforge). Void Atlas comes later with its own layout.
THEME = {"bg": "#07120f", "panel": "#0b1815", "ink": "#eee6d5", "muted": "#b3beb8", "gold": "#ac8950", "line": "#665436",
         "accent": "#80e2b1", "glow": "#2a9a70", "soft": "#183c2e"}

# Words app.js shows next to a value (unitFor) and for a lifetime (lifetime).
LIFETIME = {"session": "Session", "actor": "Character", "game_save": "Game save", "read_only": "Read only", "view": "View", "recovery": "Recovery"}
NO_UNIT = {"lprolls", "idolprefixes", "idolsuffixes", "craft_fp", "loot_ww", "build_weaverflat", "build_projectileflat", "build_blessingoptions", "build_minionextra", "build_totemextra",
           "build_ballistaextra", "build_forgedextra", "build_companionextra"}
AUTOMATIC_TEMPLATES = ("buildset ", "qolset ", "qolarea ", "pickupradius ", "pickupinterval ", "autopickup ", "remotepickup ", "stashpickup ", "mapreveal ", "mapset ", "tierset ",
                       "fontset ", "effectsset ", "deathrecap ", "rageset ", "ailmentset ", "invset ", "questskip ", "bossdoors ", "noblessing ", "autoshatter ", "autoshatterquiet ")
# Values the catalog does not name a source for: where app.js reads them.
EXTRA_SOURCES = {
    "pickup_radius": ("pickupradius", "radius"), "pickup_interval": ("pickupradius", "intervalSeconds"),
    "tier_icons": ("tierread", "icons"), "tier_tooltips": ("tierread", "tooltips"), "tier_lines": ("tierread", "lines"),
    "tier_rules": ("tierread", "rules"), "tier_rule_position": ("tierread", "rulePosition"), "tier_rule_parts": ("tierread", "ruleParts"),
    "tier_filter_colors": ("tierread", "filterColors"), "tier_name_colors": ("tierread", "nameColors"), "tier_special": ("tierread", "special"),
    "map_minimapsize": ("mapsettings", "minimapSize"), "map_minimapzoom": ("mapsettings", "minimapZoom"),
    "map_minimapopacity": ("mapsettings", "minimapOpacity"), "map_hideoutbutton": ("mapsettings", "hideoutButton"),
    "inventory_pages": ("invread", "pages"), "inventory_idolslots": ("invread", "idolSlots"), "inventory_idolsall": ("invread", "idolsAll"), "inventory_autosort": ("invread", "autosort"), "inventory_autosortall": ("invread", "autosortAll"), "inventory_sellkeep": ("invread", "sellKeep"),
}
# Reads whose results are JSON objects the panel can follow a path into.
JSON_READS = {"hideoutread", "buildread", "qolread", "rageread", "ailmentread", "quadread", "effectsread", "deathrecap", "pickupradius", "tierread", "mapsettings", "invread", "fontread",
              "craftread"}
# Interface texts the desktop app does not have; translated here (the app's own strings come from ui/i18n-*.json).
OWN_TEXTS = {
    "Quad stash": {"tr": "Quad depo", "ru": "Четверной тайник", "zh": "Quad stash"},
    "24 × 34 cells, four times the items (EpochPact). Kept safe in three storage tabs for when EpochPact is removed.": {"tr": "24 × 34 kare, dört kat eşya (EpochPact). EpochPact kaldırılırsa diye üç saklama sekmesinde güvende tutulur.", "ru": "24 × 34 ячейки, в четыре раза больше предметов (EpochPact). Хранятся в трёх вкладках хранения на случай удаления EpochPact.", "zh": "24 × 34 格，物品容量是原来的四倍（EpochPact）。数据另存于三个存储标签页，即使移除 EpochPact 也不会丢失。"},
    "Items lie outside the normal 12 × 17 cells of this tab. Move them into the top-left part first.": {"tr": "Bu sekmede normal 12 × 17 karenin dışında eşyalar var. Önce onları sol üst kısma taşı.", "ru": "В этой вкладке есть предметы вне обычных 12 × 17 ячеек. Сначала переместите их в левую верхнюю часть.", "zh": "部分物品位于此标签页正常的 12 × 17 格之外。请先把它们移到左上角区域。"},
    "Quad stash tabs": {"tr": "Quad depo sekmeleri", "ru": "Четверные вкладки тайника", "zh": "Quad stash 标签页"},
    "A quad tab holds 24 × 34 cells instead of 12 × 17: four times the items, drawn at half size. Open your stash before you change one. Its other three quarters are also kept in three storage tabs in the folder EpochPact quad, so without EpochPact you find every item there. A tab goes back to normal only when everything in it fits in the top-left 12 × 17.": {"tr": "Quad bir sekme 12 × 17 yerine 24 × 34 kare tutar: dört kat eşya, yarım boyutta çizilir. Değiştirmeden önce deponu aç. Diğer üç çeyreği EpochPact quad klasöründeki üç saklama sekmesinde de tutulur, böylece EpochPact olmadan her eşyayı orada bulursun. Bir sekme ancak içindeki her şey sol üstteki 12 × 17'ye sığınca normale döner.", "ru": "Четверная вкладка вмещает 24 × 34 ячейки вместо 12 × 17: в четыре раза больше предметов, нарисованных вдвое меньше. Откройте тайник, прежде чем менять вкладку. Остальные три четверти хранятся также в трёх вкладках хранения в папке EpochPact quad, так что без EpochPact все предметы будут там. Вкладка снова становится обычной, только когда всё в ней помещается в левые верхние 12 × 17.", "zh": "Quad 标签页有 24 × 34 格，而不是 12 × 17：物品容量四倍，以一半大小显示。更改之前请先打开你的仓库。其余三个四分之一区域也保存在文件夹 EpochPact quad 的三个存储标签页中，因此没有 EpochPact 时也能在那里找到所有物品。只有当标签页内所有物品都放得进左上角 12 × 17 时，才能恢复为普通标签页。"},
    "Open your stash, then press Refresh to see its tabs.": {"tr": "Deponu aç, sonra sekmelerini görmek için Yenile'ye bas.", "ru": "Откройте тайник, затем нажмите «Обновить», чтобы увидеть вкладки.", "zh": "请先打开你的仓库，然后点击刷新查看其标签页。"},
    "{0} — {1} × {2}, {3} items": {"tr": "{0} — {1} × {2}, {3} eşya", "ru": "{0} — {1} × {2}, предметов: {3}", "zh": "{0} — {1} × {2}，{3} 件物品"},
    "Grey rubble": {"tr": "Gri moloz", "ru": "Серый щебень", "zh": "灰色碎石"},
    "Mossy earth": {"tr": "Yosunlu toprak", "ru": "Мшистая земля", "zh": "青苔地面"},
    "Hive floor": {"tr": "Kovan zemini", "ru": "Пол улья", "zh": "蜂巢地面"},
    "Lava": {"tr": "Lav", "ru": "Лава", "zh": "熔岩"},
    "Flesh": {"tr": "Et", "ru": "Плоть", "zh": "血肉"},
    "Templates": {"tr": "Şablonlar", "ru": "Шаблоны", "zh": "模板"},
    "A template puts up a whole hideout at once. What you had can be brought back with Undo.": {"tr": "Şablon, bütün bir saklanma yerini tek seferde kurar. Önceki halini Geri al ile getirebilirsin.", "ru": "Шаблон сразу создаёт всё убежище. Прежний вид можно вернуть кнопкой «Отменить».", "zh": "模板可一次性搭建整个藏身处。之前的藏身处可通过撤销恢复。"},
    "Ready to use": {"tr": "Hazır şablonlar", "ru": "Готовые шаблоны", "zh": "可直接使用"},
    "Your templates": {"tr": "Şablonların", "ru": "Ваши шаблоны", "zh": "你的模板"},
    "Save this hideout as a template": {"tr": "Bu saklanma yerini şablon olarak kaydet", "ru": "Сохранить это убежище как шаблон", "zh": "将此藏身处保存为模板"},
    "Name: letters, digits, spaces": {"tr": "İsim: harf, rakam, boşluk", "ru": "Имя: буквы, цифры, пробелы", "zh": "名称：字母、数字、空格"},
    "This replaces your hideout as it is now. Undo brings it back.": {"tr": "Bu, saklanma yerinin şimdiki halinin yerine geçer. Geri al eski halini getirir.", "ru": "Это заменит ваше убежище. «Отменить» вернёт его.", "zh": "这将替换你当前的藏身处。可通过撤销恢复。"},
    "This template goes for good.": {"tr": "Bu şablon kalıcı olarak silinir.", "ru": "Этот шаблон будет удалён навсегда.", "zh": "此模板将被永久删除。"},
    "Undo": {"tr": "Geri al", "ru": "Отменить", "zh": "撤销"},
    "To share one, send a friend its file. It goes in this folder, and it shows up here:": {"tr": "Paylaşmak için dosyasını arkadaşına gönder. Bu klasöre koyunca burada görünür:", "ru": "Чтобы поделиться, отправьте файл другу. Положите его в эту папку, и он появится здесь:", "zh": "要分享模板，请把它的文件发给朋友。文件放在此文件夹中，并会显示在这里："},
    "Mossy courtyard": {"tr": "Yosunlu avlu", "ru": "Мшистый двор", "zh": "青苔庭院"},
    "Council hall": {"tr": "Konsey salonu", "ru": "Зал совета", "zh": "议事厅"},
    "Lagon palace": {"tr": "Lagon sarayı", "ru": "Дворец Лагона", "zh": "Lagon 宫殿"},
    "Morditas ring": {"tr": "Morditas halkası", "ru": "Кольцо Мордитаса", "zh": "Morditas 圆环"},
    "Crossroads": {"tr": "Kavşak", "ru": "Перекрёсток", "zh": "十字路口"},
    "Cracked stone": {"tr": "Çatlak taş", "ru": "Треснувший камень", "zh": "裂纹石地"},
    "Rocky ground": {"tr": "Kayalık", "ru": "Скалистая земля", "zh": "岩石地面"},
    "Ornate floor": {"tr": "Süslü döşeme", "ru": "Узорный пол", "zh": "华丽地板"},
    "The ground": {"tr": "Zemin", "ru": "Земля", "zh": "地面"},
    "Shape": {"tr": "Şekil", "ru": "Форма", "zh": "形状"},
    "Size": {"tr": "Boyut", "ru": "Размер", "zh": "大小"},
    "Texture": {"tr": "Doku", "ru": "Текстура", "zh": "纹理"},
    "Round": {"tr": "Yuvarlak", "ru": "Круглая", "zh": "圆形"},
    "Long hall": {"tr": "Uzun salon", "ru": "Длинный зал", "zh": "长厅"},
    "Cross": {"tr": "Haç", "ru": "Крест", "zh": "十字"},
    "Three islands": {"tr": "Üç ada", "ru": "Три острова", "zh": "三座岛屿"},
    "Small": {"tr": "Küçük", "ru": "Маленькая", "zh": "小"},
    "Medium": {"tr": "Orta", "ru": "Средняя", "zh": "中"},
    "Large": {"tr": "Büyük", "ru": "Большая", "zh": "大"},
    "Huge": {"tr": "Dev", "ru": "Огромная", "zh": "巨大"},
    "Dunes": {"tr": "Kum tepeleri", "ru": "Дюны", "zh": "沙丘"},
    "Council chamber": {"tr": "Konsey odası", "ru": "Зал совета", "zh": "议事厅堂"},
    "Lagon": {"tr": "Lagon", "ru": "Lagon", "zh": "Lagon"},
    "Morditas arena": {"tr": "Morditas arenası", "ru": "Арена Мордитаса", "zh": "Morditas 竞技场"},
    "Things that would fall off a smaller ground move in towards the middle.": {"tr": "Küçük zeminden düşecek şeyler ortaya doğru kayar.", "ru": "То, что упало бы с меньшей земли, сдвигается к центру.", "zh": "较小地面放不下的物体会向中间移动。"},
    "Your hideout": {"tr": "Saklanma yerin", "ru": "Ваше убежище", "zh": "你的藏身处"},
    "A place of your own, with anything of the game in it. Turn on Boss doors in the Monolith page, go to the Monolith Hub, then come here. Whatever you change is remembered.": {"tr": "İçinde oyunun her şeyi olabilen kendi alanın. Monolith sayfasından Boss doors'u aç, Monolith Hub'a git, sonra buraya gel. Değiştirdiğin her şey hatırlanır.", "ru": "Ваше собственное место, где может быть что угодно из игры. Включите Boss doors на странице Monolith, идите в Monolith Hub и вернитесь сюда. Все изменения запоминаются.", "zh": "属于你自己的地方，可放入游戏中的任何东西。请在 Monolith 页面开启 Boss doors，前往 Monolith Hub，然后回到这里。你做的所有更改都会被记住。"},
    "The hideout is ready.": {"tr": "Saklanma yeri hazır.", "ru": "Убежище готово.", "zh": "藏身处已就绪。"},
    "The hideout opens in the Monolith Hub, with Boss doors on.": {"tr": "Saklanma yeri, Boss doors açıkken Monolith Hub'da açılır.", "ru": "Убежище открывается в Monolith Hub при включённом Boss doors.", "zh": "藏身处在 Monolith Hub 中开启，并启用 Boss doors。"},
    "Start again": {"tr": "Baştan başla", "ru": "Начать заново", "zh": "重新开始"},
    "Your hideout goes back to how it was at first. Everything you placed goes.": {"tr": "Saklanma yerin ilk haline döner. Yerleştirdiğin her şey gider.", "ru": "Убежище вернётся к исходному виду. Всё, что вы поставили, исчезнет.", "zh": "你的藏身处将恢复到最初的样子。你放置的所有东西都会消失。"},
    "Finish editing": {"tr": "Düzenlemeyi bitir", "ru": "Закончить", "zh": "完成编辑"},
    "Edit with the mouse": {"tr": "Fareyle düzenle", "ru": "Редактировать мышью", "zh": "用鼠标编辑"},
    "Go to the hideout": {"tr": "Saklanma yerine git", "ru": "В убежище", "zh": "前往藏身处"},
    "In the editor: left click picks a thing up and puts it down. Arrow keys left and right turn it, up and down size it, Page Up and Page Down lift it. Delete removes it. End finishes.": {"tr": "Düzenleyicide: sol tık bir şeyi alır ve bırakır. Sol ve sağ ok döndürür, yukarı ve aşağı ok boyutlandırır, Page Up ve Page Down yükseltir. Delete kaldırır. End bitirir.", "ru": "В редакторе: левый клик берёт и ставит предмет. Стрелки влево и вправо поворачивают, вверх и вниз меняют размер, Page Up и Page Down поднимают. Delete удаляет. End завершает.", "zh": "编辑器中：左键点击拾起并放下物体。左右方向键旋转，上下方向键缩放，Page Up 和 Page Down 升降。Delete 删除。End 结束。"},
    "Add anything of the game": {"tr": "Oyundan istediğini ekle", "ru": "Добавить что угодно из игры", "zh": "添加游戏中的任何东西"},
    "Search: statue, wall, tree, chest, fountain...": {"tr": "Ara: statue, wall, tree, chest, fountain...", "ru": "Поиск: statue, wall, tree, chest, fountain...", "zh": "搜索：雕像、墙、树、箱子、喷泉..."},
    "Everything": {"tr": "Hepsi", "ru": "Всё", "zh": "全部"},
    "Buildings": {"tr": "Yapılar", "ru": "Постройки", "zh": "建筑"},
    "Nature": {"tr": "Doğa", "ru": "Природа", "zh": "自然"},
    "Things": {"tr": "Eşyalar", "ru": "Предметы", "zh": "物件"},
    "Creatures": {"tr": "Yaratıklar", "ru": "Существа", "zh": "生物"},
    "Other": {"tr": "Diğer", "ru": "Другое", "zh": "其他"},
    "This list was made for another game version. It comes with the next EpochPact update.": {"tr": "Bu liste başka bir oyun sürümü için yapıldı. Sonraki EpochPact güncellemesiyle gelir.", "ru": "Этот список сделан для другой версии игры. Он появится со следующим обновлением EpochPact.", "zh": "此列表是为另一个游戏版本制作的，将随 EpochPact 的下次更新提供。"},
    "Showing {0} of {1}. Type more to narrow the list.": {"tr": "{1} sonuçtan {0} tanesi gösteriliyor. Daraltmak için daha fazla yaz.", "ru": "Показано {0} из {1}. Уточните поиск.", "zh": "显示 {1} 项中的 {0} 项。输入更多内容以缩小列表。"},
    "{0} found.": {"tr": "{0} sonuç bulundu.", "ru": "Найдено: {0}.", "zh": "找到 {0} 项。"},
    "Add": {"tr": "Ekle", "ru": "Добавить", "zh": "添加"},
    "What stands there": {"tr": "Orada duranlar", "ru": "Что там стоит", "zh": "此处有什么"},
    "Nothing read yet.": {"tr": "Henüz bir şey okunmadı.", "ru": "Пока ничего не прочитано.", "zh": "尚未读取。"},
    "Boss door {0}": {"tr": "Boss kapısı {0}", "ru": "Дверь босса {0}", "zh": "Boss 门 {0}"},
    "Way out": {"tr": "Çıkış", "ru": "Выход", "zh": "出口"},
    "Difficulty door": {"tr": "Zorluk kapısı", "ru": "Дверь сложности", "zh": "难度之门"},
    "Boss Rush door": {"tr": "Boss Rush kapısı", "ru": "Дверь Boss Rush", "zh": "Boss Rush 门"},
    "(not found)": {"tr": "(bulunamadı)", "ru": "(не найдено)", "zh": "（未找到）"},
    "Bring back": {"tr": "Geri getir", "ru": "Вернуть", "zh": "恢复"},
    "(taken away)": {"tr": "(kaldırıldı)", "ru": "(убрано)", "zh": "（已移除）"},
    "Pick up": {"tr": "Eline al", "ru": "Взять", "zh": "拾起"},
    "Here": {"tr": "Buraya", "ru": "Сюда", "zh": "此处"},
    "Remove": {"tr": "Kaldır", "ru": "Убрать", "zh": "移除"},
    "This section is not in the in-game window yet. Use the EpochPact app for it.": {"tr": "Bu bölüm henüz oyun içi pencerede yok. Bunun için EpochPact uygulamasını kullanın.", "ru": "Этого раздела пока нет в игровом окне. Используйте для него приложение EpochPact.", "zh": "游戏内窗口尚未包含此部分。请使用 EpochPact 应用。"},
    "F6 closes this window.": {"tr": "F6 bu pencereyi kapatır.", "ru": "F6 закрывает это окно.", "zh": "按 F6 关闭此窗口。"},
    "In game": {"tr": "Oyunda", "ru": "В игре", "zh": "游戏内"},
    "Window": {"tr": "Pencere", "ru": "Окно", "zh": "窗口"},
    "Full screen": {"tr": "Tam ekran", "ru": "Во весь экран", "zh": "全屏"},
    "Left half": {"tr": "Sol yarı", "ru": "Левая половина", "zh": "左半屏"},
    "Right half": {"tr": "Sağ yarı", "ru": "Правая половина", "zh": "右半屏"},
    "Default size": {"tr": "Varsayılan boyut", "ru": "Обычный размер", "zh": "默认大小"},
    "& combines requirements; | accepts either condition. Tooltip text searches use the game’s current language.": {"tr": "& gereksinimleri birleştirir; | iki koşuldan birini kabul eder. İpucu metni aramaları oyunun geçerli dilini kullanır.", "ru": "& объединяет требования; | принимает любое из условий. Поиск по тексту подсказок использует текущий язык игры.", "zh": "& 表示同时满足所有条件；| 表示满足任一条件。提示文字搜索使用游戏当前的语言。"},
    "Add & place": {"tr": "Ekle ve yerleştir", "ru": "Добавить и разместить", "zh": "添加并放置"},
    "Add objects": {"tr": "Nesne ekle", "ru": "Добавить объекты", "zh": "添加物体"},
    "Align to selection anchor": {"tr": "Seçim çapasına hizala", "ru": "Выровнять по якорю выделения", "zh": "对齐到选择锚点"},
    "Altars": {"tr": "Sunaklar", "ru": "Алтари", "zh": "祭坛"},
    "Any type": {"tr": "Herhangi bir tür", "ru": "Любой тип", "zh": "任意类型"},
    "Applied once each time an offline character is ready. Your last used settings include loot modes, collection categories, wanted affixes and the font. Profiles and window preferences stay on this device.": {"tr": "Çevrimdışı bir karakter hazır olduğunda her seferinde bir kez uygulanır. Son kullandığın ayarlar loot modlarını, koleksiyon kategorilerini, istenen affix'leri ve yazı tipini içerir. Profiller ve pencere tercihleri bu cihazda kalır.", "ru": "Применяется один раз при каждой готовности офлайн-персонажа. Ваши последние настройки включают режимы добычи, категории сбора, нужные аффиксы и шрифт. Профили и настройки окна остаются на этом устройстве.", "zh": "每次离线角色准备就绪时应用一次。上次使用的设置包括拾取模式、收集类别、想要的词缀和字体。配置方案和窗口偏好保留在此设备上。"},
    "Apply colours": {"tr": "Renkleri uygula", "ru": "Применить цвета", "zh": "应用颜色"},
    "Back to editing": {"tr": "Düzenlemeye dön", "ru": "Назад к редактированию", "zh": "返回编辑"},
    "Back up and share your Last Epoch loot filters. Imports and copies are new files; existing filters are never replaced.": {"tr": "Last Epoch loot filtrelerini yedekle ve paylaş. İçe aktarmalar ve kopyalar yeni dosya olur; mevcut filtrelerin üzerine asla yazılmaz.", "ru": "Сохраняйте резервные копии фильтров добычи Last Epoch и делитесь ими. Импорт и копии создаются как новые файлы; существующие фильтры никогда не заменяются.", "zh": "备份和分享你的 Last Epoch 物品过滤器。导入和复制都会生成新文件，不会替换现有过滤器。"},
    "Background colour": {"tr": "Arka plan rengi", "ru": "Цвет фона", "zh": "背景颜色"},
    "Banners": {"tr": "Bayraklar", "ru": "Знамёна", "zh": "旗帜"},
    "CHRONOFORGE": {"tr": "CHRONOFORGE", "ru": "CHRONOFORGE", "zh": "CHRONOFORGE"},
    "Changes to your game save create a recovery backup in Last Epoch\\EpochPact\\backups. Restore backups with the game closed, using the desktop recovery tools. In-game restoration is disabled to protect your loaded character.": {"tr": "Oyun kaydında yapılan değişiklikler Last Epoch\\EpochPact\\backups klasöründe bir kurtarma yedeği oluşturur. Yedekleri oyun kapalıyken, masaüstü kurtarma araçlarıyla geri yükle. Yüklü karakterini korumak için oyun içi geri yükleme devre dışıdır.", "ru": "При изменении сохранения игры создаётся резервная копия в Last Epoch\\EpochPact\\backups. Восстанавливайте копии при закрытой игре с помощью настольных средств восстановления. Восстановление в игре отключено для защиты загруженного персонажа.", "zh": "对游戏存档的更改会在 Last Epoch\\EpochPact\\backups 中创建恢复备份。请在关闭游戏后使用桌面恢复工具还原备份。游戏内还原已禁用，以保护已加载的角色。"},
    "Choose an object": {"tr": "Bir nesne seç", "ru": "Выберите объект", "zh": "选择一个物体"},
    "Click a model to select it. The green copy follows your cursor; Q/E rotates it.": {"tr": "Bir modele tıkla ve seç. Yeşil kopya imlecini takip eder; Q/E ile döner.", "ru": "Нажмите на модель, чтобы выбрать её. Зелёная копия следует за курсором; Q/E поворачивает её.", "zh": "点击模型进行选择。绿色副本会跟随光标；Q/E 旋转。"},
    "Click to place · Right-click: cancel · Q/E: rotate · Delete: remove · Ctrl+Z/Y: undo/redo": {"tr": "Tıkla: yerleştir · Sağ tık: iptal · Q/E: döndür · Delete: kaldır · Ctrl+Z/Y: geri al/yinele", "ru": "Клик: разместить · ПКМ: отмена · Q/E: поворот · Delete: удалить · Ctrl+Z/Y: отменить/повторить", "zh": "点击放置 · 右键：取消 · Q/E：旋转 · Delete：删除 · Ctrl+Z/Y：撤销/重做"},
    "Click: pick up / drop · Q/E: rotate · Up/Down: scale · Page Up/Down: height · Delete: remove · End: finish": {"tr": "Tıkla: al / bırak · Q/E: döndür · Yukarı/Aşağı: ölçekle · Page Up/Down: yükseklik · Delete: kaldır · End: bitir", "ru": "Клик: взять / поставить · Q/E: поворот · Вверх/Вниз: масштаб · Page Up/Down: высота · Delete: удалить · End: завершить", "zh": "点击：拿起/放下 · Q/E：旋转 · 上/下：缩放 · Page Up/Down：高度 · Delete：删除 · End：完成"},
    "Close Last Epoch before replacing the mod DLL. Download the complete matching package; your local preferences are kept.": {"tr": "Mod DLL'ini değiştirmeden önce Last Epoch'u kapat. Eşleşen paketin tamamını indir; yerel tercihlerin korunur.", "ru": "Закройте Last Epoch перед заменой DLL мода. Скачайте полный подходящий пакет; ваши локальные настройки сохранятся.", "zh": "替换模组 DLL 前请关闭 Last Epoch。请下载完整且匹配的软件包；本地偏好设置会保留。"},
    "Columns": {"tr": "Sütunlar", "ru": "Столбцы", "zh": "列"},
    "Compact table": {"tr": "Sıkı tablo", "ru": "Компактная таблица", "zh": "紧凑表格"},
    "Completed changes can be restored with Undo.": {"tr": "Tamamlanan değişiklikler Geri al ile eski haline getirilebilir.", "ru": "Выполненные изменения можно вернуть кнопкой «Отменить».", "zh": "已完成的更改可通过撤销恢复。"},
    "Component": {"tr": "Bileşen", "ru": "Компонент", "zh": "组件"},
    "Could not save local preferences.": {"tr": "Yerel tercihler kaydedilemedi.", "ru": "Не удалось сохранить локальные настройки.", "zh": "无法保存本地偏好设置。"},
    "Could not save local preferences. Check the EpochPact folder permissions.": {"tr": "Yerel tercihler kaydedilemedi. EpochPact klasörünün izinlerini kontrol et.", "ru": "Не удалось сохранить локальные настройки. Проверьте права доступа к папке EpochPact.", "zh": "无法保存本地偏好设置。请检查 EpochPact 文件夹的权限。"},
    "Could not save window position.": {"tr": "Pencere konumu kaydedilemedi.", "ru": "Не удалось сохранить положение окна.", "zh": "无法保存窗口位置。"},
    "Crystals": {"tr": "Kristaller", "ru": "Кристаллы", "zh": "水晶"},
    "Ctrl-click models or use the Placed objects checkboxes to select up to 32 objects.": {"tr": "En fazla 32 nesne seçmek için modellere Ctrl+tıkla veya Yerleştirilen nesneler onay kutularını kullan.", "ru": "Ctrl+клик по моделям или флажки в списке размещённых объектов позволяют выбрать до 32 объектов.", "zh": "按住 Ctrl 点击模型或使用已放置物体复选框，最多可选择 32 个物体。"},
    "Ctrl-click: add/remove selection · Move selection to drag · Tools: align, group and exact values": {"tr": "Ctrl+tık: seçime ekle/çıkar · Sürüklemek için seçimi taşı · Araçlar: hizala, grupla ve tam değerler", "ru": "Ctrl+клик: добавить/убрать из выделения · Перемещение выделения: перетаскивание · Инструменты: выравнивание, группа и точные значения", "zh": "Ctrl+点击：添加/移除选择 · 移动选择以拖动 · 工具：对齐、编组和精确数值"},
    "Delete": {"tr": "Sil", "ru": "Удалить", "zh": "删除"},
    "Delete template": {"tr": "Şablonu sil", "ru": "Удалить шаблон", "zh": "删除模板"},
    "Delete this saved template permanently? Your current hideout stays as it is.": {"tr": "Bu kayıtlı şablon kalıcı olarak silinsin mi? Mevcut saklanma yerin olduğu gibi kalır.", "ru": "Удалить этот сохранённый шаблон навсегда? Ваше текущее убежище останется как есть.", "zh": "永久删除此已保存的模板吗？你当前的藏身处不会改变。"},
    "Done": {"tr": "Bitti", "ru": "Готово", "zh": "完成"},
    "Editor tools": {"tr": "Düzenleyici araçları", "ru": "Инструменты редактора", "zh": "编辑工具"},
    "Enter the hideout before adding or arranging objects.": {"tr": "Nesne eklemeden veya düzenlemeden önce saklanma yerine gir.", "ru": "Войдите в убежище, прежде чем добавлять или расставлять объекты.", "zh": "请先进入藏身处，再添加或排列物体。"},
    "Eternity Cache": {"tr": "Eternity Cache", "ru": "Eternity Cache", "zh": "Eternity Cache"},
    "Exact transform": {"tr": "Tam dönüşüm", "ru": "Точное преобразование", "zh": "精确变换"},
    "Favorites": {"tr": "Favoriler", "ru": "Избранное", "zh": "收藏"},
    "Find a model": {"tr": "Model bul", "ru": "Найти модель", "zh": "查找模型"},
    "Find placed objects": {"tr": "Yerleştirilen nesneleri bul", "ru": "Найти размещённые объекты", "zh": "查找已放置物体"},
    "Floors & paths": {"tr": "Zeminler ve yollar", "ru": "Полы и дорожки", "zh": "地板与小路"},
    "Forward / Z": {"tr": "İleri / Z", "ru": "Вперёд / Z", "zh": "前 / Z"},
    "Furniture": {"tr": "Mobilyalar", "ru": "Мебель", "zh": "家具"},
    "Grid snap": {"tr": "Izgaraya yapış", "ru": "Привязка к сетке", "zh": "网格吸附"},
    "Group": {"tr": "Grupla", "ru": "Группа", "zh": "编组"},
    "HIDEOUT EDITOR": {"tr": "SAKLANMA YERİ DÜZENLEYİCİ", "ru": "РЕДАКТОР УБЕЖИЩА", "zh": "藏身处编辑器"},
    "Height": {"tr": "Yükseklik", "ru": "Высота", "zh": "高度"},
    "Hideout Builder": {"tr": "Hideout Oluşturucu", "ru": "Конструктор убежища", "zh": "藏身处建造器"},
    "Hideout tools": {"tr": "Hideout araçları", "ru": "Инструменты убежища", "zh": "藏身处工具"},
    "Hover a picture to inspect it. Add picks it up for placement.": {"tr": "İncelemek için bir resmin üzerine gel. Ekle, yerleştirmek üzere onu alır.", "ru": "Наведите курсор на картинку для просмотра. Кнопка «Добавить» берёт её для размещения.", "zh": "悬停在图片上可查看。点击添加即可拿起并放置。"},
    "Hover or click a picture to inspect. Add & place picks up a green copy; right-click cancels placement.": {"tr": "İncelemek için bir resme gel veya tıkla. Ekle ve yerleştir yeşil bir kopya alır; sağ tık yerleştirmeyi iptal eder.", "ru": "Наведите курсор или нажмите на картинку для просмотра. «Добавить и разместить» берёт зелёную копию; ПКМ отменяет размещение.", "zh": "悬停或点击图片可查看。添加并放置会拿起绿色副本；右键取消放置。"},
    "Import filter": {"tr": "Filtreyi içe aktar", "ru": "Импортировать фильтр", "zh": "导入过滤器"},
    "Import loot filter": {"tr": "Loot filtresini içe aktar", "ru": "Импортировать фильтр добычи", "zh": "导入物品过滤器"},
    "In game · F6": {"tr": "Oyunda · F6", "ru": "В игре · F6", "zh": "游戏内 · F6"},
    "In-game interface · Chronoforge": {"tr": "Oyun içi arayüz · Chronoforge", "ru": "Интерфейс в игре · Chronoforge", "zh": "游戏内界面 · Chronoforge"},
    "Inspect a model before placing it.": {"tr": "Yerleştirmeden önce bir modeli incele.", "ru": "Осмотрите модель перед размещением.", "zh": "放置前先检查模型。"},
    "Installation and removal require Last Epoch to be closed. Replace the complete matching package when updating. Existing game saves and unrelated mods are not installation targets.": {"tr": "Kurulum ve kaldırma için Last Epoch'un kapalı olması gerekir. Güncellerken eşleşen paketin tamamını değiştir. Mevcut oyun kayıtları ve ilgisiz modlar kurulum hedefi değildir.", "ru": "Для установки и удаления нужно закрыть Last Epoch. При обновлении заменяйте весь подходящий пакет. Существующие сохранения игры и сторонние моды не затрагиваются установкой.", "zh": "安装和卸载需要先关闭 Last Epoch。更新时请替换完整且匹配的软件包。现有游戏存档和无关的模组不属于安装目标。"},
    "Interface": {"tr": "Arayüz", "ru": "Интерфейс", "zh": "界面"},
    "Invalid collection page.": {"tr": "Geçersiz koleksiyon sayfası.", "ru": "Недопустимая страница коллекции.", "zh": "无效的收集页面。"},
    "Last: ": {"tr": "Son: ", "ru": "Последнее: ", "zh": "上次："},
    "Lights (test effects)": {"tr": "Işıklar (test efektleri)", "ru": "Освещение (тестовые эффекты)", "zh": "灯光（测试效果）"},
    "Load": {"tr": "Yükle", "ru": "Загрузить", "zh": "加载"},
    "Load loot filter files": {"tr": "Loot filtresi dosyalarını yükle", "ru": "Загрузить файлы фильтров добычи", "zh": "加载物品过滤器文件"},
    "Loading preview…": {"tr": "Önizleme yükleniyor…", "ru": "Загрузка предпросмотра…", "zh": "正在加载预览…"},
    "Model type (from asset names)": {"tr": "Model türü (varlık adlarından)", "ru": "Тип модели (по названиям ассетов)", "zh": "模型类型（来自资源名称）"},
    "Move inside the hideout · Right-click: cancel · Q/E: rotate · Delete: remove": {"tr": "Saklanma yerinin içinde hareket et · Sağ tık: iptal · Q/E: döndür · Delete: kaldır", "ru": "Перемещение в убежище · ПКМ: отмена · Q/E: поворот · Delete: удалить", "zh": "在藏身处内移动 · 右键：取消 · Q/E：旋转 · Delete：删除"},
    "Native hideout station · use Move to inspect its position": {"tr": "Yerel saklanma yeri istasyonu · konumunu görmek için Taşı'yı kullan", "ru": "Встроенная станция убежища · используйте «Переместить», чтобы увидеть её положение", "zh": "原生藏身处设施 · 使用移动查看其位置"},
    "New name": {"tr": "Yeni ad", "ru": "Новое имя", "zh": "新名称"},
    "Next": {"tr": "İleri", "ru": "Далее", "zh": "下一页"},
    "No loot filters found. Use Import filter to add an XML file or a filter backup ZIP.": {"tr": "Loot filtresi bulunamadı. XML dosyası veya filtre yedeği ZIP eklemek için Filtreyi içe aktar'ı kullan.", "ru": "Фильтры добычи не найдены. Используйте «Импортировать фильтр», чтобы добавить XML-файл или ZIP-архив с резервной копией фильтров.", "zh": "未找到物品过滤器。请使用导入过滤器添加 XML 文件或过滤器备份 ZIP。"},
    "No models match these words and category.": {"tr": "Hiçbir model bu kelimeler ve kategoriyle eşleşmiyor.", "ru": "Нет моделей, подходящих под эти слова и категорию.", "zh": "没有符合这些关键词和类别的模型。"},
    "No objects selected": {"tr": "Seçili nesne yok", "ru": "Объекты не выбраны", "zh": "未选择物体"},
    "Open timelines from End of Time, Monolith Hub or Traveler's Rest.": {"tr": "Zaman çizelgelerini End of Time, Monolith Hub veya Traveler's Rest'ten aç.", "ru": "Открывайте хронологии в End of Time, Monolith Hub или Traveler's Rest.", "zh": "可从 End of Time、Monolith Hub 或 Traveler's Rest 打开时间线。"},
    "Overwrite template": {"tr": "Şablonun üzerine yaz", "ru": "Перезаписать шаблон", "zh": "覆盖模板"},
    "Page": {"tr": "Sayfa", "ru": "Страница", "zh": "页"},
    "Paste the full path to a loot filter XML file or an EpochPact backup ZIP. Every filter is checked first and imported as a new file. Existing filters are kept.": {"tr": "Bir loot filtresi XML dosyasının veya EpochPact yedeği ZIP'inin tam yolunu yapıştır. Her filtre önce kontrol edilir ve yeni dosya olarak içe aktarılır. Mevcut filtreler korunur.", "ru": "Вставьте полный путь к XML-файлу фильтра добычи или ZIP-архиву резервной копии EpochPact. Каждый фильтр сначала проверяется и импортируется как новый файл. Существующие фильтры сохраняются.", "zh": "粘贴物品过滤器 XML 文件或 EpochPact 备份 ZIP 的完整路径。每个过滤器都会先经过检查，并作为新文件导入。现有过滤器会保留。"},
    "Place": {"tr": "Yerleştir", "ru": "Разместить", "zh": "放置"},
    "Place new objects before grouping, aligning or locking them.": {"tr": "Nesneleri gruplamadan, hizalamadan veya kilitlemeden önce yenilerini yerleştir.", "ru": "Разместите новые объекты, прежде чем группировать, выравнивать или блокировать их.", "zh": "请先放置新物体，再进行编组、对齐或锁定。"},
    "Placed": {"tr": "Yerleştirilen", "ru": "Размещено", "zh": "已放置"},
    "Placement options": {"tr": "Yerleştirme seçenekleri", "ru": "Параметры размещения", "zh": "放置选项"},
    "Plants": {"tr": "Bitkiler", "ru": "Растения", "zh": "植物"},
    "Previous": {"tr": "Önceki", "ru": "Назад", "zh": "上一页"},
    "Profile: ": {"tr": "Profil: ", "ru": "Профиль: ", "zh": "配置方案："},
    "Q/E rotation step": {"tr": "Q/E döndürme adımı", "ru": "Шаг поворота Q/E", "zh": "Q/E 旋转步长"},
    "Recently used": {"tr": "Son kullanılanlar", "ru": "Недавние", "zh": "最近使用"},
    "Redo": {"tr": "Yinele", "ru": "Повторить", "zh": "重做"},
    "Removed": {"tr": "Kaldırılan", "ru": "Удалено", "zh": "已移除"},
    "Rename": {"tr": "Yeniden adlandır", "ru": "Переименовать", "zh": "重命名"},
    "Rename template": {"tr": "Şablonu yeniden adlandır", "ru": "Переименовать шаблон", "zh": "重命名模板"},
    "Replace the current hideout? Undo restores your previous layout.": {"tr": "Mevcut saklanma yeri değiştirilsin mi? Geri al, önceki düzenini geri getirir.", "ru": "Заменить текущее убежище? Кнопка «Отменить» вернёт прежнюю планировку.", "zh": "替换当前藏身处吗？撤销可恢复你之前的布局。"},
    "Replace this saved template with your current hideout?": {"tr": "Bu kayıtlı şablonun yerine mevcut saklanma yerin kaydedilsin mi?", "ru": "Заменить этот сохранённый шаблон вашим текущим убежищем?", "zh": "用你当前的藏身处替换此已保存的模板吗？"},
    "Report saved in the game's EpochPact\\reports folder. Nothing was uploaded.": {"tr": "Rapor oyunun EpochPact\\reports klasörüne kaydedildi. Hiçbir şey yüklenmedi.", "ru": "Отчёт сохранён в папке EpochPact\\reports игры. Ничего не отправлено.", "zh": "报告已保存到游戏的 EpochPact\\reports 文件夹。未上传任何内容。"},
    "Right / X": {"tr": "Sağ / X", "ru": "Вправо / X", "zh": "右 / X"},
    "Rocks": {"tr": "Kayalar", "ru": "Камни", "zh": "岩石"},
    "Rotation": {"tr": "Döndürme", "ru": "Поворот", "zh": "旋转"},
    "Rotation (degrees)": {"tr": "Döndürme (derece)", "ru": "Поворот (градусы)", "zh": "旋转（度）"},
    "Save": {"tr": "Kaydet", "ru": "Сохранить", "zh": "保存"},
    "Save a report in Last Epoch\\EpochPact\\reports: a screenshot, game and EpochPact state, recent logs and a description.txt to fill in. Nothing is uploaded. Windows user and computer names are masked; reports can contain character names.": {"tr": "Last Epoch\\EpochPact\\reports klasörüne bir rapor kaydet: ekran görüntüsü, oyun ve EpochPact durumu, son kayıtlar ve doldurman için bir description.txt. Hiçbir şey yüklenmez. Windows kullanıcı ve bilgisayar adları maskelenir; raporlar karakter adlarını içerebilir.", "ru": "Сохраните отчёт в Last Epoch\\EpochPact\\reports: снимок экрана, состояние игры и EpochPact, последние журналы и файл description.txt для заполнения. Ничего не отправляется. Имена пользователя Windows и компьютера скрываются; отчёты могут содержать имена персонажей.", "zh": "在 Last Epoch\\EpochPact\\reports 中保存报告：包含截图、游戏和 EpochPact 状态、近期日志以及需要填写的 description.txt。不会上传任何内容。Windows 用户名和计算机名会被隐藏；报告中可能包含角色名称。"},
    "Scale": {"tr": "Ölçek", "ru": "Масштаб", "zh": "缩放"},
    "Search your layout…": {"tr": "Düzeninde ara…", "ru": "Поиск по вашей планировке…", "zh": "搜索你的布局…"},
    "Search: statue, wall, tree, chest…": {"tr": "Ara: heykel, duvar, ağaç, sandık…", "ru": "Поиск: статуя, стена, дерево, сундук…", "zh": "搜索：雕像、墙、树、箱子…"},
    "Select": {"tr": "Seç", "ru": "Выбрать", "zh": "选择"},
    "Select floor pieces with the mouse": {"tr": "Zemin parçalarını fareyle seç", "ru": "Выбирать части пола мышью", "zh": "用鼠标选择地板块"},
    "Setting search": {"tr": "Ayar ara", "ru": "Поиск настроек", "zh": "设置搜索"},
    "Stash": {"tr": "Depo", "ru": "Тайник", "zh": "仓库"},
    "Static game model. Place the green preview in the world, then click to confirm. Q/E rotates it.": {"tr": "Statik oyun modeli. Yeşil önizlemeyi dünyaya yerleştir, sonra onaylamak için tıkla. Q/E ile döner.", "ru": "Статичная игровая модель. Поместите зелёный предпросмотр в мир, затем нажмите для подтверждения. Q/E поворачивает её.", "zh": "静态游戏模型。将绿色预览放到场景中，然后点击确认。Q/E 旋转。"},
    "Static in-game model preview · ": {"tr": "Statik oyun içi model önizlemesi · ", "ru": "Предпросмотр статичной игровой модели · ", "zh": "静态游戏内模型预览 · "},
    "Statues": {"tr": "Heykeller", "ru": "Статуи", "zh": "雕像"},
    "The action could not be completed.": {"tr": "İşlem tamamlanamadı.", "ru": "Не удалось выполнить действие.", "zh": "无法完成该操作。"},
    "The asset catalog does not match this game build. Update EpochPact before placing models.": {"tr": "Varlık kataloğu bu oyun sürümüyle eşleşmiyor. Model yerleştirmeden önce EpochPact'i güncelle.", "ru": "Каталог ассетов не соответствует этой версии игры. Обновите EpochPact перед размещением моделей.", "zh": "资源目录与此游戏版本不匹配。放置模型前请更新 EpochPact。"},
    "The game did not confirm the selected Echo.": {"tr": "Oyun seçilen Echo'yu onaylamadı.", "ru": "Игра не подтвердила выбранный Echo.", "zh": "游戏未确认所选的 Echo。"},
    "The loaded character changed during travel.": {"tr": "Yüklü karakter yolculuk sırasında değişti.", "ru": "Загруженный персонаж изменился во время перемещения.", "zh": "已加载的角色在旅行期间发生了变化。"},
    "The loaded character changed while opening the timeline.": {"tr": "Yüklü karakter zaman çizelgesi açılırken değişti.", "ru": "Загруженный персонаж изменился при открытии хронологии.", "zh": "已加载的角色在打开时间线时发生了变化。"},
    "The loaded character changed. Refresh live data.": {"tr": "Yüklü karakter değişti. Canlı verileri yenile.", "ru": "Загруженный персонаж изменился. Обновите данные.", "zh": "已加载的角色发生了变化。请刷新实时数据。"},
    "The local draft “": {"tr": "Yerel taslak “", "ru": "Локальный черновик “", "zh": "本地草稿“"},
    "The native player mod is loaded in this game. No separate EpochPact EXE or MelonLoader is required. Gameplay changes are available only for a ready offline character.": {"tr": "Yerel oyuncu modu bu oyunda yüklü. Ayrı bir EpochPact EXE'si veya MelonLoader gerekmez. Oynanış değişiklikleri yalnızca hazır bir çevrimdışı karakter için kullanılabilir.", "ru": "Встроенный мод игрока загружен в эту игру. Отдельный EXE EpochPact или MelonLoader не нужен. Изменения геймплея доступны только для готового офлайн-персонажа.", "zh": "原生玩家模组已在此游戏中加载。无需单独的 EpochPact EXE 或 MelonLoader。仅在离线角色准备就绪时才可使用游戏玩法更改。"},
    "The timeline is still loading. The opening request was not repeated.": {"tr": "Zaman çizelgesi hâlâ yükleniyor. Açma isteği tekrarlanmadı.", "ru": "Хронология всё ещё загружается. Запрос на открытие не был повторён.", "zh": "时间线仍在加载。未重复发送打开请求。"},
    "The whole layout must fit a smaller ground. Move edge objects inward first or choose a larger size.": {"tr": "Tüm düzen daha küçük bir zemine sığmalı. Önce kenardaki nesneleri içeri taşı veya daha büyük bir boyut seç.", "ru": "Вся планировка должна поместиться на меньшей площади. Сначала сдвиньте крайние объекты внутрь или выберите больший размер.", "zh": "整个布局必须适合较小的地面。请先将边缘物体向内移动，或选择更大的尺寸。"},
    "Tools": {"tr": "Araçlar", "ru": "Инструменты", "zh": "工具"},
    "Traveler's Rest did not respond.": {"tr": "Traveler's Rest yanıt vermedi.", "ru": "Traveler's Rest не ответил.", "zh": "Traveler's Rest 没有响应。"},
    "Traveler's Rest has not finished loading. Resume the game and try again.": {"tr": "Traveler's Rest yüklemeyi bitirmedi. Oyuna devam et ve tekrar dene.", "ru": "Traveler's Rest ещё не загрузился. Возобновите игру и повторите попытку.", "zh": "Traveler's Rest 尚未加载完成。请恢复游戏后重试。"},
    "Trees": {"tr": "Ağaçlar", "ru": "Деревья", "zh": "树木"},
    "Unlock this object before moving or deleting it.": {"tr": "Taşımadan veya silmeden önce bu nesnenin kilidini aç.", "ru": "Разблокируйте этот объект, прежде чем перемещать или удалять его.", "zh": "移动或删除此物体前请先解锁。"},
    "Use a shorter profile name.": {"tr": "Daha kısa bir profil adı kullan.", "ru": "Используйте более короткое имя профиля.", "zh": "请使用更短的配置方案名称。"},
    "Use six hexadecimal digits, for example #091310.": {"tr": "Altı onaltılık rakam kullan, örneğin #091310.", "ru": "Введите шесть шестнадцатеричных цифр, например #091310.", "zh": "请输入六位十六进制数字，例如 #091310。"},
    "Use this template": {"tr": "Bu şablonu kullan", "ru": "Использовать этот шаблон", "zh": "使用此模板"},
    "Vendor": {"tr": "Satıcı", "ru": "Торговец", "zh": "商人"},
    "Wait until your offline character is ready before changing layout history.": {"tr": "Düzen geçmişini değiştirmeden önce çevrimdışı karakterinin hazır olmasını bekle.", "ru": "Дождитесь готовности офлайн-персонажа, прежде чем менять историю планировки.", "zh": "请等待离线角色准备就绪后再更改布局历史。"},
    "Walls": {"tr": "Duvarlar", "ru": "Стены", "zh": "墙"},
    "XML or ZIP file": {"tr": "XML veya ZIP dosyası", "ru": "XML- или ZIP-файл", "zh": "XML 或 ZIP 文件"},
    "XP": {"tr": "XP", "ru": "Опыт", "zh": "经验"},
    "YOUR HIDEOUT · BUILD & ARRANGE": {"tr": "SAKLANMA YERİN · OLUŞTUR VE DÜZENLE", "ru": "ВАШЕ УБЕЖИЩЕ · СТРОЙТЕ И РАССТАВЛЯЙТЕ", "zh": "你的藏身处 · 建造与布置"},
    "anchor": {"tr": "çapa", "ru": "якорь", "zh": "锚点"},
    "objects": {"tr": "nesne", "ru": "объектов", "zh": "个物体"},
    "objects selected": {"tr": "nesne seçili", "ru": "объектов выбрано", "zh": "个物体已选择"},
    "off": {"tr": "kapalı", "ru": "выкл.", "zh": "关"},
    "pickups": {"tr": "toplama", "ru": "подборы", "zh": "个拾取物"},
    "profile “": {"tr": "profil “", "ru": "профиль “", "zh": "配置方案“"},
    "results": {"tr": "sonuç", "ru": "результатов", "zh": "个结果"},
    "ward": {"tr": "ward", "ru": "оберег", "zh": "守卫"},
    "” will be deleted. Game settings are unchanged.": {"tr": "” silinecek. Oyun ayarları değişmez.", "ru": "” будет удалён. Настройки игры не изменены.", "zh": "”将被删除。游戏设置不会改变。"},
    "All models": {"tr": "Tüm modeller", "ru": "Все модели", "zh": "全部模型"},
    "Back edges": {"tr": "Arka kenarlar", "ru": "Задние края", "zh": "后边缘"},
    "Front edges": {"tr": "Ön kenarlar", "ru": "Передние края", "zh": "前边缘"},
    "Ground": {"tr": "Zemin", "ru": "Земля", "zh": "地面"},
    "Left edges": {"tr": "Sol kenarlar", "ru": "Левые края", "zh": "左边缘"},
    "Model library": {"tr": "Model kütüphanesi", "ru": "Библиотека моделей", "zh": "模型库"},
    "Objects": {"tr": "Nesneler", "ru": "Объекты", "zh": "物体"},
    "Placed objects": {"tr": "Yerleştirilen nesneler", "ru": "Размещённые объекты", "zh": "已放置物体"},
    "Props": {"tr": "Aksesuarlar", "ru": "Реквизит", "zh": "道具"},
    "Right edges": {"tr": "Sağ kenarlar", "ru": "Правые края", "zh": "右边缘"},
    "X centers": {"tr": "X merkezleri", "ru": "Центры по X", "zh": "X 中心"},
    "Z centers": {"tr": "Z merkezleri", "ru": "Центры по Z", "zh": "Z 中心"},
    " Every style a font has (Bold, Light, Semibold, Italic …) is listed. The choice is undone when you choose Game font or close EpochPact.": {"tr": " Bir fontun her stili (Bold, Light, Semibold, Italic …) listelenir. Seçim, Oyun fontu'nu seçtiğinde ya da EpochPact'i kapattığında geri alınır.", "ru": " Перечислены все начертания шрифта (Bold, Light, Semibold, Italic …). Выбор отменяется, когда вы выбираете шрифт игры или закрываете EpochPact.", "zh": "列出字体的所有样式（Bold、Light、Semibold、Italic …）。选择“游戏字体”或关闭 EpochPact 时会撤销此选择。"},
    " updated.": {"tr": " güncellendi.", "ru": " обновлено.", "zh": "已更新。"},
    "Apply transform": {"tr": "Dönüşümü uygula", "ru": "Применить преобразование", "zh": "应用变换"},
    "Delete group": {"tr": "Grubu sil", "ru": "Удалить группу", "zh": "删除组"},
    "Delete object": {"tr": "Nesneyi sil", "ru": "Удалить объект", "zh": "删除物体"},
    "Enable Boss doors, visit the Monolith Hub, then enter your hideout.": {"tr": "Boss kapılarını aç, Monolith Hub'a git, sonra saklanma yerine gir.", "ru": "Включите двери боссов, посетите Monolith Hub, затем войдите в убежище.", "zh": "开启 Boss 门，前往 Monolith Hub，然后进入你的藏身处。"},
    "Hideout\nTravel to your personal sanctuary": {"tr": "Hideout\nKendi sığınağına git", "ru": "Убежище\nОтправиться в своё личное убежище", "zh": "藏身处\n前往你的专属庇护所"},
    "Loading your hideout…": {"tr": "Saklanma yerin yükleniyor…", "ru": "Загрузка убежища…", "zh": "正在加载你的藏身处…"},
    "Local layout · No character-save changes · Hover pictures to inspect models": {"tr": "Yerel düzen · Karakter kaydı değişmez · Modelleri incelemek için resimlerin üzerine gel", "ru": "Локальная планировка · Сохранение персонажа не меняется · Наведите на картинку, чтобы осмотреть модель", "zh": "本地布局 · 不更改角色存档 · 将鼠标悬停在图片上以查看模型"},
    "Lock": {"tr": "Kilitle", "ru": "Заблокировать", "zh": "锁定"},
    "Move": {"tr": "Taşı", "ru": "Переместить", "zh": "移动"},
    "Move group": {"tr": "Grubu taşı", "ru": "Переместить группу", "zh": "移动组"},
    "Move object": {"tr": "Nesneyi taşı", "ru": "Переместить объект", "zh": "移动物体"},
    "No models match these words and filter.": {"tr": "Bu kelimelere ve filtreye uyan model yok.", "ru": "Нет моделей, подходящих под эти слова и фильтр.", "zh": "没有符合这些关键词和筛选条件的模型。"},
    "No placed objects match this search.": {"tr": "Bu aramaya uyan yerleştirilmiş nesne yok.", "ru": "Нет размещённых объектов, подходящих под этот поиск.", "zh": "没有符合此搜索的已放置物体。"},
    "Restore": {"tr": "Geri getir", "ru": "Восстановить", "zh": "恢复"},
    "Restore object": {"tr": "Nesneyi geri getir", "ru": "Восстановить объект", "zh": "恢复物体"},
    "Set preview transform": {"tr": "Önizleme dönüşümünü ayarla", "ru": "Задать преобразование предпросмотра", "zh": "设置预览变换"},
    "Source · ": {"tr": "Kaynak · ", "ru": "Источник · ", "zh": "来源 · "},
    "Travelling to Hideout…": {"tr": "Saklanma yerine gidiliyor…", "ru": "Переход в убежище…", "zh": "正在前往藏身处…"},
    "Unlock": {"tr": "Kilidi aç", "ru": "Разблокировать", "zh": "解锁"},
    "Unlock group": {"tr": "Grubun kilidini aç", "ru": "Разблокировать группу", "zh": "解锁组"},
    "Values for ": {"tr": "Değerler: ", "ru": "Значения для ", "zh": "数值："},
    "You are in your Hideout": {"tr": "Saklanma yerindesin", "ru": "Вы в своём убежище", "zh": "你在你的藏身处中"},
    "✎  Edit Hideout": {"tr": "✎  Saklanma yerini düzenle", "ru": "✎  Редактировать убежище", "zh": "✎  编辑藏身处"},
    "✎  Resume editing": {"tr": "✎  Düzenlemeye devam et", "ru": "✎  Продолжить редактирование", "zh": "✎  继续编辑"},
}


# ---------------------------------------------------------------------------------------- colours
def rgba(hex_colour: str) -> tuple[int, int, int, int]:
    h = hex_colour.lstrip("#")
    if len(h) == 6:
        h += "ff"
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4, 6))  # type: ignore[return-value]


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(4))


def premultiplied_lerp(a, b, t):
    """CSS gradients interpolate in premultiplied alpha."""
    pa = [a[i] * a[3] / 255 for i in range(3)] + [a[3]]
    pb = [b[i] * b[3] / 255 for i in range(3)] + [b[3]]
    p = [pa[i] + (pb[i] - pa[i]) * t for i in range(4)]
    alpha = p[3]
    if alpha <= 0:
        return (0, 0, 0, 0)
    return (round(p[0] * 255 / alpha), round(p[1] * 255 / alpha), round(p[2] * 255 / alpha), round(alpha))


def stops_colour(stops, t):
    """stops: [(position 0..1, rgba)] as in a CSS gradient."""
    if t <= stops[0][0]:
        return stops[0][1]
    for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
        if t <= p1:
            return premultiplied_lerp(c0, c1, 0 if p1 == p0 else (t - p0) / (p1 - p0))
    return stops[-1][1]


def linear_gradient(width, height, angle_deg, stops):
    """CSS linear-gradient(angle, stops) over a width x height box."""
    angle = math.radians(angle_deg)
    dx, dy = math.sin(angle), -math.cos(angle)
    length = abs(width * dx) + abs(height * dy)
    image = Image.new("RGBA", (width, height))
    pixels = image.load()
    cache = {}
    for y in range(height):
        for x in range(width):
            t = ((x + 0.5 - width / 2) * dx + (y + 0.5 - height / 2) * dy) / length + 0.5
            key = round(t * 1024)
            colour = cache.get(key)
            if colour is None:
                colour = cache[key] = stops_colour(stops, key / 1024)
            pixels[x, y] = colour
    return image


def radial_gradient(width, height, cx, cy, rx, ry, stops):
    image = Image.new("RGBA", (width, height))
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            d = math.hypot((x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry)
            pixels[x, y] = stops_colour(stops, d)
    return image


def ellipse_mask(width, height, solid, clear):
    """mask-image: radial-gradient(ellipse, black solid, transparent clear) (farthest-corner)."""
    rx, ry = width / 2 * math.sqrt(2), height / 2 * math.sqrt(2)
    mask = Image.new("L", (width, height))
    pixels = mask.load()
    for y in range(height):
        for x in range(width):
            d = math.hypot((x + 0.5 - width / 2) / rx, (y + 0.5 - height / 2) / ry)
            pixels[x, y] = 255 if d <= solid else 0 if d >= clear else round(255 * (clear - d) / (clear - solid))
    return mask


def solid(width, height, colour):
    return Image.new("RGBA", (width, height), rgba(colour) if isinstance(colour, str) else colour)


# ------------------------------------------------------------------------------------ sprites
def shape(size_css, draw, scale=S):
    """Draw at SS times the export size, then reduce: smooth edges at any radius."""
    w, h = size_css
    big = Image.new("RGBA", (w * scale * SS, h * scale * SS), (0, 0, 0, 0))
    draw(ImageDraw.Draw(big), scale * SS)
    return big.resize((w * scale, h * scale), Image.LANCZOS)


def frame_sprite(radius, stroke=1.0, fill=False):
    """White rounded frame (or fill), 9-sliced at runtime. Returns (image, slice in css px)."""
    edge = int(max(radius, math.ceil(stroke))) + 2
    side = edge * 2 + 2

    def draw(d, k):
        box = [0, 0, side * k - 1, side * k - 1]
        if fill:
            d.rounded_rectangle(box, radius=radius * k, fill=(255, 255, 255, 255))
        else:
            d.rounded_rectangle(box, radius=radius * k, outline=(255, 255, 255, 255), width=max(1, round(stroke * k)))
    return shape((side, side), draw), edge


def band_sprite(width):
    """box-shadow: inset 0 0 0 <width>px: a white band inside the edge."""
    side = width * 2 + 2

    def draw(d, k):
        d.rectangle([0, 0, side * k - 1, side * k - 1], fill=(255, 255, 255, 255))
        d.rectangle([width * k, width * k, (side - width) * k - 1, (side - width) * k - 1], fill=(0, 0, 0, 0))
    return shape((side, side), draw), width


def corner_sprite():
    """.panel::before: a 13px corner of 1px gold lines (top-left; the other corner is rotated)."""
    def draw(d, k):
        d.rectangle([0, 0, 13 * k - 1, k - 1], fill=(255, 255, 255, 255))
        d.rectangle([0, 0, k - 1, 13 * k - 1], fill=(255, 255, 255, 255))
    return shape((13, 13), draw)


def circle_sprite(diameter, ring=False):
    def draw(d, k):
        box = [0, 0, diameter * k - 1, diameter * k - 1]
        if ring:
            d.ellipse(box, outline=(255, 255, 255, 255), width=k)
        else:
            d.ellipse(box, fill=(255, 255, 255, 255))
    return shape((diameter, diameter), draw)


def track_sprite():
    """input[type=range]: 8px tall, 1px gold border, radius 8, linear-gradient(90deg, glow, #343a35).
    Made 256 css px wide and stretched to each slider's width (a smooth horizontal gradient)."""
    width, height = 256, 8
    fill = linear_gradient(width * S, height * S, 90, [(0, rgba(THEME["glow"])), (1, rgba("#343a35"))])
    mask = shape((width, height), lambda d, k: d.rounded_rectangle([0, 0, width * k - 1, height * k - 1], radius=height / 2 * k, fill=(255, 255, 255, 255)))
    border = shape((width, height), lambda d, k: d.rounded_rectangle([0, 0, width * k - 1, height * k - 1], radius=height / 2 * k,
                                                                    outline=rgba(THEME["gold"]), width=k))
    image = Image.new("RGBA", fill.size)
    image.paste(fill, (0, 0), mask.split()[3])
    image.alpha_composite(border)
    return image


def thumb_sprite():
    """::-webkit-slider-thumb: 19px, rotate(45deg), accent fill, 3px double #775329 border,
    radius 3, box-shadow 0 0 8px glow. The image is 44 css px square, centred on the thumb."""
    canvas = 44
    k = S * SS
    big = Image.new("RGBA", (canvas * k, canvas * k), (0, 0, 0, 0))
    square = Image.new("RGBA", (19 * k, 19 * k), (0, 0, 0, 0))
    d = ImageDraw.Draw(square)
    d.rounded_rectangle([0, 0, 19 * k - 1, 19 * k - 1], radius=3 * k, fill=rgba("#775329"))           # outer line
    d.rounded_rectangle([k, k, 18 * k - 1, 18 * k - 1], radius=2 * k, fill=rgba(THEME["accent"]))     # gap shows the background
    d.rounded_rectangle([2 * k, 2 * k, 17 * k - 1, 17 * k - 1], radius=1 * k, fill=rgba("#775329"))  # inner line
    d.rounded_rectangle([3 * k, 3 * k, 16 * k - 1, 16 * k - 1], radius=1 * k, fill=rgba(THEME["accent"]))
    # The double border's gap is the element's background colour (the accent): as Chromium draws it.
    rotated = square.rotate(45, resample=Image.BICUBIC, expand=True)
    glow = Image.new("RGBA", rotated.size, rgba(THEME["glow"]))
    glow.putalpha(rotated.split()[3])
    glow_canvas = Image.new("RGBA", big.size, (0, 0, 0, 0))
    offset = ((big.width - rotated.width) // 2, (big.height - rotated.height) // 2)
    glow_canvas.paste(glow, offset, glow)
    glow_canvas = glow_canvas.filter(ImageFilter.GaussianBlur(4 * k))   # 8px blur radius = sigma 4
    big.alpha_composite(glow_canvas)
    big.alpha_composite(rotated, offset)
    return big.resize((canvas * S, canvas * S), Image.LANCZOS)


# ------------------------------------------------------------------------------------ artwork
def reference_crop(image, scaled_size, position, size, solid_at, clear_at):
    """A sprite cut from the reference sheet the way style.css does with background-size and
    -position, faded at the edges by its radial mask-image."""
    big = image.resize((scaled_size[0] * S, scaled_size[1] * S), Image.LANCZOS)
    x, y = -position[0] * S, -position[1] * S
    crop = big.crop((x, y, x + size[0] * S, y + size[1] * S)).convert("RGBA")
    crop.putalpha(ImageChops.multiply(crop.split()[3], ellipse_mask(size[0] * S, size[1] * S, solid_at, clear_at)))
    return crop


def cover(art, width, height, y_position):
    """background-size: cover; background-position: center <y_position>."""
    scale = max(width / art.width, height / art.height)
    w, h = math.ceil(art.width * scale), math.ceil(art.height * scale)
    resized = art.resize((w, h), Image.LANCZOS)
    left, top = round((width - w) * 0.5), round((height - h) * y_position)
    out = Image.new("RGB", (width, height))
    out.paste(resized, (left, top))
    return out.convert("RGBA")


def hero_image(art, height_css):
    """.hero (#07100d) under .hero-art: gradient over the art, the whole layer at opacity .85.
    .hero-art fills the hero inside its 1px bottom border: 225px, or 155px on a page with tabs."""
    width, height = 1185 * S, height_css * S   # the main area less its 15px scrollbar
    layer = cover(art, width, height, 0.48)
    layer.alpha_composite(linear_gradient(width, height, 90, [(0.05, rgba("#08120fff")), (0.38, rgba("#08120fca")), (0.72, (8, 18, 15, 0))]))
    base = solid(width, height, "#07100d")
    return Image.blend(base, layer, 0.85).convert("RGB")


def sidebar_image(art):
    """.sidebar: radial-gradient(at bottom) over linear-gradient(#080f0df5,#081410e8) over the art
    (background-size: auto 100%, at the top left)."""
    width, height = 238 * S, 898 * S
    scale = height / art.height
    layer = Image.new("RGBA", (width, height))
    layer.paste(art.resize((math.ceil(art.width * scale), height), Image.LANCZOS).convert("RGBA"), (0, 0))
    layer.alpha_composite(linear_gradient(width, height, 180, [(0, rgba("#080f0df5")), (1, rgba("#081410e8"))]))
    rx, ry = 119 * S * math.sqrt(2), 898 * S * math.sqrt(2)
    layer.alpha_composite(radial_gradient(width // 4, height // 4, width / 8, height / 4, rx / 4, ry / 4,
                                          [(0, rgba("#1a45332b")), (1, (26, 69, 51, 0))]).resize((width, height), Image.BICUBIC))
    return layer.convert("RGB")


def main_background():
    """.main-scroll: radial-gradient(ellipse at 70% 0%, soft, transparent 50%) over the page colour.
    Smooth, so it is made at a quarter size and stretched."""
    width, height = 300, 187       # 1200 x 748 / 4
    base = solid(width, height, THEME["bg"])
    soft = rgba(THEME["soft"])
    base.alpha_composite(radial_gradient(width, height, 0.7 * width, 0, 0.7 * width * math.sqrt(2), height * math.sqrt(2),
                                         [(0, soft), (0.5, soft[:3] + (0,))]))
    return base.convert("RGB")


def density_icon(source):
    """.icon.density: the density artwork, contain, centred in 55x61, no mask."""
    size = (55 * S, 61 * S)
    image = Image.new("RGBA", size, (0, 0, 0, 0))
    side = 55 * S
    image.paste(source.resize((side, side), Image.LANCZOS), (0, (size[1] - side) // 2))
    return image


# ------------------------------------------------------------------------------------- fonts
def inter_instance(weight):
    """A static Inter of one weight (opsz 14, the text sizes used), limited to the scripts the
    interface shows: Latin with Turkish, Cyrillic, punctuation, arrows and the maths signs."""
    from fontTools.ttLib import TTFont
    from fontTools.varLib import instancer
    from fontTools import subset
    font = instancer.instantiateVariableFont(TTFont(ASSETS / "inter.ttf"), {"wght": weight, "opsz": 14})
    ranges = [(0x20, 0x7E), (0xA0, 0x17F), (0x400, 0x4FF), (0x2000, 0x206F), (0x20A0, 0x20BF), (0x2190, 0x21FF), (0x2200, 0x22FF),
              (0x2300, 0x23FF), (0x25A0, 0x25FF), (0x2600, 0x26FF), (0x2700, 0x27BF)]  # symbols Inter has, as the browser uses them
    options = subset.Options()
    options.layout_features = ["kern", "liga", "calt", "tnum"]
    options.name_IDs = ["*"]
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=[c for low, high in ranges for c in range(low, high + 1)])
    subsetter.subset(font)
    buffer = io.BytesIO()
    font.save(buffer)
    return buffer.getvalue()


def text_advances(faces):
    """Each character's advance in em for the faces the window draws (Inter as embedded, Georgia
    from Windows as the game loads it): the window lays out table columns from text widths as the
    browser's automatic table layout does, without asking the game to measure every cell."""
    from fontTools.ttLib import TTFont
    out = {}
    sources = dict(faces)
    windows = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts"
    for face, file in (("serif", "georgia.ttf"), ("serifBold", "georgiab.ttf")):
        if (windows / file).is_file():
            sources[face] = (windows / file).read_bytes()
    for face, font_bytes in sources.items():
        font = TTFont(io.BytesIO(font_bytes))
        em, metrics = font["head"].unitsPerEm, font["hmtx"]
        out[face] = {str(cp): round(metrics[glyph][0] / em, 4) for cp, glyph in font.getBestCmap().items() if cp < 0x3000}
    return out


# -------------------------------------------------------------------------------------- data
def js_number(value):
    """String(value) as JavaScript writes it."""
    if value is None:
        return None
    if isinstance(value, bool):
        return "true" if value else "false"
    if float(value).is_integer():
        return str(int(value))
    return repr(float(value))


def unit_for(c):
    i = c["id"]
    if i in ("weaversdrop", "uniqueroll"):
        return "%"
    if i in ("pickup_radius", "qol_arearadius"):
        return " game units"
    if i == "pickup_interval":
        return " s"
    if i.startswith("map_") and i.endswith("opacity"):
        return ""
    if i.startswith("map_"):
        return "×"
    if i == "build_cameraangle":
        return "°"
    if i.startswith("cof_double_"):
        return "%"
    if i == "loot_lp":
        return " LP"
    if i in NO_UNIT or c["widget"] == "toggle":
        return ""
    return "×"


def icon_for(control_id):
    if control_id == "speed":
        return "speed"
    if control_id == "gold":
        return "gold"
    if control_id in ("drops", "rarity"):
        return "drops"
    if control_id == "density":
        return "density"
    if control_id in ("cooldown", "autopickup"):
        return "cooldown"
    return "default"


def source_for(c):
    if c["id"] in EXTRA_SOURCES:
        read, path = EXTRA_SOURCES[c["id"]]
        return {"read": read, "path": path}
    source = c.get("currentSource") or ""
    if source.startswith("currentValues."):
        return {"read": "status", "path": source.split(".", 1)[1]}
    if source.startswith("liveState."):
        read, _, path = source.split(".", 1)[1].partition(".")
        if read in JSON_READS and "[" not in path:
            return {"read": read, "path": path}
    return None


def automatic(page, c):
    """automaticSetting() of app.js for a control shown on `page`."""
    template = c.get("commandTemplate") or ""
    return bool(((page in ("general", "overview") and c["group"] == "general") or template.startswith(AUTOMATIC_TEMPLATES))
                and c["widget"] in ("number", "toggle") and c["lifetime"] in ("session", "actor"))


def control_entry(c, english):
    label, description = english.get(c["id"], (c["id"], ""))
    value = next((p for p in c.get("parameters", []) if p["name"] == "value"), {})
    entry = {"id": c["id"], "widget": "choice" if value.get("choices") else c["widget"], "label": label, "description": description,
             "lifetime": c["lifetime"], "command": c.get("commandTemplate"), "reset": c.get("resetCommand"),
             "default": c.get("default"), "unit": unit_for(c), "icon": icon_for(c["id"])}
    if c["widget"] == "number":
        entry.update(min=value.get("minimum"), max=value.get("maximum"), integer=value.get("type") == "integer")
    if value.get("choices"):
        entry["choices"] = value["choices"]
        entry["choiceLabels"] = value.get("choiceLabels") or [("Normal game choices" if x == 0 else f"{x} choices") for x in value["choices"]]
    if c["widget"] == "toggle":
        entry["defaultText"] = "Default: on" if c.get("default") else "Default: off"
    elif c["widget"] == "number" and not value.get("choices"):
        default = js_number(c.get("default")) if c.get("default") is not None else "rank bonus"
        entry["defaultText"] = f"{js_number(value.get('minimum'))}–{js_number(value.get('maximum'))}{entry['unit']} · Default: {default}"
    source = source_for(c)
    if source:
        entry["source"] = source
    entry["params"] = [{k: p[k] for k in ("name", "type", "minimum", "maximum", "choices", "choiceLabels") if k in p}
                       for p in c.get("parameters", [])]
    return entry


def inter_cmap():
    """The characters Inter (ui/assets/inter.ttf, the desktop's font) has."""
    if not hasattr(inter_cmap, "cache"):
        from fontTools.ttLib import TTFont
        inter_cmap.cache = set(TTFont(ASSETS / "inter.ttf").getBestCmap())
    return inter_cmap.cache


def build_data():
    catalog = json.loads((UI / "catalog.json").read_text(encoding="utf-8"))
    english = json.loads((UI / "locale-en.json").read_text(encoding="utf-8"))["controls"]
    controls = catalog["controls"]
    by_id = {c["id"]: c for c in controls}
    pages, used = [], {}
    for page_id, nav, symbol, title, subtitle in ingame_pages.PAGES:
        sections = ingame_pages.sections(page_id, controls, catalog["counts"])
        panel_names = {"recent_pickups": ("loot-log", "Recent pickups"), "collect_now": ("", "Collect now"),
                       "pickup_rules": ("", "Pickup rules"), "pickup_selections": ("", "Pickup selections"),
                       "filter_files": ("filter-files", "Loot filter files"), "stats_filters": ("", "Character bonuses"),
                       "skill_budgets": ("", "Skill and point budgets"), "free_respec": ("", "Free respec"),
                       "summon_allies": ("", "Live summoned allies"), "damage_meter": ("damage-meter", "Damage meter"),
                       "minion_bonuses": ("", "Minion and companion bonuses"),
                       "inventory_shortcuts": ("", "Inventory pages"), "stash_rules": ("", "Stash tabs from loot filter"),
                       "wardrobe": ("wardrobe", "Wardrobe"),
                       "campaign": ("", "Campaign quests"), "mastery": ("", "Mastery"), "monolith_management": ("", "Monolith management"),
                       "collection_head": ("collection", "Unique Atlas" if page_id == "atlas" else "Stash Assistant"),
                       "echo_head": ("navigator", "Monolith Navigator"), "ws_head": ("custom-workshop", "Create custom item"),
                       "stash_search_help": ("", "In-game stash search"), "craft_materials": ("", "Crafting materials"),
                       "glyph_chances": ("", "Glyph chances"), "factions": ("", "Faction data")}
        group_parts = {}
        for section in sections:
            section_title, identifier = section.get("title", ""), ""
            if section.get("native") in panel_names:
                identifier, section_title = panel_names[section["native"]]
            if section.get("group") and section["group"] in group_parts:
                section.update(group_parts[section["group"]]); continue
            if section_title and page_id != "settings":
                part = {"partKey": "panel:" + (identifier or page_id + ":" + section_title), "partTitle": section_title}
                section.update(part)
                if section.get("group"): group_parts[section["group"]] = part
        sections.append(ingame_pages.native("global_search"))
        for section in sections:
            if section["kind"] == "native":
                for i in section.get("controls", []):
                    used[i] = by_id[i]
            if section["kind"] != "settings":
                continue
            section["automatic"] = [automatic(page_id, by_id[i]) for i in section["controls"]]
            for i in section["controls"] + section.get("tailControls", []):
                used[i] = by_id[i]
        # the face the browser draws the navigation symbol with: Inter when Inter has it, else the
        # system fallback (Segoe UI Symbol)
        face = "body" if all(ord(ch) in inter_cmap() for ch in symbol) else "symbol"
        # a page with tabs: its first tab, the tab whose drafts Apply changes sends, its hero
        pages.append({**ingame_pages.PAGE_EXTRA.get(page_id, {}), "id": page_id, "nav": nav, "symbol": symbol, "symbolFace": face, "title": title, "subtitle": subtitle,
                      "sections": sections})
    data = {"version": VERSION, "reference": [1440, 900], "theme": THEME, "lifetimes": LIFETIME, "pages": pages,
            "controls": {i: control_entry(c, english) for i, c in used.items()}}
    data["counts"] = catalog["counts"]
    data["searchControls"] = [dict(control_entry(c, english), group=c["group"]) for c in controls
                              if c["id"] not in ingame_pages.DEVELOPER_CONTROLS]
    # Profiles may contain a session control whose page has not been visited yet.
    for c in controls:
        if c["lifetime"] in ("session", "actor") and c["widget"] in ("number", "toggle"):
            data["controls"].setdefault(c["id"], control_entry(c, english))
    data["campaignQuests"] = catalog["optionCatalogs"]["campaignQuests"]  # the static coverage table of Campaign
    data["lensCatalog"] = catalog["optionCatalogs"]["lenses"]  # the Circle of Fortune lens catalog before a live read
    # the names the Custom Items workshop builds its extra effects' names from (workshopModifierEntries)
    stats = catalog["characterStats"]
    data["characterStats"] = {k: [{"id": x["id"], "name": x["name"]} for x in stats[k]] for k in ("properties", "tags", "ailments")}
    # Browser names, keys, units and bounds come from the same stat-model.js the app uses.
    result = subprocess.run(["node", str(ROOT / "tools/ingame_stat_catalog.js")], capture_output=True, check=True)
    data["statBrowser"] = json.loads(result.stdout.decode("utf-8"))
    data["appVersion"] = re.search(r'#define EP_VERSION_STR "([^"]+)"', (ROOT / "native/version_info.h").read_text(encoding="utf-8"))[1]
    data["i18n"] = translations(data)
    # every character the window's texts use in each language: the fonts' atlases are filled with
    # them in the background, so no text waits for a new glyph later
    english = set("".join(texts_of(data))) | {chr(c) for c in range(0x20, 0x7F)}
    # the app's dictionaries as written, for the texts the window composes while it runs
    # (native/core/ingame_i18n.cpp translates them the way ui/i18n.js does)
    data["dictionaries"] = {language: json.loads((UI / f"i18n-{language}.json").read_text(encoding="utf-8")) for language in ("tr", "ru", "zh")}
    data["charset"] = {"en": "".join(sorted(c for c in english if c >= " "))}
    for language in ("tr", "ru", "zh"):
        # composed texts too: a Chinese character only the dictionary has would otherwise be drawn late
        words = english | set("".join(data["i18n"][language].values())) | set("".join(data["dictionaries"][language].values()))
        data["charset"][language] = "".join(sorted(c for c in words if c >= " "))
    return data


def texts_of(data):
    """Every text the in-game window shows, as the desktop app shows it (whole text nodes)."""
    texts = set(LIFETIME.values())
    texts |= {"LAST EPOCH · OFFLINE TOOLS", "Rewrite the past.", "Shape your journey.", "Created by Falor", "Support · Discord",
              "Customize navigation", "↻ Refresh", "Refresh", "Find a setting…", "CHRONOFORGE", "Checking connection", "● Offline · InGame",
              "● InGame", "Changes apply automatically", "Use Apply changes below", "Default", "On", "Off", "Applying…", "Applied", "Draft",
              "Ready", "A setting needs attention", "Applying changes…", "{0} settings in draft", "Save actions use their own buttons.",
              "Apply changes", "Discard draft", "Rank", "⚙−☠▶Ⅱ↺⌃⌄◢×"}
    by_id = {p["id"]: p for p in data["pages"]}
    for page in data["pages"]:
        texts |= {page["nav"], page["title"], page["subtitle"]}  # the breadcrumb is "EpochPact / " + the translated nav
        for section in page["sections"]:
            texts |= {v for k, v in section.items() if k in ("title", "text", "note") and isinstance(v, str)}
            if section.get("details"):
                texts.add(section["details"]["summary"])
            for item in section.get("items", []):
                texts.add(item[1])
            for target in section.get("pages", []):
                texts.add(f"{by_id[target]['symbol']} {by_id[target]['nav']}")
            for override in section.get("overrides", {}).values():
                texts |= set(override.values())
    for c in data["controls"].values():
        texts |= {c["label"], c["description"], c["unit"].strip(), c.get("defaultText", ""), *c.get("choiceLabels", [])}
    texts |= native_texts()
    texts |= {k for k in OWN_TEXTS}
    for rows in data.get("statBrowser", {}).values():
        for row in rows:
            texts |= {row["name"], row["category"], *row["shared"]}
    return sorted(t for t in texts if t)


def native_texts():
    """The English texts the native window code passes to Tr() / Fmt(): a literal argument, or
    both literal branches of a ?: argument."""
    import codecs
    found = set()

    literal = r'"(?:[^"\\]|\\.)*"'

    def literals(code):
        return [codecs.escape_decode(m[1:-1].encode("utf-8"))[0] for m in re.findall(literal, code)]

    for source in sorted((ROOT / "native" / "core").glob("ingame_*.cpp")):
        code = source.read_text(encoding="utf-8")
        for start in (m.end() for m in re.finditer(r"\b(?:Tr|Fmt)\(", code)):
            # the first argument: up to the matching ")" or a top-level ","
            depth, i, quote = 0, start, False
            while i < len(code):
                ch = code[i]
                if quote:
                    if ch == "\\":
                        i += 1
                    elif ch == '"':
                        quote = False
                elif ch == '"':
                    quote = True
                elif ch in "([{":
                    depth += 1
                elif ch in ")]}":
                    if depth == 0:
                        break
                    depth -= 1
                elif ch == "," and depth == 0:
                    break
                i += 1
            argument = code[start:i].strip()
            if re.fullmatch(r"(?:" + literal + r"\s*)+", argument):
                found.add(b"".join(literals(argument)).decode("utf-8"))
            else:
                ternary = re.search(r"\?\s*(" + literal + r")\s*:\s*(" + literal + r")", argument)
                if ternary:
                    for branch in ternary.groups():
                        found.add(literals(branch)[0].decode("utf-8"))
    return found


def translations(data):
    """{language: {English: translation}} from ui/i18n.js run over ui/i18n-<language>.json (Node)."""
    texts = texts_of(data)
    request = json.dumps({"tr": texts, "ru": texts, "zh": texts}, ensure_ascii=False).encode("utf-8")
    result = subprocess.run(["node", str(ROOT / "tools" / "ingame_translate.js")], input=request, capture_output=True, check=True)
    out = json.loads(result.stdout.decode("utf-8"))
    for language in ("tr", "ru", "zh"):
        # a text the dictionary has word for word (a template too) is its entry, as the app writes it
        words = json.loads((UI / f"i18n-{language}.json").read_text(encoding="utf-8"))
        out[language].update({t: words[t] for t in texts if words.get(t) and words[t] != t})
        out[language].update({t: v[language] for t, v in OWN_TEXTS.items()})
    return out


# -------------------------------------------------------------------------------------- build
def build(out: Path):
    out.mkdir(parents=True, exist_ok=True)
    files = {}

    def save(name, image, kind="PNG", **options):
        path = out / name
        buffer = io.BytesIO()
        image.save(buffer, kind, **options)
        path.write_bytes(buffer.getvalue())
        files[name] = path

    reference = Image.open(ASSETS / "chronoforge-reference.png").convert("RGB")
    art = Image.open(ASSETS / "chronoforge-art.png").convert("RGB")
    save("crest.png", reference_crop(reference, (1071, 670), (-37, -16), (135, 120), 0.52, 0.75), optimize=True)
    for name, position in {"default": (-162, -190), "speed": (-163, -260), "gold": (-436, -263), "drops": (-436, -189), "cooldown": (-14, -9)}.items():
        save(f"icon-{name}.png", reference_crop(reference, (714, 446), position, (55, 61), 0.60, 0.77), optimize=True)
    save("icon-density.png", density_icon(Image.open(ASSETS / "monster-density-256.png").convert("RGBA")), optimize=True)
    save("hero.jpg", hero_image(art, 225), "JPEG", quality=92, optimize=True)
    save("hero-compact.jpg", hero_image(art, 155), "JPEG", quality=92, optimize=True)
    save("sidebar.jpg", sidebar_image(art), "JPEG", quality=92, optimize=True)
    save("main.png", main_background(), optimize=True)
    slices = {}
    for name, (radius, fill) in {"frame-r0": (0, False), "frame-r2": (2, False), "frame-r3": (3, False), "frame-r4": (4, False),
                                 "frame-r5": (5, False), "frame-r6": (6, False), "fill-r5": (5, True), "fill-r6": (6, True),
                                 "frame-r12": (12, False), "fill-r2": (2, True), "fill-r3": (3, True), "fill-r4": (4, True),
                                 "fill-r12": (12, True)}.items():
        image, edge = frame_sprite(radius, fill=fill)
        save(f"{name}.png", image, optimize=True)
        slices[name] = edge
    for width in (2, 4):
        image, edge = band_sprite(width)
        save(f"band-{width}.png", image, optimize=True)
        slices[f"band-{width}"] = edge
    save("corner.png", corner_sprite(), optimize=True)
    save("ring-34.png", circle_sprite(34, ring=True), optimize=True)
    save("ring-30.png", circle_sprite(30, ring=True), optimize=True)  # .craft-step h3>span
    save("circle-17.png", circle_sprite(17), optimize=True)
    save("track.png", track_sprite(), optimize=True)
    save("thumb.png", thumb_sprite(), optimize=True)
    save("grad-panel.png", linear_gradient(128 * S, 128 * S, 135, [(0, rgba("#25362e33")), (0.5, (37, 54, 46, 0))]), optimize=True)
    save("grad-card.png", linear_gradient(128 * S, 128 * S, 135, [(0, rgba("#24402c24")), (1, (36, 64, 44, 0))]), optimize=True)
    save("grad-stat-value.png", linear_gradient(128 * S, 128 * S, 120, [(0, rgba(THEME["soft"])), (0.8, (0, 0, 0, 0))]), optimize=True)
    save("grad-browser.png", linear_gradient(128 * S, 128 * S, 135, [(0, rgba(THEME["soft"])), (1, rgba("#00000022"))]), optimize=True)
    save("grad-button.png", linear_gradient(64 * S, 32 * S, 160, [(0, rgba("#29302c55")), (1, rgba("#070d0cb8"))]), optimize=True)
    save("grad-primary.png", linear_gradient(128 * S, 32 * S, 145, [(0, rgba(THEME["soft"])), (1, rgba("#07160fe0"))]), optimize=True)
    soft = rgba(THEME["soft"])
    save("grad-nav.png", linear_gradient(128 * S, 8, 90, [(0, soft), (1, rgba("#12241b22"))]), optimize=True)
    # .dm-ico: the damage types' icons (ui/assets/damage-types.png, 7 frames), one image each
    damage_types = Image.open(ASSETS / "damage-types.png").convert("RGBA")
    frame = damage_types.height
    for i in range(7):
        save(f"dmg-type-{i}.png", damage_types.crop((i * frame, 0, (i + 1) * frame, frame)).resize((36, 36), Image.LANCZOS), optimize=True)
    faces = {}
    for weight, name, face in ((400, "inter-regular.ttf", "body"), (600, "inter-semibold.ttf", "semi"), (700, "inter-bold.ttf", "bold")):
        font = inter_instance(weight)
        (out / name).write_bytes(font)
        files[name] = out / name
        faces[face] = font

    # what the hideout can place, as tools/build_hideout_catalog.py made it
    (out / "hideout-catalog.tsv").write_bytes((ROOT / "ui/hideout-catalog.tsv").read_bytes())
    files["hideout-catalog.tsv"] = out / "hideout-catalog.tsv"
    (out / "hideout-designs.json").write_bytes((ROOT / "ui/hideout-designs.json").read_bytes())
    files["hideout-designs.json"] = out / "hideout-designs.json"

    data = build_data()
    data["advances"] = text_advances(faces)
    data["slices"] = slices
    data["files"] = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in sorted(files.items())}
    (out / "ui.json").write_text(json.dumps(data, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    files["ui.json"] = out / "ui.json"

    # RCDATA named by file: IMG_HERO_JPG etc. The core looks the names up; ui.json lists them.
    lines = ["// Generated by tools/build_ingame_assets.py. Do not edit."]
    for name, path in sorted(files.items()):
        lines.append(f'{resource_name(name)} RCDATA "{str(path.resolve()).replace(chr(92), chr(92) * 2)}"')
    (out / "ingame.rc").write_text("\n".join(lines) + "\n", encoding="ascii")
    total = sum(p.stat().st_size for p in files.values())
    print(f"in-game assets: {len(files)} files, {total / 1024:.0f} KiB -> {out}")
    return data


def resource_name(file_name):
    return "EP_" + "".join(ch if ch.isalnum() else "_" for ch in file_name).upper()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "native/build/ingame")
    build(parser.parse_args().out.resolve())
