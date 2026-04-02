#include <iostream>
#include <vector>
#include <cstdint>
#include <fstream>
#include <string>
#include <algorithm>

// --- 游戏规则常量 ---
const uint32_t INITIAL_SHEEP = (1U << 15) - 1;
const uint32_t INITIAL_WOLVES = (1U << 21) | (1U << 22) | (1U << 23);
const int WOLF_TURN = 0;
const int SHEEP_TURN = 1;

uint32_t C[30][30];
std::vector<int> ADJACENT[25];
struct JumpTarget { int dist1; int dist2; };
std::vector<JumpTarget> JUMP[25];

void init_globals() {
    for (int i = 0; i < 30; ++i) {
        C[i][0] = 1;
        for (int j = 1; j <= i; ++j) C[i][j] = C[i - 1][j - 1] + C[i - 1][j];
    }
    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 5; ++c) {
            int i = r * 5 + c;
            int drs[] = {-1, 1, 0, 0}, dcs[] = {0, 0, -1, 1};
            for (int d = 0; d < 4; ++d) {
                int nr = r + drs[d], nc = c + dcs[d];
                if (nr >= 0 && nr < 5 && nc >= 0 && nc < 5) {
                    ADJACENT[i].push_back(nr * 5 + nc);
                    int jr = r + 2 * drs[d], jc = c + 2 * dcs[d];
                    if (jr >= 0 && jr < 5 && jc >= 0 && jc < 5) {
                        JUMP[i].push_back({nr * 5 + nc, jr * 5 + jc});
                    }
                }
            }
        }
    }
}

uint32_t get_rel_sheep(uint32_t sheep, uint32_t wolves) {
    uint32_t rel = 0; int slot = 0;
    uint32_t empty = (~wolves) & ((1U << 25) - 1);
    while (empty) {
        int b = __builtin_ctz(empty);
        if (sheep & (1U << b)) rel |= (1U << slot);
        slot++; empty &= empty - 1;
    }
    return rel;
}

uint32_t encode_idx(uint32_t wolves, uint32_t sheep, int k) {
    uint32_t res_w = 0, res_s = 0; 
    int i_w = 1, i_s = 1;
    uint32_t w_mask = wolves, s_mask = get_rel_sheep(sheep, wolves);
    while (w_mask) { res_w += C[__builtin_ctz(w_mask)][i_w++]; w_mask &= w_mask - 1; }
    while (s_mask) { res_s += C[__builtin_ctz(s_mask)][i_s++]; s_mask &= s_mask - 1; }
    return res_w * C[22][k] + res_s;
}

std::vector<int8_t> tb_wolf_k, tb_sheep_k;
std::vector<int8_t> tb_wolf_k_minus_1, tb_sheep_k_minus_1;
int current_loaded_k = -1;

void load_layer_from_disk(int k) {
    if (k <= 3 || k == current_loaded_k) return; 
    
    auto read_file = [](std::string filename, std::vector<int8_t>& vec) -> bool {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;
        size_t size = file.tellg();
        if (size == 0) return false;
        vec.resize(size);
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(vec.data()), size);
        return true;
    };

    if (read_file("layer_" + std::to_string(k) + "_wolf.bin", tb_wolf_k) &&
        read_file("layer_" + std::to_string(k) + "_sheep.bin", tb_sheep_k)) {
        
        if (k > 4) {
            read_file("layer_" + std::to_string(k - 1) + "_wolf.bin", tb_wolf_k_minus_1);
            read_file("layer_" + std::to_string(k - 1) + "_sheep.bin", tb_sheep_k_minus_1);
        }
        current_loaded_k = k;
    }
}

int8_t query_tablebase(uint32_t wolves, uint32_t sheep, int k, int turn) {
    if (k <= 3) return 1; 
    uint32_t idx = encode_idx(wolves, sheep, k);
    if (k == current_loaded_k) return turn == WOLF_TURN ? tb_wolf_k[idx] : tb_sheep_k[idx];
    if (k == current_loaded_k - 1) return turn == WOLF_TURN ? tb_wolf_k_minus_1[idx] : tb_sheep_k_minus_1[idx];
    return 0; 
}

int wolf_score(int8_t v) { return v > 0 ? 1000 - v : (v == 0 ? 0 : -1000 - v); }
int sheep_score(int8_t v) { return v < 0 ? 1000 + v : (v == 0 ? 0 : -1000 + v); }

// --- 新增：计算某个盘面下，当前走棋方有多少种“必死”的走法（陷阱） ---
int count_traps(uint32_t wolves, uint32_t sheep, int turn, int k) {
    int traps = 0;
    if (turn == WOLF_TURN) {
        for (int i = 0; i < 25; ++i) {
            if (wolves & (1U << i)) {
                for (const auto& tg : JUMP[i]) {
                    if (!((wolves | sheep) & (1U << tg.dist1)) && (sheep & (1U << tg.dist2))) {
                        uint32_t nw = wolves ^ (1U << i) ^ (1U << tg.dist2);
                        uint32_t ns = sheep ^ (1U << tg.dist2);
                        // 如果狼走了这步，导致评价变成了 <0 (羊必胜)，说明这是一个陷阱
                        if (query_tablebase(nw, ns, k - 1, SHEEP_TURN) < 0) traps++;
                    }
                }
                for (int n : ADJACENT[i]) {
                    if (!((wolves | sheep) & (1U << n))) {
                        uint32_t nw = wolves ^ (1U << i) ^ (1U << n);
                        if (query_tablebase(nw, sheep, k, SHEEP_TURN) < 0) traps++;
                    }
                }
            }
        }
    } else {
        for (int i = 0; i < 25; ++i) {
            if (sheep & (1U << i)) {
                for (int n : ADJACENT[i]) {
                    if (!((wolves | sheep) & (1U << n))) {
                        uint32_t ns = sheep ^ (1U << i) ^ (1U << n);
                        // 如果羊走了这步，评价变成 >0 (狼必胜)，说明是个陷阱
                        if (query_tablebase(wolves, ns, k, WOLF_TURN) > 0) traps++;
                    }
                }
            }
        }
    }
    return traps;
}

// 扩展 Move 结构体，增加 trap_count
struct Move { uint32_t nw, ns; int from, to, captured, eval_score; int8_t raw_val; int trap_count; };

std::vector<Move> get_legal_moves(uint32_t wolves, uint32_t sheep, int turn, int k) {
    std::vector<Move> moves;
    if (turn == WOLF_TURN) {
        for (int i = 0; i < 25; ++i) {
            if (wolves & (1U << i)) {
                for (const auto& tg : JUMP[i]) {
                    if (!((wolves | sheep) & (1U << tg.dist1)) && (sheep & (1U << tg.dist2))) {
                        uint32_t nw = wolves ^ (1U << i) ^ (1U << tg.dist2);
                        uint32_t ns = sheep ^ (1U << tg.dist2);
                        int8_t val = query_tablebase(nw, ns, k - 1, SHEEP_TURN);
                        // 如果是平局，计算一下给对手挖了多少坑
                        int traps = (val == 0) ? count_traps(nw, ns, SHEEP_TURN, k - 1) : 0;
                        moves.push_back({nw, ns, i, tg.dist2, tg.dist2, wolf_score(val), val, traps});
                    }
                }
                for (int n : ADJACENT[i]) {
                    if (!((wolves | sheep) & (1U << n))) {
                        uint32_t nw = wolves ^ (1U << i) ^ (1U << n);
                        int8_t val = query_tablebase(nw, sheep, k, SHEEP_TURN);
                        int traps = (val == 0) ? count_traps(nw, sheep, SHEEP_TURN, k) : 0;
                        moves.push_back({nw, sheep, i, n, -1, wolf_score(val), val, traps});
                    }
                }
            }
        }
    } else {
        for (int i = 0; i < 25; ++i) {
            if (sheep & (1U << i)) {
                for (int n : ADJACENT[i]) {
                    if (!((wolves | sheep) & (1U << n))) {
                        uint32_t ns = sheep ^ (1U << i) ^ (1U << n);
                        int8_t val = query_tablebase(wolves, ns, k, WOLF_TURN);
                        int traps = (val == 0) ? count_traps(wolves, ns, WOLF_TURN, k) : 0;
                        moves.push_back({wolves, ns, i, n, -1, sheep_score(val), val, traps});
                    }
                }
            }
        }
    }
    
    // 【终极心机排序逻辑】
    std::sort(moves.begin(), moves.end(), [](const Move& a, const Move& b){ 
        if (a.eval_score != b.eval_score) {
            return a.eval_score > b.eval_score; // 第一优先级：确保不输（分数高）
        }
        // 第二优先级：如果分数一样（比如都是平局 0），挑选给对手制造陷阱最多的那步棋！
        return a.trap_count > b.trap_count; 
    });
    return moves;
}

int main() {
    // 禁用标准库同步以稍微提高 I/O 速度，虽然在此场景影响不大
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    init_globals();
    uint32_t wolves, sheep;
    int turn, k;

    // 循环监听 Python 的输入
    while (std::cin >> wolves >> sheep >> turn >> k) {
        load_layer_from_disk(k); 
        auto moves = get_legal_moves(wolves, sheep, turn, k);
        
        if (moves.empty()) {
            std::cout << "NOMOVE" << std::endl; // std::endl 会自动刷新缓冲区
        } else {
            // 输出格式：FROM TO EVAL
            std::cout << moves[0].from << " " << moves[0].to << " " << (int)moves[0].raw_val << std::endl;
        }
    }
    return 0;
}
