"""English presentation for messages from the unchanged progression backends.

Translate only message fields. Character names, paths, commands and payloads
remain untouched, including when they contain non-English characters.
"""
from __future__ import annotations

import re

BACKEND_MESSAGES = {
    "Oyun komuta yanıt vermedi.": "The game did not respond.",
    "Karakter bilgisi okunamadı.": "Could not read character information.",
    "Geçersiz işlem veya karakter kimliği.": "Invalid action or character identity.",
    "Kayıt nesnesi bekleniyor.": "Expected a save object.",
    "Yedek bir sembolik bağlantı olamaz.": "The backup cannot be a symbolic link.",
    "EpochPact görev yedeği klasörü bekleniyor.": "Choose an EpochPact quest backup folder.",
    "Canlı yedek dosyası eksik veya bağlantı içeriyor.": "The live backup file is missing or contains a link.",
    "Yedek biçimi tanınmıyor.": "The backup format is not recognized.",
    "Yedekteki karakter kimliği geçersiz.": "The character identity in the backup is invalid.",
    "Yedekteki kasa kimliği geçersiz.": "The stash identity in the backup is invalid.",
    "Yedek başka bir kayıt klasörüne ait.": "The backup belongs to another save folder.",
    "Canlı karakter yedeği manifest ile uyuşmuyor.": "The live character backup does not match its manifest.",
    "Canlı kasa yedeği manifest ile uyuşmuyor.": "The live stash backup does not match its manifest.",
    "Kayıt yedeği eksik.": "The save backup is missing.",
    "Yedekte beklenmeyen dosya bulundu.": "An unexpected file was found in the backup.",
    "Asıl karakter kayıt dosyası yedekte yok.": "The original character save file is missing from the backup.",
    "Geri alma için oyun kapalı olmalı.": "Close the game before restoring a backup.",
    "Mevcut kayıtların kurtarma yedeği alınamadı.": "Could not create a recovery backup of the current saves.",
    "Geri alınacak işlemin karakteri şu anda yüklü değil.": "The character belonging to this operation is not currently loaded.",
    "Oyun temizce kapatılamadı; kayıtlar değiştirilmedi.": "The game could not close normally; saves were not changed.",
    "Aktif kayıt bir bağlantı olamaz.": "The active save cannot be a link.",
    "CoF bilgisi okunamadı.": "Could not read Circle of Fortune information.",
    "Geçersiz karakter kimliği.": "Invalid character identity.",
    "Faction değişimi için bool bekleniyor.": "The faction switch option must be true or false.",
    "Önizleme için bool bekleniyor.": "The preview option must be true or false.",
    "Kazanç çarpanı 1–100 arasında olmalı.": "The gain multiplier must be between 1 and 100.",
    "Prophecy ödül çarpanı 1–25 arasında olmalı.": "The prophecy reward multiplier must be between 1 and 25.",
    "Çift drop kaynağı enemy veya echo olmalı.": "The double-item source must be enemy or echo.",
    "Lens celerity, charity veya duplication olmalı.": "Choose the celerity, charity or duplication lens.",
    "Çift drop ihtimali yüzde 0–100 arasında olmalı.": "The double-item chance must be between 0 and 100 percent.",
    "Monolith bilgisi okunamadı.": "Could not read Monolith information.",
    "Geçersiz timeline veya zorluk.": "Invalid timeline or difficulty.",
    "Stability çarpanı 1–100 arasında olmalı.": "The Stability multiplier must be between 1 and 100.",
    "Değer pozitif bir tam sayı veya sıfır olmalı.": "Enter a positive whole number or zero.",
}


def english_message(message):
    if not isinstance(message, str):
        return message
    if message in BACKEND_MESSAGES:
        return BACKEND_MESSAGES[message]
    if message.startswith("Geçersiz kayıt: "):
        return "Invalid save: " + message.removeprefix("Geçersiz kayıt: ")
    bounds = re.fullmatch(r"Tam sayı (.+)–(.+) arasında olmalı\.", message)
    if bounds:
        return f"Enter a whole number between {bounds[1]} and {bounds[2]}."
    return message


def english_exception(exc):
    if isinstance(exc, OSError):
        reason = ("File not found" if isinstance(exc, FileNotFoundError) else
                  "Permission denied" if isinstance(exc, PermissionError) else
                  "Operation timed out" if isinstance(exc, TimeoutError) else
                  "File or connection operation failed")
        code = getattr(exc, "winerror", None)
        detail = f" (WinError {code})" if code is not None else f" (errno {exc.errno})" if exc.errno is not None else ""
        return reason + detail + (f": {exc.filename}" if exc.filename else ".")
    return english_message(str(exc))


def english_result(value):
    if isinstance(value, list):
        return [english_result(item) for item in value]
    if isinstance(value, dict):
        return {key: english_message(item) if isinstance(item, str) and key in ("error", "message", "text", "warning", "reason")
                else english_result(item) for key, item in value.items()}
    return value
