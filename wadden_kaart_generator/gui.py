import json
import os
import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from PIL import ImageTk

import config
from renderer import teken_kaart

KAART_B = 800
KAART_H = 400

ACHTERGROND  = "#1a2035"
PANEEL_BG    = "#232b42"
TEKST_KLEUR  = "#d0d8f0"
ACCENT       = "#4a90d9"
KNOP_BG      = "#2e3d60"
KNOP_FG      = "#ffffff"
GEVAAR       = "#e05050"
OK_KLEUR     = "#50c060"
STATUS_BG    = "#131825"


class WaddenKaartApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Wadden Kaart Generator v1.0 — BKOS-NUI")
        self.root.configure(bg=ACHTERGROND)
        self.root.resizable(False, False)

        # Data opslag
        self.bathymetrie_grid = None
        self.lons = None
        self.lats = None
        self.vaarwegen  = None
        self.boeien     = None
        self.atlas_lw   = None
        self.atlas_hw   = None
        self.actief_gebied = "wadden_west"
        self.export_map = ""

        # GUI state
        self.berichten_queue = queue.Queue()
        self._tk_img = None

        self._bouw_gui()
        self._update_spiff_gebruik()
        self._verwerk_berichten()

    # ── GUI opbouw ────────────────────────────────────────────────────────────

    def _bouw_gui(self):
        hoofd = tk.Frame(self.root, bg=ACHTERGROND)
        hoofd.pack(fill="both", expand=True, padx=8, pady=8)

        # Linker paneel (instellingen)
        links = tk.Frame(hoofd, bg=PANEEL_BG, width=300)
        links.pack(side="left", fill="y", padx=(0, 6))
        links.pack_propagate(False)

        # Rechter paneel (kaartpreview)
        rechts = tk.Frame(hoofd, bg=ACHTERGROND)
        rechts.pack(side="left", fill="both", expand=True)

        self._bouw_linker_paneel(links)
        self._bouw_rechter_paneel(rechts)

    def _lbl(self, parent, tekst, **kw):
        return tk.Label(parent, text=tekst, bg=PANEEL_BG, fg=TEKST_KLEUR,
                        font=("Segoe UI", 9), **kw)

    def _sectie(self, parent, titel):
        tk.Label(parent, text=titel, bg=PANEEL_BG, fg=ACCENT,
                 font=("Segoe UI", 9, "bold")).pack(anchor="w", padx=8, pady=(10, 2))
        tk.Frame(parent, bg=ACCENT, height=1).pack(fill="x", padx=8)

    def _knop(self, parent, tekst, cmd, **kw):
        return tk.Button(parent, text=tekst, command=cmd,
                         bg=KNOP_BG, fg=KNOP_FG, relief="flat",
                         font=("Segoe UI", 9), cursor="hand2",
                         activebackground=ACCENT, activeforeground="white", **kw)

    def _bouw_linker_paneel(self, parent):
        # Titel
        tk.Label(parent, text="WADDEN KAART GENERATOR",
                 bg=PANEEL_BG, fg=ACCENT,
                 font=("Segoe UI", 10, "bold")).pack(pady=(10, 2))
        tk.Label(parent, text="BKOS-NUI Desktop Tool",
                 bg=PANEEL_BG, fg=TEKST_KLEUR,
                 font=("Segoe UI", 8)).pack(pady=(0, 6))

        # Gebied selectie
        self._sectie(parent, "GEBIED")
        gebied_namen = {k: v["naam"] for k, v in config.GEBIEDEN.items()}
        self.gebied_var = tk.StringVar(value=list(gebied_namen.keys())[0])
        cb = ttk.Combobox(parent, textvariable=self.gebied_var,
                          values=list(gebied_namen.values()), state="readonly", width=28)
        cb.pack(padx=8, pady=3)

        # Data ophalen
        self._sectie(parent, "DATA OPHALEN")
        frame_knoppen = tk.Frame(parent, bg=PANEEL_BG)
        frame_knoppen.pack(fill="x", padx=8, pady=4)

        self._knop(frame_knoppen, "Alles ophalen", self._haal_alles_op,
                   width=14).grid(row=0, column=0, padx=2, pady=2)
        self._knop(frame_knoppen, "Bathymetrie", self._haal_batho_op,
                   width=12).grid(row=0, column=1, padx=2, pady=2)
        self._knop(frame_knoppen, "Vaarwegen",   self._haal_vaarwegen_op,
                   width=12).grid(row=1, column=0, padx=2, pady=2)
        self._knop(frame_knoppen, "Boeien",      self._haal_boeien_op,
                   width=12).grid(row=1, column=1, padx=2, pady=2)
        self._knop(frame_knoppen, "Stroming LW", self._haal_stroming_lw,
                   width=12).grid(row=2, column=0, padx=2, pady=2)
        self._knop(frame_knoppen, "Stroming HW", self._haal_stroming_hw,
                   width=12).grid(row=2, column=1, padx=2, pady=2)

        # Weergave instellingen
        self._sectie(parent, "WEERGAVE")
        grid_inst = tk.Frame(parent, bg=PANEEL_BG)
        grid_inst.pack(fill="x", padx=8, pady=4)

        # Getijhoogte slider
        self._lbl(grid_inst, "Getij (cm NAP):").grid(row=0, column=0, sticky="w")
        self.getij_var = tk.IntVar(value=0)
        self.getij_lbl = self._lbl(grid_inst, " 0 cm")
        self.getij_lbl.grid(row=0, column=1, sticky="e")
        getij_slider = tk.Scale(parent, from_=-200, to=300,
                                orient="horizontal", variable=self.getij_var,
                                bg=PANEEL_BG, fg=TEKST_KLEUR, troughcolor=KNOP_BG,
                                highlightthickness=0, command=self._on_getij)
        getij_slider.pack(fill="x", padx=8)

        # Diepgang
        self._lbl(grid_inst, "Diepgang (cm):").grid(row=1, column=0, sticky="w")
        self.diepgang_var = tk.IntVar(value=120)
        tk.Spinbox(grid_inst, from_=30, to=500, width=6,
                   textvariable=self.diepgang_var,
                   bg=KNOP_BG, fg=KNOP_FG, buttonbackground=KNOP_BG,
                   command=self._herteken).grid(row=1, column=1, sticky="e", pady=2)

        # Buffer
        self._lbl(grid_inst, "Buffer (cm):").grid(row=2, column=0, sticky="w")
        self.buffer_var = tk.IntVar(value=50)
        cb_buf = ttk.Combobox(grid_inst, textvariable=self.buffer_var,
                              values=[0, 25, 50, 100, 150], width=6, state="readonly")
        cb_buf.grid(row=2, column=1, sticky="e", pady=2)
        cb_buf.bind("<<ComboboxSelected>>", lambda _: self._herteken())

        # Lagen checkboxes
        self._sectie(parent, "LAGEN")
        self.toon_vaarwegen_var = tk.BooleanVar(value=True)
        self.toon_boeien_var    = tk.BooleanVar(value=True)
        self.toon_stroming_var  = tk.BooleanVar(value=True)
        self.toon_windroos_var  = tk.BooleanVar(value=True)

        def _chk(tekst, var):
            tk.Checkbutton(parent, text=tekst, variable=var,
                           bg=PANEEL_BG, fg=TEKST_KLEUR, selectcolor=KNOP_BG,
                           activebackground=PANEEL_BG, activeforeground=TEKST_KLEUR,
                           command=self._herteken).pack(anchor="w", padx=12)

        _chk("Vaarwegen", self.toon_vaarwegen_var)
        _chk("Boeien / betonning", self.toon_boeien_var)
        _chk("Stromingspijlen", self.toon_stroming_var)
        _chk("Windroos", self.toon_windroos_var)

        # Tijdstap stroming + atlas keuze
        self._sectie(parent, "STROMINGSTIJDSTAP")
        atlas_frame = tk.Frame(parent, bg=PANEEL_BG)
        atlas_frame.pack(fill="x", padx=8, pady=2)
        self.atlas_var = tk.StringVar(value="lw")
        tk.Radiobutton(atlas_frame, text="LW springtij", variable=self.atlas_var,
                       value="lw", bg=PANEEL_BG, fg=TEKST_KLEUR,
                       selectcolor=KNOP_BG, activebackground=PANEEL_BG,
                       command=self._herteken).pack(side="left")
        tk.Radiobutton(atlas_frame, text="HW springtij", variable=self.atlas_var,
                       value="hw", bg=PANEEL_BG, fg=TEKST_KLEUR,
                       selectcolor=KNOP_BG, activebackground=PANEEL_BG,
                       command=self._herteken).pack(side="left", padx=8)
        self._lbl(parent, "T-6h ◄──────────────► T+6h").pack(padx=8, anchor="w")
        self.tijdstap_var = tk.IntVar(value=12)  # 12 = T=0
        ts_slider = tk.Scale(parent, from_=0, to=24,
                             orient="horizontal", variable=self.tijdstap_var,
                             bg=PANEEL_BG, fg=TEKST_KLEUR, troughcolor=KNOP_BG,
                             highlightthickness=0, command=lambda _: self._herteken())
        ts_slider.pack(fill="x", padx=8)

        # SPIFF gebruik
        self._sectie(parent, "SPIFF GEBRUIK")
        self.spiff_frame = tk.Frame(parent, bg=PANEEL_BG)
        self.spiff_frame.pack(fill="x", padx=8, pady=4)
        self.spiff_labels = {}
        for naam in ("Bathymetrie", "Vaarwegen", "Boeien", "Atlas LW", "Atlas HW", "Totaal"):
            rij = tk.Frame(self.spiff_frame, bg=PANEEL_BG)
            rij.pack(fill="x")
            self._lbl(rij, f"{naam}:").pack(side="left")
            lbl = self._lbl(rij, "—")
            lbl.pack(side="right")
            self.spiff_labels[naam] = lbl

        self.spiff_balk_var = tk.DoubleVar(value=0)
        self.spiff_balk = ttk.Progressbar(parent, variable=self.spiff_balk_var,
                                           maximum=100, length=270)
        self.spiff_balk.pack(padx=8, pady=4)

        # Export knop
        self._sectie(parent, "EXPORT")
        frame_exp = tk.Frame(parent, bg=PANEEL_BG)
        frame_exp.pack(fill="x", padx=8, pady=6)
        self._knop(frame_exp, "Kies export map", self._kies_export_map,
                   width=14).grid(row=0, column=0, padx=2)
        self._knop(frame_exp, "Export naar SPIFF", self._exporteer,
                   width=14, bg="#336633").grid(row=0, column=1, padx=2)
        self.export_map_lbl = self._lbl(parent, "Geen map geselecteerd")
        self.export_map_lbl.pack(padx=8, anchor="w")

    def _bouw_rechter_paneel(self, parent):
        # Canvas voor kaartpreview
        tk.Label(parent, text="KAARTPREVIEW (800×400)",
                 bg=ACHTERGROND, fg=ACCENT,
                 font=("Segoe UI", 9, "bold")).pack(anchor="w", pady=(0, 2))

        self.canvas = tk.Canvas(parent, width=KAART_B, height=KAART_H,
                                bg="#0a1428", highlightthickness=1,
                                highlightbackground=ACCENT)
        self.canvas.pack()

        # Tekst op lege canvas
        self._canvas_placeholder()

        # Statusbalk
        self.status_var = tk.StringVar(value="Gereed. Klik op 'Alles ophalen' om te beginnen.")
        status_frame = tk.Frame(parent, bg=STATUS_BG)
        status_frame.pack(fill="x", pady=(6, 0))
        tk.Label(status_frame, textvariable=self.status_var,
                 bg=STATUS_BG, fg=TEKST_KLEUR,
                 font=("Segoe UI", 8), anchor="w").pack(fill="x", padx=6, pady=3)

        # Voortgangsbalk
        self.voortgang_var = tk.DoubleVar(value=0)
        self.voortgang_balk = ttk.Progressbar(parent, variable=self.voortgang_var,
                                               maximum=100, length=KAART_B)
        self.voortgang_balk.pack(pady=(2, 0))

        # Log venster
        log_frame = tk.Frame(parent, bg=ACHTERGROND)
        log_frame.pack(fill="both", expand=True, pady=(6, 0))
        tk.Label(log_frame, text="Log", bg=ACHTERGROND, fg=TEKST_KLEUR,
                 font=("Segoe UI", 8)).pack(anchor="w")
        self.log_tekst = tk.Text(log_frame, height=8, bg=STATUS_BG, fg=TEKST_KLEUR,
                                  font=("Courier", 8), state="disabled",
                                  wrap="word", relief="flat")
        scrollbar = ttk.Scrollbar(log_frame, command=self.log_tekst.yview)
        self.log_tekst.configure(yscrollcommand=scrollbar.set)
        self.log_tekst.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

    def _canvas_placeholder(self):
        self.canvas.create_text(
            KAART_B // 2, KAART_H // 2,
            text="Geen kaartdata — klik op 'Alles ophalen'",
            fill="#405070", font=("Segoe UI", 12),
        )

    # ── Callback handlers ──────────────────────────────────────────────────────

    def _on_getij(self, waarde):
        self.getij_lbl.config(text=f"{int(waarde):+d} cm")
        self._herteken()

    def _herteken(self):
        if self.bathymetrie_grid is None and self.vaarwegen is None:
            return

        gebied = config.GEBIEDEN[self.actief_gebied]
        stroming = self.atlas_hw if getattr(self, 'atlas_var', None) and self.atlas_var.get() == "hw" else self.atlas_lw

        img = teken_kaart(
            bathymetrie_grid=self.bathymetrie_grid,
            lons=self.lons,
            lats=self.lats,
            bbox=gebied["bbox"],
            rotatie_graden=gebied["rotatie_graden"],
            getij_cm=self.getij_var.get(),
            lat_offset_cm=gebied["lat_offset_cm"],
            vaarwegen=self.vaarwegen,
            boeien=self.boeien,
            stroming=stroming,
            tijdstap_index=self.tijdstap_var.get(),
            toon_vaarwegen=self.toon_vaarwegen_var.get(),
            toon_boeien=self.toon_boeien_var.get(),
            toon_stroming=self.toon_stroming_var.get(),
            toon_windroos=self.toon_windroos_var.get(),
            diepgang_cm=self.diepgang_var.get(),
            buffer_cm=self.buffer_var.get(),
        )

        self._tk_img = ImageTk.PhotoImage(img)
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, anchor="nw", image=self._tk_img)

    # ── Data ophalen threads ───────────────────────────────────────────────────

    def _haal_alles_op(self):
        threading.Thread(target=self._worker_alles, daemon=True).start()

    def _worker_alles(self):
        self._bericht("=== Alle data ophalen ===", voortgang=0)
        try:
            self._bericht("1/5 Bathymetrie...", voortgang=10)
            self._doe_bathymetrie()
            self._bericht("2/5 Vaarwegen...", voortgang=30)
            self._doe_vaarwegen()
            self._bericht("3/5 Boeien...", voortgang=50)
            self._doe_boeien()
            self._bericht("4/5 Stroming LW...", voortgang=65)
            self._doe_stroming("lw")
            self._bericht("5/5 Stroming HW...", voortgang=80)
            self._doe_stroming("hw")
            self._bericht("Klaar! Alle data opgehaald.", voortgang=100, herteken=True)
        except Exception as e:
            self._bericht(f"FOUT: {e}", voortgang=0)

    def _haal_batho_op(self):
        threading.Thread(target=self._doe_bathymetrie_thread, daemon=True).start()

    def _doe_bathymetrie_thread(self):
        try:
            self._doe_bathymetrie()
            self._bericht("Bathymetrie klaar.", voortgang=100, herteken=True)
        except Exception as e:
            self._bericht(f"FOUT bathymetrie: {e}")

    def _haal_vaarwegen_op(self):
        threading.Thread(target=self._doe_vaarwegen_thread, daemon=True).start()

    def _doe_vaarwegen_thread(self):
        try:
            self._doe_vaarwegen()
            self._bericht("Vaarwegen klaar.", voortgang=100, herteken=True)
        except Exception as e:
            self._bericht(f"FOUT vaarwegen: {e}")

    def _haal_boeien_op(self):
        threading.Thread(target=self._doe_boeien_thread, daemon=True).start()

    def _doe_boeien_thread(self):
        try:
            self._doe_boeien()
            self._bericht("Boeien klaar.", voortgang=100, herteken=True)
        except Exception as e:
            self._bericht(f"FOUT boeien: {e}")

    def _haal_stroming_lw(self):
        threading.Thread(target=lambda: self._doe_stroming_thread("lw"), daemon=True).start()

    def _haal_stroming_hw(self):
        threading.Thread(target=lambda: self._doe_stroming_thread("hw"), daemon=True).start()

    def _doe_stroming_thread(self, atlas):
        try:
            self._doe_stroming(atlas)
            self._bericht(f"Stroming {atlas.upper()} klaar.", voortgang=100, herteken=True)
        except Exception as e:
            self._bericht(f"FOUT stroming {atlas}: {e}")

    # ── Interne data workers ───────────────────────────────────────────────────

    def _doe_bathymetrie(self):
        from api.bathymetrie import haal_bathymetrie_op
        gebied = config.GEBIEDEN[self.actief_gebied]
        grid, lons, lats = haal_bathymetrie_op(
            gebied["bbox"], resolutie_m=200, callback=self._bericht
        )
        self.bathymetrie_grid = grid
        self.lons = lons
        self.lats = lats

    def _doe_vaarwegen(self):
        from api.vaarwegen import haal_vaarwegen_op
        gebied = config.GEBIEDEN[self.actief_gebied]
        self.vaarwegen = haal_vaarwegen_op(gebied["bbox"], callback=self._bericht)

    def _doe_boeien(self):
        from api.boeien import haal_boeien_op
        gebied = config.GEBIEDEN[self.actief_gebied]
        self.boeien = haal_boeien_op(gebied["rd_bbox"], callback=self._bericht)

    def _doe_stroming(self, atlas_key):
        from api.matroos import haal_stroomatlas_op
        from processing.stroomatlas import genereer_stroompunten
        gebied = config.GEBIEDEN[self.actief_gebied]

        if self.vaarwegen is None:
            self._bericht("Haal eerst vaarwegen op voor stroompunten.")
            return

        punten = genereer_stroompunten(
            self.vaarwegen, afstand_m=gebied.get("stroming_afstand_m", 500)
        )
        self._bericht(f"Stroming: {len(punten)} punten langs vaarwegen")

        atlas_cfg = gebied[f"atlas_{atlas_key}"]
        data = haal_stroomatlas_op(punten, atlas_cfg, callback=self._bericht)

        if atlas_key == "lw":
            self.atlas_lw = data
        else:
            self.atlas_hw = data

        # Sla verschilrapport op als beide atlassen beschikbaar zijn
        if self.atlas_lw and self.atlas_hw:
            self._sla_verschilrapport_op()

    def _sla_verschilrapport_op(self):
        from processing.stroomatlas import bereken_verschilrapport
        import datetime
        rapport = bereken_verschilrapport(self.atlas_lw, self.atlas_hw)
        rapport["gegenereerd"] = datetime.datetime.utcnow().isoformat() + "Z"
        if self.export_map:
            pad = os.path.join(self.export_map, "stroming", "verschil.json")
            os.makedirs(os.path.dirname(pad), exist_ok=True)
            with open(pad, "w") as f:
                import json
                json.dump(rapport, f, indent=2)
            self._bericht(f"Verschilrapport: {rapport['samenvatting']['stabiele_punten_pct']}% stabiel")

    # ── Export ─────────────────────────────────────────────────────────────────

    def _kies_export_map(self):
        map_pad = filedialog.askdirectory(title="Kies export map voor SPIFF bestanden")
        if map_pad:
            self.export_map = map_pad
            self.export_map_lbl.config(text=os.path.basename(map_pad) or map_pad)
            self._update_spiff_gebruik()

    def _exporteer(self):
        if not self.export_map:
            messagebox.showwarning("Export", "Kies eerst een export map.")
            return
        threading.Thread(target=self._worker_export, daemon=True).start()

    def _worker_export(self):
        from export.exporter import (exporteer_bathymetrie, exporteer_vaarwegen,
                                      exporteer_boeien, exporteer_stroomatlas,
                                      exporteer_meta, bereken_spiff_gebruik)
        from processing.contour import bereken_contourlijnen, nap_naar_lat_cm

        gebied = config.GEBIEDEN[self.actief_gebied]
        kaart_map   = os.path.join(self.export_map, "kaart")
        stroom_map  = os.path.join(self.export_map, "stroming")

        try:
            self._bericht("Export starten...", voortgang=0)

            if self.bathymetrie_grid is not None:
                self._bericht("Contourlijnen berekenen...", voortgang=10)
                diepte_grid = nap_naar_lat_cm(
                    self.bathymetrie_grid, gebied["lat_offset_cm"]
                )
                contours = bereken_contourlijnen(
                    self.lons, self.lats, diepte_grid, callback=self._bericht
                )
                grootte, n = exporteer_bathymetrie(
                    contours, gebied["lat_offset_cm"],
                    os.path.join(kaart_map, "bathymetrie.bin")
                )
                self._bericht(f"bathymetrie.bin: {grootte//1024} KB, {n} lijnen",
                              voortgang=35)

            if self.vaarwegen is not None:
                grootte, n = exporteer_vaarwegen(
                    self.vaarwegen, gebied["bbox"],
                    os.path.join(kaart_map, "vaarwegen.bin")
                )
                self._bericht(f"vaarwegen.bin: {grootte//1024} KB, {n} lijnen",
                              voortgang=50)

            if self.boeien is not None:
                grootte, n = exporteer_boeien(
                    self.boeien, os.path.join(kaart_map, "boeien.bin")
                )
                self._bericht(f"boeien.bin: {grootte//1024} KB, {n} boeien",
                              voortgang=60)

            if self.atlas_lw is not None:
                grootte, n = exporteer_stroomatlas(
                    self.atlas_lw,
                    gebied["atlas_lw"]["t0_utc"],
                    os.path.join(stroom_map, "atlas_lw.bin")
                )
                self._bericht(f"atlas_lw.bin: {grootte//1024} KB, {n} punten",
                              voortgang=75)

            if self.atlas_hw is not None:
                grootte, n = exporteer_stroomatlas(
                    self.atlas_hw,
                    gebied["atlas_hw"]["t0_utc"],
                    os.path.join(stroom_map, "atlas_hw.bin")
                )
                self._bericht(f"atlas_hw.bin: {grootte//1024} KB, {n} punten",
                              voortgang=85)

            exporteer_meta(gebied, os.path.join(kaart_map, "meta.json"))
            self._bericht("meta.json geschreven", voortgang=90)

            # SPIFF gebruik berekenen
            self._update_spiff_gebruik()
            self._bericht("Export klaar!", voortgang=100)

        except Exception as e:
            self._bericht(f"FOUT bij export: {e}")

    def _update_spiff_gebruik(self):
        if not self.export_map:
            return
        from export.exporter import bereken_spiff_gebruik
        pad_map = {
            "Bathymetrie": os.path.join(self.export_map, "kaart", "bathymetrie.bin"),
            "Vaarwegen":   os.path.join(self.export_map, "kaart", "vaarwegen.bin"),
            "Boeien":      os.path.join(self.export_map, "kaart", "boeien.bin"),
            "Atlas LW":    os.path.join(self.export_map, "stroming", "atlas_lw.bin"),
            "Atlas HW":    os.path.join(self.export_map, "stroming", "atlas_hw.bin"),
        }
        gebruik = bereken_spiff_gebruik(pad_map)
        for naam, lbl in self.spiff_labels.items():
            if naam == "Totaal":
                kleur = GEVAAR if gebruik["pct"] > 90 else OK_KLEUR
                lbl.config(
                    text=f"{gebruik['totaal']//1024} KB ({gebruik['pct']}%)",
                    fg=kleur,
                )
            elif naam in gebruik:
                lbl.config(text=f"{gebruik[naam]//1024} KB")
        self.spiff_balk_var.set(gebruik["pct"])

    # ── Berichten systeem ──────────────────────────────────────────────────────

    def _bericht(self, tekst, voortgang=None, herteken=False):
        """Thread-safe bericht naar log en statusbalk."""
        self.berichten_queue.put((tekst, voortgang, herteken))

    def _verwerk_berichten(self):
        """Verwerkt berichten uit de queue in de Tkinter main thread."""
        try:
            while True:
                tekst, voortgang, herteken = self.berichten_queue.get_nowait()
                self.status_var.set(tekst)
                self._log(tekst)
                if voortgang is not None:
                    self.voortgang_var.set(voortgang)
                if herteken:
                    self._herteken()
                    self._update_spiff_gebruik()
        except queue.Empty:
            pass
        self.root.after(100, self._verwerk_berichten)

    def _log(self, tekst):
        self.log_tekst.configure(state="normal")
        self.log_tekst.insert("end", tekst + "\n")
        self.log_tekst.see("end")
        self.log_tekst.configure(state="disabled")
