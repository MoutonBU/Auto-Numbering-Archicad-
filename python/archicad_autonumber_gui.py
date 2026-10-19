"""
Archicad Auto-Number (GUI)
Pick an element type, prefix and start number, press Start, then place
elements in Archicad. Each new one gets the next number in its Element ID.
Needs Archicad 26+ open with a project. No registration or add-on install needed:
it talks to Archicad through its built-in Python/JSON interface.
"""
import queue
import re
import threading
import time
import tkinter as tk
from tkinter import ttk

from archicad import ACConnection

TYPES = ["Object", "Lamp", "Zone", "Wall", "Column", "Beam", "Slab", "Roof",
         "Window", "Door", "Mesh", "Morph", "Stair", "Railing"]


class Worker(threading.Thread):
    def __init__(self, etype, prefix, start, pad, renumber, log):
        super().__init__(daemon=True)
        self.etype, self.prefix, self.start_num = etype, prefix, start
        self.pad, self.renumber, self.log = pad, renumber, log
        self.stop_flag = threading.Event()

    def fmt(self, n):
        return f"{self.prefix}{str(n).zfill(self.pad) if self.pad else n}"

    def run(self):
        try:
            conn = ACConnection.connect()
            if not conn:
                self.log("Could not connect. Open Archicad (26+) with a project first, then press Start again.")
                return
            acc, act, acu = conn.commands, conn.types, conn.utilities
            prop_id = acu.GetBuiltInPropertyId("General_ElementID")
            pattern = re.compile(r"^" + re.escape(self.prefix) + r"(\d+)$")

            def read_ids(els):
                out = []
                for v in acc.GetPropertyValuesOfElements(els, [prop_id]):
                    pv = v.propertyValues[0].propertyValue if v.propertyValues else None
                    out.append((getattr(pv, "value", "") or "") if pv is not None else "")
                return out

            def write(pairs):
                if pairs:
                    acc.SetPropertyValuesOfElements([
                        act.ElementPropertyValue(el.elementId, prop_id,
                                                 act.NormalStringPropertyValue(t))
                        for el, t in pairs])

            els = acc.GetElementsByType(self.etype)
            if self.renumber:
                n = self.start_num
                pairs = []
                for el in els:
                    pairs.append((el, self.fmt(n)))
                    n += 1
                write(pairs)
                nxt = n
                self.log(f"Renumbered {len(pairs)} existing {self.etype} elements.")
            else:
                used = [int(m.group(1)) for i in read_ids(els) if (m := pattern.match(i))]
                nxt = max(used) + 1 if used else self.start_num

            known = {str(e.elementId.guid) for e in els}
            self.log(f"Watching {self.etype}. Next number: {self.fmt(nxt)}")

            while not self.stop_flag.is_set():
                time.sleep(0.7)
                try:
                    cur = acc.GetElementsByType(self.etype)
                    new = [e for e in cur if str(e.elementId.guid) not in known]
                    if new:
                        pairs = []
                        for e in new:
                            pairs.append((e, self.fmt(nxt)))
                            nxt += 1
                            known.add(str(e.elementId.guid))
                        write(pairs)
                        self.log("Numbered: " + ", ".join(t for _, t in pairs))
                    else:
                        known &= {str(e.elementId.guid) for e in cur}
                except Exception as ex:
                    self.log(f"Warning: {ex}")
                    time.sleep(2)
            self.log("Stopped.")
        except Exception as ex:
            self.log(f"Error: {ex}")


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Archicad Auto-Number")
        self.resizable(False, False)
        self.attributes("-topmost", True)
        self.q = queue.Queue()
        self.worker = None

        f = ttk.Frame(self, padding=12)
        f.grid()

        self.type_v = tk.StringVar(value="Object")
        self.prefix_v = tk.StringVar(value="")
        self.start_v = tk.StringVar(value="1")
        self.pad_v = tk.StringVar(value="0")
        self.renum_v = tk.BooleanVar(value=False)

        rows = [("Element type", ttk.Combobox(f, textvariable=self.type_v, values=TYPES, width=14)),
                ("Prefix", ttk.Entry(f, textvariable=self.prefix_v, width=16)),
                ("Start at", ttk.Entry(f, textvariable=self.start_v, width=16)),
                ("Zero padding (0 = none)", ttk.Entry(f, textvariable=self.pad_v, width=16))]
        for i, (label, widget) in enumerate(rows):
            ttk.Label(f, text=label).grid(row=i, column=0, sticky="w", pady=2)
            widget.grid(row=i, column=1, sticky="w", pady=2, padx=(8, 0))
        ttk.Checkbutton(f, text="Renumber existing elements first",
                        variable=self.renum_v).grid(row=4, column=0, columnspan=2, sticky="w", pady=4)

        self.btn = ttk.Button(f, text="Start", command=self.toggle)
        self.btn.grid(row=5, column=0, columnspan=2, sticky="ew", pady=6)
        self.logbox = tk.Text(f, height=8, width=44, state="disabled")
        self.logbox.grid(row=6, column=0, columnspan=2)

        self.after(200, self.pump)
        self.protocol("WM_DELETE_WINDOW", self.close)

    def log(self, msg):
        self.q.put(msg)

    def pump(self):
        while not self.q.empty():
            self.logbox.config(state="normal")
            self.logbox.insert("end", self.q.get() + "\n")
            self.logbox.see("end")
            self.logbox.config(state="disabled")
        if self.worker and not self.worker.is_alive():
            self.btn.config(text="Start")
        self.after(200, self.pump)

    def toggle(self):
        if self.worker and self.worker.is_alive():
            self.worker.stop_flag.set()
            self.btn.config(text="Start")
            return
        try:
            start, pad = int(self.start_v.get()), int(self.pad_v.get())
        except ValueError:
            self.log("Start and padding must be whole numbers.")
            return
        self.worker = Worker(self.type_v.get(), self.prefix_v.get(), start, pad,
                             self.renum_v.get(), self.log)
        self.worker.start()
        self.btn.config(text="Stop")

    def close(self):
        if self.worker:
            self.worker.stop_flag.set()
        self.destroy()


if __name__ == "__main__":
    App().mainloop()
