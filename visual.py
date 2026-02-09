import tkinter as tk
from tkinter import messagebox

class MPISimulator:
    def __init__(self, root):
        self.root = root
        self.root.title("MPI Bubble Sort - Arrows & Borders")
        
        self.n = 9
        self.p = 3
        self.local_n = 3
        
        # Mixed-Stress Szenario
        self.data = [
            [99, 95, 10], 
            [50, 45, 40], 
            [80, 5, 2]    
        ]
        
        self.pass_idx = 0
        self.rank_idx = 0
        self.step_idx = 0 
        self.local_j = 0
        self.total_swaps = 0
        self.finished = False

        # UI Setup
        self.header_frame = tk.Frame(root)
        self.header_frame.pack(pady=10)
        
        self.pass_label = tk.Label(self.header_frame, text=f"PASS: {self.pass_idx}", font=("Consolas", 14, "bold"))
        self.pass_label.pack(side=tk.LEFT, padx=20)
        
        self.swap_label = tk.Label(self.header_frame, text=f"SWAPS: 0", font=("Consolas", 14, "bold"), fg="#d35400")
        self.swap_label.pack(side=tk.LEFT, padx=20)

        self.canvas = tk.Canvas(root, width=750, height=450, bg="#f5f5f5")
        self.canvas.pack(pady=10)
        
        self.info_label = tk.Label(root, text="Startbereit.", font=("Arial", 11))
        self.info_label.pack()

        self.btn = tk.Button(root, text="Nächster Schritt", command=self.next_step, 
                             height=2, width=25, bg="#2c3e50", fg="white", font=("Arial", 10, "bold"))
        self.btn.pack(pady=15)

        self.draw_state()

    def draw_arrow(self, x1, x2, color):
        """ Zeichnet einen visuellen Indikator/Pfeil über zwei Balken """
        y_pos = 60
        # Horizontale Linie zwischen den Balken
        self.canvas.create_line(x1, y_pos, x2, y_pos, fill=color, width=3)
        # Pfeilspitzen nach unten
        self.canvas.create_line(x1, y_pos, x1, y_pos + 15, fill=color, width=3, arrow=tk.LAST)
        self.canvas.create_line(x2, y_pos, x2, y_pos + 15, fill=color, width=3, arrow=tk.LAST)
        # Text "VS" in der Mitte
        mid_x = (x1 + x2) / 2
        self.canvas.create_text(mid_x, y_pos - 10, text="VS", fill=color, font=("Arial", 8, "bold"))

    def draw_state(self, highlights=None):
        self.canvas.delete("all")
        limit = self.n - 1 - self.pass_idx
        bar_colors = ["#3498db", "#2ecc71", "#f1c40f"] 
        
        highlight_coords = []

        for r in range(self.p):
            x_offset = r * 240
            global_start = r * self.local_n
            is_done = (limit < global_start) or self.finished
            
            box_bg = "#d4edda" if is_done else "white"
            self.canvas.create_rectangle(x_offset+10, 15, x_offset+230, 400, fill=box_bg, outline="#999")
            self.canvas.create_text(x_offset+120, 35, text=f"Prozess P{r}", font=("Arial", 10, "bold"))

            for i in range(self.local_n):
                val = self.data[r][i]
                x0 = x_offset + 40 + i * 55
                y_base = 350
                bar_h = val * 2.5
                y0 = y_base - bar_h
                
                fill_col = bar_colors[r]
                current_highlight_color = None

                if highlights:
                    for h_rank, h_idx, h_col in highlights:
                        if r == h_rank and i == h_idx:
                            fill_col = h_col
                            current_highlight_color = h_col
                            highlight_coords.append(x0 + 20)

                # BALKEN MIT RÄNDERN (width=2)
                self.canvas.create_rectangle(x0, y0, x0+40, y_base, fill=fill_col, outline="black", width=2)
                self.canvas.create_text(x0+20, y_base+15, text=str(val), font=("Arial", 9, "bold"))

            l_limit = (limit - global_start) if (limit <= (global_start + self.local_n - 1)) else (self.local_n - 1)
            self.canvas.create_text(x_offset+120, 385, text=f"Local Limit: {l_limit if not is_done else '-'}", font=("Arial", 8, "italic"))

        # Zeichne Pfeile, wenn zwei Elemente verglichen werden
        if len(highlight_coords) == 2 and highlights:
            # Wir nehmen die Farbe des ersten Highlights für den Pfeil
            self.draw_arrow(highlight_coords[0], highlight_coords[1], highlights[0][2])

    def next_step(self):
        if self.finished: return
        limit = self.n - 1 - self.pass_idx
        global_start = self.rank_idx * self.local_n
        my_end_idx = global_start + self.local_n - 1

        if limit < global_start:
            self.move_to_next_rank()
            return

        stop_in_me = (limit <= my_end_idx)
        local_limit = (limit - global_start) if stop_in_me else (self.local_n - 1)

        if self.step_idx == 0: # LOKAL
            if self.local_j < local_limit:
                j = self.local_j
                color = "#e67e22" # Orange Standard
                if self.data[self.rank_idx][j] > self.data[self.rank_idx][j+1]:
                    self.data[self.rank_idx][j], self.data[self.rank_idx][j+1] = self.data[self.rank_idx][j+1], self.data[self.rank_idx][j]
                    self.total_swaps += 1
                    msg = f"P{self.rank_idx}: Tausch!"
                else:
                    msg = f"P{self.rank_idx}: OK."
                self.update_ui(msg, [(self.rank_idx, j, color), (self.rank_idx, j+1, color)])
                self.local_j += 1
            else:
                self.step_idx = 1; self.local_j = 0; self.next_step()

        elif self.step_idx == 1: # GRENZE
            if self.rank_idx < self.p - 1 and not stop_in_me:
                l_v, r_v = self.data[self.rank_idx][2], self.data[self.rank_idx+1][0]
                if l_v > r_v:
                    self.data[self.rank_idx][2], self.data[self.rank_idx+1][0] = r_v, l_v
                    self.total_swaps += 1
                    self.update_ui("GRENZTAUSCH!", [(self.rank_idx, 2, "#2ecc71"), (self.rank_idx+1, 0, "#2ecc71")])
                else:
                    self.update_ui("GRENZE OK.", [(self.rank_idx, 2, "#c0392b"), (self.rank_idx+1, 0, "#c0392b")])
            else:
                self.draw_state()
            self.move_to_next_rank()

    def update_ui(self, msg, highlights):
        self.info_label.config(text=msg)
        self.swap_label.config(text=f"SWAPS: {self.total_swaps}")
        self.draw_state(highlights)

    def move_to_next_rank(self):
        self.step_idx = 0; self.local_j = 0; self.rank_idx += 1
        if self.rank_idx >= self.p:
            self.rank_idx = 0; self.pass_idx += 1
            if self.pass_idx >= self.n - 1:
                self.finished = True
                self.btn.config(state="disabled", text="Fertig")
        self.pass_label.config(text=f"PASS: {self.pass_idx if not self.finished else 'ENDE'}")
        self.draw_state()

if __name__ == "__main__":
    root = tk.Tk(); sim = MPISimulator(root); root.mainloop()