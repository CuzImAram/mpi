import tkinter as tk

# --- SIMULIERTER MPI PUFFER MIT TAGS ---
# Der C-Code nutzt Tag 0 für Send-to-Right und Tag 1 für Send-to-Left (Return)
# Buffer Key: (src, dst, tag) -> Value: data
mpi_buffers = {} 

def mpi_send(src, dst, data, tag):
    mpi_buffers[(src, dst, tag)] = data

def mpi_recv(src, dst, tag):
    key = (src, dst, tag)
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
        self.state = "INIT_PASS" 
        self.local_j = 0
        self.global_start = rank * local_n
        self.my_end_idx = self.global_start + local_n - 1
        
        # Temporäre Speicher für die C-Code Logik
        self.buf_recv = None
        self.buf_send = None
        
        # Visualisierung
        self.status_msg = "Warte auf Start..."
        self.is_waiting = False
        self.highlights = [] 
        self.finished_permanently = False

    def tick(self):
        """ Führt genau einen logischen Schritt basierend auf dem C-Code aus """
        self.highlights = []
        self.is_waiting = False
        
        # --- C-CODE: Early Exit Logic ---
        limit = self.n - 1 - self.pass_idx
        if limit < self.global_start:
            self.state = "FINISHED"
            self.finished_permanently = True
            self.status_msg = "FERTIG (Limit erreicht)"
            return

        stop_in_me = (limit <= self.my_end_idx)
        local_limit = (limit - self.global_start) if stop_in_me else (self.local_n - 1)

        # --- ZUSTANDSMASCHINE (Entspricht den C-Code Blöcken) ---

        if self.state == "INIT_PASS":
            # Entscheidung: Beginne ich mit Step 1 oder Step 2?
            # C-Code: if (rank > 0) -> Step 1
            if self.rank > 0:
                self.state = "STEP_1_RECV"
            else:
                self.state = "STEP_2_LOOP"
                self.local_j = 0

        # --- C-CODE BLOCK: Step 1: Boundary check with Left neighbor ---
        elif self.state == "STEP_1_RECV":
            # Zeile 61: MPI_Recv(..., rank-1, 0, ...)
            val = mpi_recv(self.rank - 1, self.rank, 0)
            if val is not None:
                self.buf_recv = val
                
                # Zeile 63: if (buf_recv.val > local_a[0].val)
                my_left = self.data[0]
                if self.buf_recv > my_left:
                    # Tausch Logik
                    self.buf_send = my_left      # Das kleine geht zurück
                    self.data[0] = self.buf_recv # Das große bleibt hier
                    self.highlights = [(0, "#2ecc71")] # Grün
                    self.status_msg = f"Step 1: Empfange {self.buf_recv} von P{self.rank-1}. {self.buf_recv} > {my_left} -> TAUSCH."
                else:
                    self.buf_send = self.buf_recv # Das empfangene geht zurück
                    self.highlights = [(0, "#c0392b")] # Rot
                    self.status_msg = f"Step 1: Empfange {self.buf_recv}. {self.buf_recv} <= {my_left} -> KEIN Tausch."
                
                self.state = "STEP_1_SEND"
            else:
                self.is_waiting = True
                self.status_msg = f"Step 1: Warte auf Daten von P{self.rank-1}..."

        elif self.state == "STEP_1_SEND":
            # Zeile 73: MPI_Send(..., rank-1, 1, ...) (Tag 1 = Return Path)
            mpi_send(self.rank, self.rank - 1, self.buf_send, 1)
            self.status_msg = f"Step 1: Sende {self.buf_send} zurück an P{self.rank-1}."
            
            # Weiter zu Step 2
            self.state = "STEP_2_LOOP"
            self.local_j = 0

        # --- C-CODE BLOCK: Step 2: Local comparisons ---
        elif self.state == "STEP_2_LOOP":
            # Zeile 77: for (int j = 0; j < local_limit; ++j)
            if self.local_j < local_limit:
                j = self.local_j
                val_curr = self.data[j]
                val_next = self.data[j+1]
                
                # Zeile 79: if (local_a[j] > local_a[j+1])
                if val_curr > val_next:
                    self.data[j], self.data[j+1] = val_next, val_curr
                    self.highlights = [(j, "#e67e22"), (j+1, "#e67e22")]
                    self.status_msg = f"Step 2: Lokal vergleichen [{j}] vs [{j+1}]. {val_curr} > {val_next} -> TAUSCH."
                else:
                    self.highlights = [(j, "#e67e22"), (j+1, "#e67e22")]
                    self.status_msg = f"Step 2: Lokal vergleichen [{j}] vs [{j+1}]. OK."
                
                self.local_j += 1
            else:
                # Schleife fertig. 
                # C-Code Zeile 94: if (rank < p - 1 && !stop_in_me)
                if self.rank < self.p - 1 and not stop_in_me:
                    self.state = "STEP_3_SEND"
                else:
                    # Pass beendet
                    self.pass_idx += 1
                    self.state = "INIT_PASS"
                    self.status_msg = "Ende Pass. Nächster..."

        # --- C-CODE BLOCK: Step 3: Boundary check with Right neighbor ---
        elif self.state == "STEP_3_SEND":
            # Zeile 98: buf_send = local_a[local_n - 1]
            val_to_send = self.data[self.local_n - 1]
            
            # Zeile 99: MPI_Send(..., rank+1, 0, ...) (Tag 0 = Forward Path)
            mpi_send(self.rank, self.rank + 1, val_to_send, 0)
            
            self.highlights = [(self.local_n - 1, "#3498db")]
            self.status_msg = f"Step 3: Sende {val_to_send} an P{self.rank+1} (Rechts)."
            self.state = "STEP_3_RECV"

        elif self.state == "STEP_3_RECV":
            # Zeile 100: MPI_Recv(..., rank+1, 1, ...) (Tag 1 = Return Path)
            val = mpi_recv(self.rank + 1, self.rank, 1)
            
            if val is not None:
                old_val = self.data[self.local_n - 1]
                # Zeile 101: local_a[local_n - 1] = buf_recv
                self.data[self.local_n - 1] = val
                
                if val != old_val:
                     self.highlights = [(self.local_n - 1, "#2ecc71")]
                     self.status_msg = f"Step 3: Empfange {val} von P{self.rank+1}. Tausch war erfolgreich."
                else:
                     self.highlights = [(self.local_n - 1, "#c0392b")]
                     self.status_msg = f"Step 3: Empfange {val} von P{self.rank+1}. Kein Tausch passiert."

                # Pass beendet
                self.pass_idx += 1
                self.state = "INIT_PASS"
            else:
                self.is_waiting = True
                self.status_msg = f"Step 3: Warte auf Rückgabe von P{self.rank+1}..."


class MPISimulator:
    def __init__(self, root):
        self.root = root
        self.root.title("MPI C-Code Visualisierung (Step 1, 2, 3)")
        
        # Fullscreen / Maximiert starten
        try:
            self.root.state('zoomed')
        except:
            self.root.attributes('-fullscreen', True)
        
        self.n = 9
        self.p = 3
        self.local_n = 3
        
        # Mixed Stress Szenario
        raw_data = [
            [99, 95, 10], 
            [50, 45, 40], 
            [80, 5, 2]    
        ]
        
        self.procs = []
        for r in range(self.p):
            self.procs.append(MPIProcess(r, self.p, self.local_n, self.n, raw_data[r]))

        # UI Setup
        screen_w = self.root.winfo_screenwidth()
        screen_h = self.root.winfo_screenheight()
        
        # Basis-Bereich der originalen Zeichnung
        base_w = 800
        base_h = 550 
        
        # Skalierung berechnen (Platz für Button unten lassen)
        self.scale = min(screen_w / base_w, (screen_h - 250) / base_h)
        
        self.canvas = tk.Canvas(root, width=int(base_w * self.scale), height=int(base_h * self.scale), bg="#f0f0f0")
        self.canvas.pack(pady=5)
        
        self.btn = tk.Button(root, text="Nächster Tick (Alle Prozesse)", command=self.global_tick, 
                             height=2, width=30, bg="#2c3e50", fg="white", font=("Arial", 14, "bold"))
        self.btn.pack(pady=5)
        
        self.draw_state()

    def draw_arrow(self, x_start, x_end, y, color):
        self.canvas.create_line(x_start, y, x_end, y, fill=color, width=3)
        # Pfeilspitze
        self.canvas.create_polygon(x_end, y-5, x_end, y+5, x_end+10 if x_end>x_start else x_end-10, y, fill=color)

    def draw_state(self):
        self.canvas.delete("all")
        bar_colors = ["#3498db", "#2ecc71", "#f1c40f"] 
        
        all_finished = True

        for r, proc in enumerate(self.procs):
            if not proc.finished_permanently: all_finished = False
            
            x_offset = r * 260
            
            # Hintergrund Logik
            bg_col = "white"
            if proc.finished_permanently: bg_col = "#d4edda" 
            elif proc.is_waiting: bg_col = "#e0e0e0" # Grau beim Warten
            
            outline_col = "#28a745" if proc.finished_permanently else "#555"
            width = 3 if proc.finished_permanently else 1
            
            # Container Box
            self.canvas.create_rectangle(x_offset+10, 20, x_offset+250, 520, fill=bg_col, outline=outline_col, width=width)
            
            # --- HEADER ---
            self.canvas.create_text(x_offset+130, 40, text=f"Prozess P{r}", font=("Arial", 16, "bold"))
            self.canvas.create_text(x_offset+130, 65, text=f"Loop Pass: {proc.pass_idx}", font=("Arial", 14))
            
            # --- STATUS MELDUNG (Step 1, 2, 3) ---
            msg_color = "#c0392b" if proc.is_waiting else "#102a43"
            # Textumbruch für lange C-Code Erklärungen
            self.canvas.create_text(x_offset+130, 115, text=proc.status_msg, 
                                    font=("Arial", 20, "bold"), fill=msg_color, width=230, justify="center")

            # --- BALKEN ---
            for i in range(self.local_n):
                val = proc.data[i]
                x0 = x_offset + 30 + i * 65
                y_base = 480
                bar_h = val * 2.5
                y0 = y_base - bar_h
                
                fill_col = bar_colors[r]
                for h_idx, h_col in proc.highlights:
                    if h_idx == i: fill_col = h_col
                
                # Balken mit Rand
                self.canvas.create_rectangle(x0, y0, x0+50, y_base, fill=fill_col, outline="black", width=2)
                self.canvas.create_text(x0+25, y_base+20, text=str(val), font=("Arial", 14, "bold"))

            # --- Pfeile für Step 2 (Lokal) ---
            if "STEP_2" in proc.state and len(proc.highlights) == 2:
                idx1 = proc.highlights[0][0]
                idx2 = proc.highlights[1][0]
                bx1 = x_offset + 30 + idx1 * 65 + 25
                bx2 = x_offset + 30 + idx2 * 65 + 25
                # Pfeil drüber malen
                self.canvas.create_line(bx1, y_base-300, bx2, y_base-300, fill="#e67e22", width=3)
                self.canvas.create_text((bx1+bx2)/2, y_base-310, text="VS", fill="#e67e22", font=("Arial", 12, "bold"))

        if all_finished:
            self.canvas.create_text(400, 250, text="ALLES SORTIERT!", font=("Arial", 45, "bold"), fill="green")
            self.btn.config(state="disabled", bg="green", text="Fertig")

        # Skalierung anwenden
        self.canvas.scale("all", 0, 0, self.scale, self.scale)

    def global_tick(self):
        for proc in self.procs:
            proc.tick()
        self.draw_state()

if __name__ == "__main__":
    root = tk.Tk()
    sim = MPISimulator(root)
    root.mainloop()