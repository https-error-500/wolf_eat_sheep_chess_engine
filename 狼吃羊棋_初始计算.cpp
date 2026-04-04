#include <iostream>
#include <vector>
#include <cstdint>
#include <thread>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <string>

// --- 游戏规则与组合数学常量 ---
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

uint32_t colex(uint32_t mask, int k) {
    uint32_t res = 0; int i = 1;
    while (mask) {
        int b = __builtin_ctz(mask);
        res += C[b][i++]; mask &= mask - 1;
    }
    return res;
}

uint32_t encode_idx(uint32_t wolves, uint32_t sheep, int k) {
    return colex(wolves, 3) * C[22][k] + colex(get_rel_sheep(sheep, wolves), k);
}

void decode_idx(uint32_t idx, int k, uint32_t& wolves, uint32_t& sheep) {
    uint32_t w_idx = idx / C[22][k]; uint32_t s_idx = idx % C[22][k];
    wolves = 0; int w_k = 3;
    for (int i = 24; i >= 0 && w_k > 0; --i) {
        if (w_idx >= C[i][w_k]) { w_idx -= C[i][w_k]; wolves |= (1U << i); w_k--; }
    }
    uint32_t rel_sheep = 0; int s_k = k;
    for (int i = 21; i >= 0 && s_k > 0; --i) {
        if (s_idx >= C[i][s_k]) { s_idx -= C[i][s_k]; rel_sheep |= (1U << i); s_k--; }
    }
    sheep = 0; int slot = 0;
    uint32_t empty = (~wolves) & ((1U << 25) - 1);
    while (empty) {
        int b = __builtin_ctz(empty);
        if (rel_sheep & (1U << slot)) sheep |= (1U << b);
        slot++; empty &= empty - 1;
    }
}

// 【彻底修复】: 现在能够正确读取对手阵营的盘面估值！
inline int8_t get_child_val(uint32_t nw, uint32_t ns, int n_k, int curr_k,
                            const std::vector<std::vector<int8_t>>& curr_tabs,
                            const std::vector<std::vector<int8_t>>& prev_tabs, int target_turn) {
    if (n_k <= 3) return 1; 
    uint32_t idx = encode_idx(nw, ns, n_k);
    return (n_k == curr_k) ? curr_tabs[target_turn][idx] : prev_tabs[target_turn][idx];
}

int8_t update_state(uint32_t idx, int turn, int k, 
                    const std::vector<std::vector<int8_t>>& curr_tabs, 
                    const std::vector<std::vector<int8_t>>& prev_tabs) {
    uint32_t wolves, sheep;
    decode_idx(idx, k, wolves, sheep);
    
    bool has_moves = false; bool all_loss = true;
    
    if (turn == WOLF_TURN) {
        int8_t best_win = 126, worst_loss = 0;
        for (int i = 0; i < 25; ++i) {
            if (wolves & (1U << i)) {
                // 新吃子规则：狼隔空吃羊，落在羊身上
                for (const auto& tg : JUMP[i]) {
                    if (!((wolves | sheep) & (1U << tg.dist1)) && (sheep & (1U << tg.dist2))) {
                        has_moves = true;
                        uint32_t nw = wolves ^ (1U << i) ^ (1U << tg.dist2);
                        uint32_t ns = sheep ^ (1U << tg.dist2);
                        int8_t v = get_child_val(nw, ns, k - 1, k, curr_tabs, prev_tabs, SHEEP_TURN);
                        if (v > 0) { best_win = std::min(best_win, v); all_loss = false; }
                        else if (v == 0) { all_loss = false; }
                        else { worst_loss = std::min(worst_loss, v); }
                    }
                }
                for (int n : ADJACENT[i]) {
                    if (!((wolves | sheep) & (1U << n))) {
                        has_moves = true;
                        int8_t v = get_child_val(wolves ^ (1U << i) ^ (1U << n), sheep, k, k, curr_tabs, prev_tabs, SHEEP_TURN);
                        if (v > 0) { best_win = std::min(best_win, v); all_loss = false; }
                        else if (v == 0) { all_loss = false; }
                        else { worst_loss = std::min(worst_loss, v); }
                    }
                }
            }
        }
        if (!has_moves) return -1;
        if (best_win != 126) return (best_win < 125) ? best_win + 1 : 125;
        if (all_loss) return (worst_loss > -125) ? worst_loss - 1 : -125;
        return 0;
    } else {
        int8_t best_win = -126, worst_loss = 0;
        for (int i = 0; i < 25; ++i) {
            if (sheep & (1U << i)) {
                for (int n : ADJACENT[i]) {
                    if (!((wolves | sheep) & (1U << n))) {
                        has_moves = true;
                        int8_t v = get_child_val(wolves, sheep ^ (1U << i) ^ (1U << n), k, k, curr_tabs, prev_tabs, WOLF_TURN);
                        if (v < 0) { best_win = std::max(best_win, v); all_loss = false; }
                        else if (v == 0) { all_loss = false; }
                        else { worst_loss = std::max(worst_loss, v); }
                    }
                }
            }
        }
        if (!has_moves) return 1;
        if (best_win != -126) return (best_win > -125) ? best_win - 1 : -125;
        if (all_loss) return (worst_loss < 125) ? worst_loss + 1 : 125;
        return 0;
    }
}

void worker_thread(uint32_t start, uint32_t end, int turn, int k, 
                   std::vector<int8_t>& thread_curr_t, 
                   const std::vector<std::vector<int8_t>>& curr_tabs, 
                   const std::vector<std::vector<int8_t>>& prev_tabs, 
                   std::atomic<bool>& global_changed) {
    bool local_changed = false;
    for (uint32_t idx = start; idx < end; ++idx) {
        int8_t old_v = thread_curr_t[idx];
        int8_t new_v = update_state(idx, turn, k, curr_tabs, prev_tabs);
        if (old_v != new_v) { thread_curr_t[idx] = new_v; local_changed = true; }
    }
    if (local_changed) global_changed.store(true, std::memory_order_relaxed);
}

bool load_layer(int k, std::vector<int8_t>& curr_w, std::vector<int8_t>& curr_s, uint32_t expected_size) {
    std::string wf = "layer_" + std::to_string(k) + "_wolf.bin";
    std::string sf = "layer_" + std::to_string(k) + "_sheep.bin";
    std::ifstream fw(wf, std::ios::binary | std::ios::ate);
    std::ifstream fs(sf, std::ios::binary | std::ios::ate);
    if (!fw.is_open() || !fs.is_open()) return false;
    if (fw.tellg() != expected_size || fs.tellg() != expected_size) return false;
    fw.seekg(0, std::ios::beg); fs.seekg(0, std::ios::beg);
    curr_w.resize(expected_size); curr_s.resize(expected_size);
    fw.read(reinterpret_cast<char*>(curr_w.data()), expected_size);
    fs.read(reinterpret_cast<char*>(curr_s.data()), expected_size);
    return true;
}

void save_layer(int k, const std::vector<int8_t>& curr_w, const std::vector<int8_t>& curr_s) {
    std::string wf = "layer_" + std::to_string(k) + "_wolf.bin";
    std::string sf = "layer_" + std::to_string(k) + "_sheep.bin";
    std::ofstream fw(wf, std::ios::binary); std::ofstream fs(sf, std::ios::binary);
    fw.write(reinterpret_cast<const char*>(curr_w.data()), curr_w.size());
    fs.write(reinterpret_cast<const char*>(curr_s.data()), curr_s.size());
}

int main() {
    init_globals();
    std::cout << "========= [完美修复版] 终极逆向分析引擎 =========" << std::endl;
    std::cout << "当前规则：狼隔一个空格吃羊（落在羊的位置上）" << std::endl;
    
    std::vector<std::vector<int8_t>> prev_tables(2); 
    int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;

    for (int k = 1; k <= 15; ++k) {
        uint32_t num_states = C[25][3] * C[22][k];
        std::cout << "\n[检查图层 " << k << "] 状态数: " << num_states << " ... ";
        std::vector<std::vector<int8_t>> curr_tables(2);
        
        if (load_layer(k, curr_tables[WOLF_TURN], curr_tables[SHEEP_TURN], num_states)) {
            std::cout << "发现缓存，秒级加载！" << std::endl;
            prev_tables = std::move(curr_tables); 
            continue;
        }
        
        std::cout << "未发现缓存，开始推演..." << std::endl;
        curr_tables[WOLF_TURN].assign(num_states, 0);
        curr_tables[SHEEP_TURN].assign(num_states, 0);
        
        auto start_time = std::chrono::high_resolution_clock::now();
        int iteration = 0; std::atomic<bool> changed(true);
        
        while (changed.load()) {
            changed.store(false); iteration++;
            for (int turn : {WOLF_TURN, SHEEP_TURN}) {
                std::vector<std::thread> threads;
                uint32_t chunk_size = (num_states + num_threads - 1) / num_threads;
                for (int t = 0; t < num_threads; ++t) {
                    uint32_t start = t * chunk_size;
                    uint32_t end = std::min(start + chunk_size, num_states);
                    if (start < end) {
                        // 修正：将整个 curr_tables 传进去供跨边读取
                        threads.emplace_back(worker_thread, start, end, turn, k, 
                                             std::ref(curr_tables[turn]), 
                                             std::cref(curr_tables), 
                                             std::cref(prev_tables), 
                                             std::ref(changed));
                    }
                }
                for (auto& th : threads) th.join();
            }
            std::cout << "  -> 迭代 " << iteration << " 完成... \r" << std::flush;
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        std::cout << "\n图层 " << k << " 推演收敛! 耗时: " << std::chrono::duration<double>(end_time - start_time).count() << " 秒." << std::endl;
        save_layer(k, curr_tables[WOLF_TURN], curr_tables[SHEEP_TURN]);
        prev_tables = std::move(curr_tables); 
    }

    uint32_t root_idx = encode_idx(INITIAL_WOLVES, INITIAL_SHEEP, 15);
    int8_t final_score = prev_tables[WOLF_TURN][root_idx]; 
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "最终数学绝对判决：" << std::endl;
    if (final_score > 0) std::cout << ">>> 狼方必胜！最快可在 " << (int)final_score << " 步内吃光羊！" << std::endl;
    else if (final_score < 0) std::cout << ">>> 羊方必胜！只要不犯错，最慢可在 " << -(int)final_score << " 步内将狼彻底围堵致死！" << std::endl;
    else std::cout << ">>> 平局！" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
