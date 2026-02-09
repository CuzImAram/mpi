import tkinter as tk
from tkinter import messagebox

# --- SIMULIERTER MPI PUFFER ---
# Damit Prozesse Daten austauschen können, ohne direkt aufeinander zuzugreifen
mpi_buffers = {} # Key: (src, dst), Value: data

def mpi_send(src, dst, data):
    mpi_buffers[(src, dst)] = data

def mpi_recv(src, dst):
    key = (src, dst)
    if key in mpi_buffers:
        return mpi_buffers.pop(key)
    return None

class MPIProcess:
    def __init__(self, rank, p, local_n, total_n, data_slice):
        self.rank = rank
        self.p = p
        self.local_n = local_n
        self.n = total_n
        self.data = list(data_slice)
        
        # Initialer Zustand
        self.pass_idx = 0
        self.state = "START" # START, CHECK_LEFT_RECV, CHECK_LEFT_SEND, LOCAL_SORT, CHECK_RIGHT_SEND, CHECK_RIGHT_RECV, FINISHED
        self.local_j = 0
        self.global_start = rank * local_n
        self.my_end_idx = self.global_start + local_n - 1
        
        # Temp Speicher für Grenzwerte
        self.recv_buf = None
        
        # Für Visualisierung
        self.status_msg = "Startbereit"
        self.is_waiting = False
        self.highlights = [] # [(idx, color), ...]

    def tick(self):
        """ Führt genau einen Arbeitsschritt aus """
        self.highlights = []
        self.is_waiting = False
        
        limit = self.n - 1 - self.pass_idx
        
        # --- EARLY EXIT CHECK ---
        if limit < self.global_start:
            self.state = "FINISHED"
            self.status_msg = "FERTIG (Limit erreicht)"
            return

        # Berechne lokales Limit
        stop_in_me = (limit <= self.my_end_idx)
        local_limit = (limit - self.global_start) if stop_in_me else (self.local_n - 1)

        # --- ZUSTANDSMASCHINE ---
        
        if self.state == "START":
            # Entscheidung: Habe ich einen linken Nachbarn?
            if self.rank > 0:
                self.state = "CHECK_LEFT_RECV"
            else:
                self.state = "LOCAL_SORT"
                self.local_j = 0
        
        elif self.state == "CHECK_LEFT_RECV":
            # Warte auf Daten von Links (Rank-1)
            val = mpi_recv(self.rank - 1, self.rank)
            if val is not None:
                self.recv_buf = val
                # Vergleich mit meinem linken Element (Index 0)
                my_left = self.data[0]
                if self.recv_buf > my_left:
                    self.data[0] = self.recv_buf # Tausch lokal
                    self.recv_buf = my_left      # Das kleinere geht zurück
                    self.highlights = [(0, "#2ecc71")] # Grün für Tausch
                    self.status_msg = f"Empfange {val} > {my_left}. Tausche."
                else:
                    self.highlights = [(0, "#c0392b")] # Rot für kein Tausch
                    self.status_msg = f"Empfange {val} <= {my_left}. Kein Tausch."
                self.state = "CHECK_LEFT_SEND"
            else:
                self.is_waiting = True
                self.status_msg = f"Warte auf P{self.rank-1}..."

        elif self.state == "CHECK_LEFT_SEND":
            # Schicke das (kleinere) Element zurück nach Links
            mpi_send(self.rank, self.rank - 1, self.recv_buf)
            self.state = "LOCAL_SORT"
            self.local_j = 0
            self.status_msg = "Sende zurück an Links."

        elif self.state == "LOCAL_SORT":
            # Ein Schritt Bubble Sort
            if self.local_j < local_limit:
                j = self.local_j
                val_curr = self.data[j]
                val_next = self.data[j+1]
                
                if val_curr > val_next:
                    self.data[j], self.data[j+1] = val_next, val_curr
                    self.highlights = [(j, "#e67e22"), (j+1, "#e67e22")]
                    self.status_msg = "Lokal Sortieren"
                else:
                    self.highlights = [(j, "#e67e22"), (j+1, "#e67e22")]
                    self.status_msg = "Lokal OK"
                
                self.local_j += 1
            else:
                # Lokaler Sort fertig -> Prüfe Rechts
                if self.rank < self.p - 1 and not stop_in_me:
                    self.state = "CHECK_RIGHT_SEND"
                else:
                    # Pass beendet
                    self.pass_idx += 1
                    self.state = "START"
                    self.status_msg = "Pass fertig. Nächster..."

        elif self.state == "CHECK_RIGHT_SEND":
            # Sende mein rechtes Element an den rechten Nachbarn
            val_to_send = self.data[self.local_n - 1]
            mpi_send(self.rank, self.rank + 1, val_to_send)
            self.state = "CHECK_RIGHT_RECV"
            self.highlights = [(self.local_n - 1, "#3498db")]
            self.status_msg = f"Sende {val_to_send} an P{self.rank+1}"

        elif self.state == "CHECK_RIGHT_RECV":
            # Warte auf Antwort von Rechts
            val = mpi_recv(self.rank + 1, self.rank)
            if val is not None:
                old_val = self.data[self.local_n - 1]
                self.data[self.local_n - 1] = val
                
                if val != old_val: # Es wurde getauscht
                     self.highlights = [(self.local_n - 1, "#2ecc71")]
                     self.status_msg = f"Tausch bestätigt von P{self.rank+1}"
                else:
                     self.highlights = [(self.local_n - 1, "#c0392b")]
                     self.status_msg = f"Kein Tausch mit P{self.rank+1}"

                self.pass_idx += 1
                self.state = "START"
            else:
                self.is_waiting = True
                self.status_msg = f"Warte auf P{self.rank+1}..."


class MPISimulator:
    def __init__(self, root):
        self.root = root
        self.root.title("Echte Parallele MPI Simulation (Pipeline)")
        
        self.n = 9
        self.p = 3
        self.local_n = 3
        
        # Startdaten (Mixed Stress)
        raw_data = [
            [99, 95, 10], 
            [50, 45, 40], 
            [80, 5, 2]    
        ]
        
        # Erstelle Prozess-Objekte
        self.procs = []
        for r in range(self.p):
            self.procs.append(MPIProcess(r, self.p, self.local_n, self.n, raw_data[r]))

        # UI Setup
        self.canvas = tk.Canvas(root, width=800, height=500, bg="#f5f5f5")
        self.canvas.pack(pady=10)
        
        self.btn = tk.Button(root, text="Globaler Zeitschritt (Tick)", command=self.global_tick, 
                             height=2, width=30, bg="#2c3e50", fg="white", font=("Arial", 12, "bold"))
        self.btn.pack(pady=10)
        
        self.draw_state()

    def draw_arrow(self, x_start, x_end, y, color, label=""):
        self.canvas.create_line(x_start, y, x_end, y, fill=color, width=3)
        # Pfeilspitzen
        self.canvas.create_line(x_start, y, x_start+5, y-5, fill=color, width=3)
        self.canvas.create_line(x_start, y, x_start+5, y+5, fill=color, width=3)
        self.canvas.create_line(x_end, y, x_end-5, y-5, fill=color, width=3)
        self.canvas.create_line(x_end, y, x_end-5, y+5, fill=color, width=3)
        if label:
            self.canvas.create_text((x_start+x_end)/2, y-15, text=label, fill=color, font=("Arial", 8, "bold"))

    def draw_state(self):
        self.canvas.delete("all")
        bar_colors = ["#3498db", "#2ecc71", "#f1c40f"] 
        
        all_finished = True

        for r, proc in enumerate(self.procs):
            if proc.state != "FINISHED": all_finished = False
            
            x_offset = r * 260
            
            # --- Box Hintergrund ---
            bg_col = "white"
            if proc.state == "FINISHED": bg_col = "#d4edda" # Grün
            elif proc.is_waiting: bg_col = "#ecf0f1" # Grau (Warten)
            
            outline_col = "#28a745" if proc.state == "FINISHED" else "#7f8c8d"
            width = 3 if proc.state == "FINISHED" else 1
            
            self.canvas.create_rectangle(x_offset+10, 20, x_offset+250, 450, fill=bg_col, outline=outline_col, width=width)
            
            # --- Header Infos ---
            self.canvas.create_text(x_offset+130, 40, text=f"Prozess P{r}", font=("Arial", 12, "bold"))
            self.canvas.create_text(x_offset+130, 60, text=f"Pass: {proc.pass_idx}", font=("Arial", 10))
            
            status_col = "#e74c3c" if proc.is_waiting else "#333"
            self.canvas.create_text(x_offset+130, 80, text=proc.status_msg, font=("Arial", 9, "italic"), fill=status_col)

            # --- Balken ---
            for i in range(self.local_n):
                val = proc.data[i]
                x0 = x_offset + 30 + i * 65
                y_base = 400
                bar_h = val * 2.8
                y0 = y_base - bar_h
                
                # Farbe ermitteln
                fill_col = bar_colors[r]
                for h_idx, h_col in proc.highlights:
                    if h_idx == i: fill_col = h_col
                
                # Balken mit fettem Rand
                self.canvas.create_rectangle(x0, y0, x0+50, y_base, fill=fill_col, outline="black", width=2)
                self.canvas.create_text(x0+25, y_base+20, text=str(val), font=("Arial", 10, "bold"))

            # --- Pfeile (Local Sort) ---
            if len(proc.highlights) == 2 and proc.state == "LOCAL_SORT":
                # Annahme: Highlights sind benachbart für lokalen Sort
                idx1, idx2 = proc.highlights[0][0], proc.highlights[1][0]
                bx1 = x_offset + 30 + idx1 * 65 + 25
                bx2 = x_offset + 30 + idx2 * 65 + 25
                self.draw_arrow(bx1, bx2, 120, "#e67e22", "VS")

        if all_finished:
            self.canvas.create_text(400, 250, text="ALLES SORTIERT!", font=("Arial", 30, "bold"), fill="green")
            self.btn.config(state="disabled", bg="green", text="Fertig")

    def global_tick(self):
        # Alle Prozesse machen einen Schritt (wenn sie können)
        for proc in self.procs:
            proc.tick()
        self.draw_state()

if __name__ == "__main__":
    root = tk.Tk()
    sim = MPISimulator(root)
    root.mainloop()