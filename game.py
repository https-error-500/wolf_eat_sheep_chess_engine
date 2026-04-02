import tkinter as tk
from tkinter import messagebox
import subprocess

# --- 游戏核心参数与规则预计算 ---
WOLF_TURN = 0
SHEEP_TURN = 1

ADJACENT = [[] for _ in range(25)]
JUMP = [[] for _ in range(25)]

for r in range(5):
    for c in range(5):
        i = r * 5 + c
        for dr, dc in [(-1,0), (1,0), (0,-1), (0,1)]:
            nr, nc = r + dr, c + dc
            if 0 <= nr < 5 and 0 <= nc < 5:
                ADJACENT[i].append(nr * 5 + nc)
                jr, jc = r + 2*dr, c + 2*dc
                if 0 <= jr < 5 and 0 <= jc < 5:
                    JUMP[i].append({'dist1': nr * 5 + nc, 'dist2': jr * 5 + jc})

class WolfSheepGameGUI:
    def __init__(self, master):
        self.master = master
        self.master.title("狼吃羊棋 - 终极真理版")
        self.master.geometry("600x750")
        self.master.configure(bg="#F0E6D2") 

        try:
            self.engine = subprocess.Popen(['query_engine.exe'], 
                                           stdin=subprocess.PIPE, 
                                           stdout=subprocess.PIPE, 
                                           stderr=subprocess.PIPE,
                                           text=True)
        except Exception as e:
            messagebox.showerror("致命错误", f"找不到 query_engine.exe！\n{e}")
            self.master.destroy()
            return

        self.wolves = (1<<21) | (1<<22) | (1<<23)
        self.sheep = (1<<15) - 1
        self.turn = WOLF_TURN
        self.human_role = None
        self.selected_idx = None
        self.total_moves = 0
        self.last_move = None  # 【新增】用来记录上一步的动作
        
        self.create_widgets()
        self.show_role_selection()

    def create_widgets(self):
        self.info_var = tk.StringVar(value="请选择你的阵营")
        tk.Label(self.master, textvariable=self.info_var, font=("微软雅黑", 16, "bold"), bg="#F0E6D2", fg="#333333").pack(pady=10)

        self.eval_var = tk.StringVar(value="")
        self.eval_label = tk.Label(self.master, textvariable=self.eval_var, font=("微软雅黑", 12, "bold"), bg="#F0E6D2", fg="#D32F2F")
        self.eval_label.pack(pady=5)

        self.canvas_size = 500
        self.margin = 50
        self.cell = (self.canvas_size - 2 * self.margin) / 4
        self.canvas = tk.Canvas(self.master, width=self.canvas_size, height=self.canvas_size, bg="#DEB887", highlightthickness=2, highlightbackground="#8B4513")
        self.canvas.pack(pady=10)
        self.canvas.bind("<Button-1>", self.on_canvas_click)

        for i in range(5):
            x = self.margin + i * self.cell
            self.canvas.create_line(x, self.margin, x, self.canvas_size - self.margin, width=3)
            y = self.margin + i * self.cell
            self.canvas.create_line(self.margin, y, self.canvas_size - self.margin, y, width=3)

    def show_role_selection(self):
        self.sel_win = tk.Toplevel(self.master)
        self.sel_win.title("选择阵营")
        self.sel_win.geometry("300x150")
        self.sel_win.transient(self.master)
        self.sel_win.grab_set()
        tk.Label(self.sel_win, text="请选择你要扮演的角色：", font=("微软雅黑", 12)).pack(pady=20)
        
        btn_frame = tk.Frame(self.sel_win)
        btn_frame.pack()
        tk.Button(btn_frame, text="🐺 扮演狼 (先手)", font=("微软雅黑", 12), command=lambda: self.start_game(0)).pack(side=tk.LEFT, padx=10)
        tk.Button(btn_frame, text="🐑 扮演羊 (后手)", font=("微软雅黑", 12), command=lambda: self.start_game(1)).pack(side=tk.LEFT, padx=10)

    def start_game(self, role):
        self.human_role = role
        self.sel_win.destroy()
        
        # 【新增】游戏开局时查询一次评价，确保一开始就有正确的上帝视角
        k = self.get_k()
        self.engine.stdin.write(f"{self.wolves} {self.sheep} {self.turn} {k}\n")
        self.engine.stdin.flush()
        resp = self.engine.stdout.readline().strip()
        if resp != "NOMOVE":
            _, _, ev = map(int, resp.split())
            self.update_eval_ui(ev)
            
        self.update_board_ui()
        self.check_turn()

    def get_k(self):
        return bin(self.sheep).count('1')

    def check_turn(self):
        k = self.get_k()
        if k <= 3:
            self.info_var.set("🐺 游戏结束：羊被吃到剩 3 只，狼方大获全胜！")
            return
        if self.total_moves >= 200:
            self.info_var.set("⌛ 游戏结束：已达 100 回合上限，绝对平局！")
            return
        if not self.has_any_legal_move(self.turn):
            loser = "狼" if self.turn == WOLF_TURN else "羊"
            winner = "羊" if self.turn == WOLF_TURN else "狼"
            self.info_var.set(f"🏆 游戏结束：【{loser}】无路可走，【{winner}】方获胜！")
            return

        role_str = "狼" if self.turn == WOLF_TURN else "羊"
        self.info_var.set(f"第 {self.total_moves//2 + 1} 回合 | 当前轮到: 【{role_str}】")

        if self.turn != self.human_role:
            self.master.after(500, self.do_ai_move)

    def do_ai_move(self):
        k = self.get_k()
        self.engine.stdin.write(f"{self.wolves} {self.sheep} {self.turn} {k}\n")
        self.engine.stdin.flush()
        
        response = self.engine.stdout.readline().strip()
        if response == "NOMOVE":
            return
            
        f, t, ev = map(int, response.split())
        self.update_eval_ui(ev)
        self.execute_move(f, t)

    def update_eval_ui(self, ev):
        if ev == 0:
            self.eval_var.set("【上帝之眼】 局势: 绝对平局 (稳住)")
            self.eval_label.config(fg="#F57C00") 
        elif ev > 0:
            self.eval_var.set(f"【上帝之眼】 局势: 狼方必胜 (剩 {ev} 步)")
            self.eval_label.config(fg="#D32F2F" if self.human_role==1 else "#388E3C")
        else:
            self.eval_var.set(f"【上帝之眼】 局势: 羊方必胜 (剩 {-ev} 步)")
            self.eval_label.config(fg="#388E3C" if self.human_role==1 else "#D32F2F")

    def execute_move(self, f, t):
        # 【新增】记录走棋动作，以便画残影
        self.last_move = (f, t)
        
        if self.turn == WOLF_TURN:
            self.wolves ^= (1 << f) ^ (1 << t)
            if self.sheep & (1 << t):
                self.sheep ^= (1 << t)
        else:
            self.sheep ^= (1 << f) ^ (1 << t)

        self.selected_idx = None
        self.turn = 1 - self.turn
        self.total_moves += 1
        
        self.update_board_ui()
        self.check_turn()
        
    def has_any_legal_move(self, turn_to_check):
        if turn_to_check == WOLF_TURN:
            for i in range(25):
                if self.wolves & (1 << i):
                    for tg in JUMP[i]:
                        if not ((self.wolves | self.sheep) & (1 << tg['dist1'])) and (self.sheep & (1 << tg['dist2'])):
                            return True
                    for n in ADJACENT[i]:
                        if not ((self.wolves | self.sheep) & (1 << n)):
                            return True
        else:
            for i in range(25):
                if self.sheep & (1 << i):
                    for n in ADJACENT[i]:
                        if not ((self.wolves | self.sheep) & (1 << n)):
                            return True
        return False
    
    def get_legal_human_moves(self, idx):
        moves = []
        if self.turn == WOLF_TURN:
            for tg in JUMP[idx]:
                if not ((self.wolves | self.sheep) & (1 << tg['dist1'])) and (self.sheep & (1 << tg['dist2'])):
                    moves.append(tg['dist2'])
            for n in ADJACENT[idx]:
                if not ((self.wolves | self.sheep) & (1 << n)):
                    moves.append(n)
        else:
            for n in ADJACENT[idx]:
                if not ((self.wolves | self.sheep) & (1 << n)):
                    moves.append(n)
        return moves

    def on_canvas_click(self, event):
        if self.turn != self.human_role: return 
        if self.get_k() <= 3 or self.total_moves >= 200: return
        if not self.has_any_legal_move(self.turn): return 
        
        col = round((event.x - self.margin) / self.cell)
        row = round((event.y - self.margin) / self.cell)
        
        if 0 <= col < 5 and 0 <= row < 5:
            clicked_idx = row * 5 + col
            
            if self.selected_idx is None:
                if self.turn == WOLF_TURN and (self.wolves & (1 << clicked_idx)):
                    self.selected_idx = clicked_idx
                elif self.turn == SHEEP_TURN and (self.sheep & (1 << clicked_idx)):
                    self.selected_idx = clicked_idx
            else:
                if self.selected_idx == clicked_idx:
                    self.selected_idx = None
                else:
                    legal_targets = self.get_legal_human_moves(self.selected_idx)
                    if clicked_idx in legal_targets:
                        # 【修改】人类合法走棋后，直接执行，后续的 AI query 自然会更新评价
                        self.execute_move(self.selected_idx, clicked_idx)
                    else:
                        if self.turn == WOLF_TURN and (self.wolves & (1 << clicked_idx)):
                            self.selected_idx = clicked_idx
                        elif self.turn == SHEEP_TURN and (self.sheep & (1 << clicked_idx)):
                            self.selected_idx = clicked_idx
                        else:
                            self.selected_idx = None 
                            
            self.update_board_ui()

    def update_board_ui(self):
        self.canvas.delete("piece") 
        
        r = 20
        
        # 【新增特效】画出上一步的蓝色追踪残影！
        if self.last_move is not None:
            f, t = self.last_move
            fx = self.margin + (f % 5) * self.cell
            fy = self.margin + (f // 5) * self.cell
            tx = self.margin + (t % 5) * self.cell
            ty = self.margin + (t // 5) * self.cell
            
            # 画一个粗壮的蓝色箭头
            self.canvas.create_line(fx, fy, tx, ty, fill="#2196F3", width=6, arrow=tk.LAST, tags="piece")
            # 用蓝色高亮目的地
            self.canvas.create_oval(tx-r-5, ty-r-5, tx+r+5, ty+r+5, outline="#2196F3", width=3, tags="piece")
        
        for i in range(25):
            cx = self.margin + (i % 5) * self.cell
            cy = self.margin + (i // 5) * self.cell
            
            if self.selected_idx == i:
                self.canvas.create_oval(cx-r-5, cy-r-5, cx+r+5, cy+r+5, outline="#00FF00", width=3, tags="piece")
                legal_moves = self.get_legal_human_moves(i)
                for tgt in legal_moves:
                    tx = self.margin + (tgt % 5) * self.cell
                    ty = self.margin + (tgt // 5) * self.cell
                    self.canvas.create_oval(tx-10, ty-10, tx+10, ty+10, fill="#00FF00", tags="piece")

            if self.wolves & (1 << i):
                self.canvas.create_oval(cx-r, cy-r, cx+r, cy+r, fill="#D32F2F", tags="piece")
                self.canvas.create_text(cx, cy, text="狼", fill="white", font=("微软雅黑", 14, "bold"), tags="piece")
            elif self.sheep & (1 << i):
                self.canvas.create_oval(cx-r, cy-r, cx+r, cy+r, fill="#FFF8DC", outline="#8B4513", tags="piece")
                self.canvas.create_text(cx, cy, text="羊", fill="#8B4513", font=("微软雅黑", 14, "bold"), tags="piece")

    def __del__(self):
        if hasattr(self, 'engine'):
            self.engine.terminate()

if __name__ == "__main__":
    root = tk.Tk()
    game = WolfSheepGameGUI(root)
    root.mainloop()
