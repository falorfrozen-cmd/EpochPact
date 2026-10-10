"""Two requested progression buttons; the native core performs the game operations."""
from __future__ import annotations

import json
import queue
import threading
import tkinter as tk
from pathlib import Path
from tkinter import ttk

try:
    from . import progression_backend as backend
except ImportError:
    import progression_backend as backend

HISTORY = backend.le_session.OUT / "progression-panel-history.json"


class Panel:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.results: queue.Queue = queue.Queue()
        self.current = None
        self.busy = False
        self.history = []
        try:
            entries = json.loads(HISTORY.read_text(encoding="utf-8"))
            if isinstance(entries, list):
                self.history = [entry for entry in entries if isinstance(entry, dict) and isinstance(entry.get("backup"), str)]
        except (OSError, ValueError):
            pass
        root.title("EpochPact — Görevler ve Waypointler")
        root.geometry("660x510")
        root.minsize(580, 420)
        frame = ttk.Frame(root, padding=22)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="Kampanyadan endgame'e", font=("Segoe UI", 20, "bold")).pack(anchor="w")
        self.identity = tk.StringVar(value="Çevrimdışı karakter okunuyor…")
        ttk.Label(frame, textvariable=self.identity, font=("Segoe UI", 11)).pack(anchor="w", pady=(10, 4))
        self.summary = tk.StringVar()
        ttk.Label(frame, textvariable=self.summary, justify="left").pack(anchor="w", pady=(0, 16))
        self.complete = ttk.Button(frame, text="Tüm görevleri tamamla ve ödülleri ver", command=lambda: self.act("questscomplete"))
        self.complete.pack(fill="x", ipady=9, pady=4)
        self.waypoints = ttk.Button(frame, text="Tüm waypointleri aç", command=lambda: self.act("waypointsunlock"))
        self.waypoints.pack(fill="x", ipady=9, pady=4)
        ttk.Label(frame, text="Ana ve yan kampanya görevleri + en az seviye 55. Ustalık ve fraksiyon seçimi sana ait.",
                  wraplength=590).pack(anchor="w", pady=(12, 3))
        ttk.Label(frame, text="Her işlem öncesi yedek alınır. Tamamlanmış görevler tekrar ödül vermez.",
                  wraplength=590).pack(anchor="w", pady=(0, 14))
        controls = ttk.Frame(frame)
        controls.pack(fill="x")
        self.refresh_button = ttk.Button(controls, text="Durumu yenile", command=self.refresh)
        self.refresh_button.pack(side="left")
        self.undo_button = ttk.Button(controls, text="Son işlemi geri al", command=self.undo)
        self.undo_button.pack(side="right")
        self.message = tk.StringVar(value="")
        ttk.Label(frame, textvariable=self.message, wraplength=590, justify="left").pack(anchor="w", pady=16)
        self.buttons = [self.complete, self.waypoints, self.refresh_button, self.undo_button]
        self.refresh()
        root.after(100, self.drain)

    def enable(self):
        for button in self.buttons:
            button.configure(state="disabled" if self.busy else "normal")
        if not self.current:
            self.complete.configure(state="disabled")
            self.waypoints.configure(state="disabled")
        can_undo = self.current and self.history and self.history[-1].get("id") == self.current["player"]["id"]
        if not can_undo:
            self.undo_button.configure(state="disabled")

    def start(self, work, callback, message):
        if self.busy:
            return
        self.busy = True
        self.message.set(message)
        self.enable()

        def worker():
            try:
                self.results.put((callback, work(), None))
            except Exception as exc:
                self.results.put((callback, None, str(exc)))

        threading.Thread(target=worker, daemon=True).start()

    def drain(self):
        try:
            callback, result, error = self.results.get_nowait()
        except queue.Empty:
            pass
        else:
            self.busy = False
            if error:
                self.message.set(error)
            else:
                try:
                    callback(result)
                except Exception as exc:
                    self.message.set(str(exc))
            self.enable()
        self.root.after(100, self.drain)

    def show(self, data):
        self.current = data
        player = data["player"]
        self.identity.set(f"{player['name']} · Seviye {player['level']} · Çevrimdışı")
        quests = [q for q in data["quests"] if q["eligible"]]
        done = sum(q["state"] == 2 for q in quests)
        points = [p for p in data["waypoints"] if not p["noWaypoint"]]
        unlocked = sum(p["unlocked"] for p in points)
        self.summary.set(f"Kampanya: {done}/{len(quests)} tamamlandı    Waypoint: {unlocked}/{len(points)} açık\n"
                         f"Görev pasif puanları: {data['passivePoints']}    İdol açılımları: {data['idolUnlock']}")

    def refresh(self):
        def done(data):
            self.show(data)
            self.message.set("Hazır. Görev ödülleri verilir, ardından karakter en az 55. seviyeye yükseltilir.")
        self.start(backend.read, done, "Karakter durumu okunuyor…")

    def act(self, action):
        if not self.current:
            return
        player = self.current["player"].copy()

        def work():
            result = backend.apply(action, player["id"])
            try:
                return result, backend.read(), None
            except Exception as exc:
                return result, None, str(exc)

        def done(bundle):
            result, data, read_error = bundle
            if data:
                self.show(data)
            if result.get("backup"):
                self.history.append({"id": player["id"], "name": player["name"], "backup": result["backup"]})
                HISTORY.parent.mkdir(parents=True, exist_ok=True)
                HISTORY.write_text(json.dumps(self.history, ensure_ascii=False, indent=2), encoding="utf-8")
            if not result.get("ok"):
                self.message.set(f"İşlem tamamlanamadı: {result.get('error', 'Bilinmeyen hata')}. Yedek korunuyor.")
            elif action == "questscomplete":
                before, after = result["before"], result["after"]
                self.message.set(f"{result['completed']} görev tamamlandı. Görev XP’si +{result.get('expectedQuestXp', 0):,}, "
                                 f"seviye desteği XP’si +{result.get('levelTopUpXp', 0):,}, "
                                 f"altın +{after['gold'] - before['gold']:,}. Yeni seviye: {after['level']}.")
            else:
                self.message.set(f"{result['added']} waypoint açıldı. Haritadan endgame merkezine geçebilirsin.")
            if read_error:
                self.message.set(self.message.get() + " Durum yenilenemedi: " + read_error)

        self.start(work, done, "Yedek alınıyor ve işlem uygulanıyor…")

    def undo(self):
        if not self.history:
            return
        entry = self.history[-1]

        def done(result):
            self.history.pop()
            HISTORY.write_text(json.dumps(self.history, ensure_ascii=False, indent=2), encoding="utf-8")
            self.current = None
            self.identity.set(f"{result['player']} — işlem öncesi kayıt geri yüklendi")
            self.summary.set("")
            self.message.set("Oyun temizce kapatıldı ve yedek geri yüklendi. Oyunu açıp karakterine girdikten sonra durumu yenile.")

        self.start(lambda: backend.undo(Path(entry["backup"])), done, "Oyun kaydedilerek kapatılıyor; işlem öncesi kayıt geri yükleniyor…")


def main():
    root = tk.Tk()
    Panel(root)
    root.mainloop()


if __name__ == "__main__":
    main()
