#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define EXPORTED EMSCRIPTEN_KEEPALIVE
#else
#define EXPORTED
#endif

namespace qtl2048 {

using Board = uint64_t;

enum Move : int {
    MOVE_LEFT = 0,
    MOVE_RIGHT = 1,
    MOVE_UP = 2,
    MOVE_DOWN = 3,
    MOVE_NONE = -1,
};

constexpr int kSize = 4;
constexpr int kCells = 16;
constexpr uint8_t kMaxExponent = 12;   // preserve current variant semantics
constexpr double kSpawnProb1 = 0.9;
constexpr int kDefaultTimeLimitMs = 70;
constexpr int kDefaultMaxDepth = 8;
constexpr double kDefaultScoreWeight = 1.0;
constexpr double kDefaultMovePenalty = 5000.0;

struct RowMove {
    uint16_t row = 0;
    bool moved = false;
    int mergeScore = 0;
};

struct AnalysisResult {
    int bestMove = MOVE_NONE;
    int depthCompleted = 0;
    uint64_t nodes = 0;
    uint64_t cacheHits = 0;
    bool timedOut = false;
    std::array<double, 4> moveScores{{-std::numeric_limits<double>::infinity(),
                                      -std::numeric_limits<double>::infinity(),
                                      -std::numeric_limits<double>::infinity(),
                                      -std::numeric_limits<double>::infinity()}};
    std::array<bool, 4> legal{{false, false, false, false}};
};

struct SearchConfig {
    int maxDepth = kDefaultMaxDepth;
    int timeLimitMs = kDefaultTimeLimitMs;
    double scoreWeight = kDefaultScoreWeight;
    double movePenalty = kDefaultMovePenalty;
};

struct TTKey {
    Board board{};
    uint8_t depth{};
    uint8_t nodeType{}; // 0=max, 1=chance

    bool operator==(const TTKey& other) const {
        return board == other.board && depth == other.depth && nodeType == other.nodeType;
    }
};

struct TTKeyHash {
    std::size_t operator()(const TTKey& k) const noexcept {
        std::size_t h1 = std::hash<uint64_t>{}(k.board);
        std::size_t h2 = std::hash<uint16_t>{}(static_cast<uint16_t>((k.depth << 8) | k.nodeType));
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6U) + (h1 >> 2U));
    }
};

struct TTEntry {
    double value = 0.0;
};

static std::array<RowMove, 65536> g_rowLeft;
static std::array<RowMove, 65536> g_rowRight;
static bool g_tablesReady = false;

inline uint8_t getCell(Board b, int idx) {
    return static_cast<uint8_t>((b >> (idx * 4)) & 0xFULL);
}

inline Board setCell(Board b, int idx, uint8_t value) {
    const Board mask = ~(Board{0xFULL} << (idx * 4));
    return (b & mask) | (static_cast<Board>(value & 0xF) << (idx * 4));
}

inline uint16_t getRow(Board b, int r) {
    return static_cast<uint16_t>((b >> (r * 16)) & 0xFFFFULL);
}

inline Board setRow(Board b, int r, uint16_t row) {
    const Board mask = ~(Board{0xFFFFULL} << (r * 16));
    return (b & mask) | (static_cast<Board>(row) << (r * 16));
}

std::array<uint8_t, 4> unpackRow(uint16_t row) {
    return {static_cast<uint8_t>(row & 0xF),
            static_cast<uint8_t>((row >> 4) & 0xF),
            static_cast<uint8_t>((row >> 8) & 0xF),
            static_cast<uint8_t>((row >> 12) & 0xF)};
}

uint16_t packRow(const std::array<uint8_t, 4>& cells) {
    return static_cast<uint16_t>(cells[0] | (cells[1] << 4) | (cells[2] << 8) | (cells[3] << 12));
}

uint16_t reverseRow(uint16_t row) {
    auto c = unpackRow(row);
    std::reverse(c.begin(), c.end());
    return packRow(c);
}

RowMove computeRowMoveLeft(uint16_t row) {
    auto cells = unpackRow(row);
    std::array<uint8_t, 4> compact{};
    int n = 0;
    for (uint8_t c : cells) {
        if (c != 0) compact[n++] = c;
    }

    std::array<uint8_t, 4> merged{};
    int out = 0;
    int score = 0;
    for (int i = 0; i < n; ++i) {
        uint8_t v = compact[i];
        if (i + 1 < n && compact[i + 1] == v && v < kMaxExponent) {
            ++v;
            ++i;
            score += (1 << v);
        }
        merged[out++] = v;
    }

    RowMove rm;
    rm.row = packRow(merged);
    rm.moved = rm.row != row;
    rm.mergeScore = score;
    return rm;
}

void initTables() {
    if (g_tablesReady) return;
    for (int row = 0; row < 65536; ++row) {
        g_rowLeft[row] = computeRowMoveLeft(static_cast<uint16_t>(row));
        uint16_t rev = reverseRow(static_cast<uint16_t>(row));
        RowMove rr = computeRowMoveLeft(rev);
        rr.row = reverseRow(rr.row);
        rr.moved = rr.row != static_cast<uint16_t>(row);
        g_rowRight[row] = rr;
    }
    g_tablesReady = true;
}

Board packBoard(const std::array<uint8_t, kCells>& cells) {
    Board b = 0;
    for (int i = 0; i < kCells; ++i) {
        b |= static_cast<Board>(cells[i] & 0xF) << (i * 4);
    }
    return b;
}

std::array<uint8_t, kCells> unpackBoard(Board b) {
    std::array<uint8_t, kCells> out{};
    for (int i = 0; i < kCells; ++i) out[i] = getCell(b, i);
    return out;
}

Board transpose(Board b) {
    Board t = 0;
    for (int r = 0; r < kSize; ++r) {
        for (int c = 0; c < kSize; ++c) {
            t = setCell(t, c * 4 + r, getCell(b, r * 4 + c));
        }
    }
    return t;
}

struct MoveResult {
    Board board = 0;
    bool moved = false;
    int mergeScore = 0;
};

MoveResult applyMove(Board b, Move mv) {
    initTables();
    MoveResult res;
    switch (mv) {
        case MOVE_LEFT: {
            Board nb = b;
            for (int r = 0; r < 4; ++r) {
                auto rm = g_rowLeft[getRow(b, r)];
                nb = setRow(nb, r, rm.row);
                res.moved = res.moved || rm.moved;
                res.mergeScore += rm.mergeScore;
            }
            res.board = nb;
            break;
        }
        case MOVE_RIGHT: {
            Board nb = b;
            for (int r = 0; r < 4; ++r) {
                auto rm = g_rowRight[getRow(b, r)];
                nb = setRow(nb, r, rm.row);
                res.moved = res.moved || rm.moved;
                res.mergeScore += rm.mergeScore;
            }
            res.board = nb;
            break;
        }
        case MOVE_UP: {
            Board t = transpose(b);
            Board nt = t;
            for (int r = 0; r < 4; ++r) {
                auto rm = g_rowLeft[getRow(t, r)];
                nt = setRow(nt, r, rm.row);
                res.moved = res.moved || rm.moved;
                res.mergeScore += rm.mergeScore;
            }
            res.board = transpose(nt);
            break;
        }
        case MOVE_DOWN: {
            Board t = transpose(b);
            Board nt = t;
            for (int r = 0; r < 4; ++r) {
                auto rm = g_rowRight[getRow(t, r)];
                nt = setRow(nt, r, rm.row);
                res.moved = res.moved || rm.moved;
                res.mergeScore += rm.mergeScore;
            }
            res.board = transpose(nt);
            break;
        }
        default:
            res.board = b;
            break;
    }
    return res;
}

bool isTerminal(Board b) {
    if (~b != 0ULL) {
        for (int i = 0; i < kCells; ++i) {
            if (getCell(b, i) == 0) return false;
        }
    }
    for (Move mv : {MOVE_LEFT, MOVE_RIGHT, MOVE_UP, MOVE_DOWN}) {
        if (applyMove(b, mv).moved) return false;
    }
    return true;
}

int countEmpty(Board b) {
    int count = 0;
    for (int i = 0; i < kCells; ++i) count += (getCell(b, i) == 0);
    return count;
}

int maxExponent(Board b) {
    int mx = 0;
    for (int i = 0; i < kCells; ++i) mx = std::max<int>(mx, getCell(b, i));
    return mx;
}

std::vector<int> emptyIndices(Board b) {
    std::vector<int> idx;
    idx.reserve(16);
    for (int i = 0; i < kCells; ++i) if (getCell(b, i) == 0) idx.push_back(i);
    return idx;
}

std::array<std::array<double, 16>, 8> makePatterns() {
    std::array<std::array<double, 16>, 8> patterns{};
    const std::array<double, 16> base = {
        65536, 32768, 16384, 8192,
        512,   1024,  2048,  4096,
        256,   128,   64,    32,
        2,     4,     8,     16};

    auto idx = [](int r, int c) { return r * 4 + c; };
    std::array<double, 16> p0{};
    for (int i = 0; i < 16; ++i) p0[i] = base[i];
    patterns[0] = p0;

    std::array<double, 16> p1{};
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) p1[idx(r, c)] = p0[idx(r, 3 - c)];
    patterns[1] = p1;

    std::array<double, 16> p2{};
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) p2[idx(r, c)] = p0[idx(3 - r, c)];
    patterns[2] = p2;

    std::array<double, 16> p3{};
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) p3[idx(r, c)] = p0[idx(3 - r, 3 - c)];
    patterns[3] = p3;

    std::array<double, 16> p4{};
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) p4[idx(r, c)] = p0[idx(c, r)];
    patterns[4] = p4;

    std::array<double, 16> p5{};
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) p5[idx(r, c)] = p0[idx(c, 3 - r)];
    patterns[5] = p5;

    std::array<double, 16> p6{};
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) p6[idx(r, c)] = p0[idx(3 - c, r)];
    patterns[6] = p6;

    std::array<double, 16> p7{};
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) p7[idx(r, c)] = p0[idx(3 - c, 3 - r)];
    patterns[7] = p7;

    return patterns;
}

const std::array<std::array<double, 16>, 8> kGradientPatterns = makePatterns();

struct Evaluator {
    std::array<double, 13> tileValue{};

    Evaluator() {
        tileValue[0] = 0.0;
        for (int e = 1; e <= 12; ++e) tileValue[e] = static_cast<double>(1 << e);
    }

    double gradient(Board b) const {
        double best = -std::numeric_limits<double>::infinity();
        for (const auto& pattern : kGradientPatterns) {
            double s = 0.0;
            for (int i = 0; i < 16; ++i) {
                const uint8_t e = getCell(b, i);
                if (e) s += pattern[i] * tileValue[e];
            }
            best = std::max(best, s);
        }
        return best;
    }

    double smoothness(Board b) const {
        double penalty = 0.0;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                const uint8_t v = getCell(b, r * 4 + c);
                if (!v) continue;
                if (c + 1 < 4) {
                    const uint8_t w = getCell(b, r * 4 + (c + 1));
                    if (w) penalty += std::abs(int(v) - int(w));
                }
                if (r + 1 < 4) {
                    const uint8_t w = getCell(b, (r + 1) * 4 + c);
                    if (w) penalty += std::abs(int(v) - int(w));
                }
            }
        }
        return -penalty;
    }

    double monotonicity(Board b) const {
        double total = 0.0;
        for (int r = 0; r < 4; ++r) {
            double inc = 0.0, dec = 0.0;
            for (int c = 0; c < 3; ++c) {
                int a = getCell(b, r * 4 + c);
                int d = getCell(b, r * 4 + c + 1);
                if (a > d) dec += a - d;
                else inc += d - a;
            }
            total += std::max(inc, dec);
        }
        for (int c = 0; c < 4; ++c) {
            double inc = 0.0, dec = 0.0;
            for (int r = 0; r < 3; ++r) {
                int a = getCell(b, r * 4 + c);
                int d = getCell(b, (r + 1) * 4 + c);
                if (a > d) dec += a - d;
                else inc += d - a;
            }
            total += std::max(inc, dec);
        }
        return total;
    }

    double mergePotential(Board b) const {
        int count = 0;
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                int idx = r * 4 + c;
                uint8_t v = getCell(b, idx);
                if (!v) continue;
                if (c + 1 < 4 && getCell(b, idx + 1) == v) ++count;
                if (r + 1 < 4 && getCell(b, idx + 4) == v) ++count;
            }
        }
        return static_cast<double>(count);
    }

    double emptyWeight(int empty) const {
        if (empty <= 2) return 500000.0;
        if (empty <= 5) return 250000.0;
        return 100000.0;
    }

    double strictCornerLock(Board b) const {

        int mx = maxExponent(b);

        int pos = 0;
        for (int i = 0; i < 16; ++i)
            if (getCell(b, i) == mx) { pos = i; break; }

        // الركن المثالي: Top-Left (0)
        if (pos == 0)
            return tileValue[mx] * 40.0;   // 👑 locked perfectly

        // باقي الأركان — مسموح لكن أقل
        if (pos == 3 || pos == 12 || pos == 15)
            return tileValue[mx] * 10.0;

        int r = pos / 4, c = pos % 4;

        bool edge = (r == 0 || r == 3 || c == 0 || c == 3);

        if (edge)
            return -tileValue[mx] * 25.0;

        return -tileValue[mx] * 80.0;   // 💀 center = disaster
    }

    double snakeBonus(Board board) const {
        // Dynamic snake evaluation across multiple orientations
        static const int snakes[8][16] = {
            {0,1,2,3,7,6,5,4,8,9,10,11,15,14,13,12},
            {0,4,8,12,13,9,5,1,2,6,10,14,15,11,7,3},

            {3,2,1,0,4,5,6,7,11,10,9,8,12,13,14,15},
            {3,7,11,15,14,10,6,2,1,5,9,13,12,8,4,0},

            {12,13,14,15,11,10,9,8,4,5,6,7,3,2,1,0},
            {12,8,4,0,1,5,9,13,14,10,6,2,3,7,11,15},

            {15,14,13,12,8,9,10,11,7,6,5,4,0,1,2,3},
            {15,11,7,3,2,6,10,14,13,9,5,1,0,4,8,12}
        };

        const int mx = maxExponent(board);
        double best = -std::numeric_limits<double>::infinity();

        for (const auto& snake : snakes) {
            double score = 0.0;

            for (int i = 0; i < 15; ++i) {
                int a = getCell(board, snake[i]);
                int b = getCell(board, snake[i+1]);

                if (a >= b)
                    score += (a - b);
                else
                    score -= (b - a) * 2.0;

                if (a > 0)
                    score += tileValue[a] * (16 - i) * 0.015;
            }

            if (getCell(board, snake[0]) == mx)
                score += tileValue[mx] * 2.0;

            best = std::max(best, score);
        }

        int empty = countEmpty(board);
        double phase = (empty <= 2) ? 1.8 : (empty <= 5 ? 1.3 : 0.8);

        return best * 4000.0 * phase;
    }

    double stabilityScore(Board b) const {

        double score = 0;

        // الصف الأول (Top Row)
        for (int c = 0; c < 3; ++c) {
            int a = getCell(b, c);
            int d = getCell(b, c + 1);

            if (a >= d)
                score += (a - d) * 5;
            else
                score -= (d - a) * 15;
        }

        // العمود الأول
        for (int r = 0; r < 3; ++r) {
            int a = getCell(b, r * 4);
            int d = getCell(b, (r + 1) * 4);

            if (a >= d)
                score += (a - d) * 5;
            else
                score -= (d - a) * 15;
        }

        // عقوبة الفجوات خلف أكبر tile
        int mx = maxExponent(b);
        if (getCell(b, 0) == mx) {
            for (int i = 1; i < 4; ++i)
                if (getCell(b, i) == 0)
                    score -= tileValue[mx] * 5;
        }

        return score * 6000.0;
    }

    double endgameScore(Board b) const {
        int empty = countEmpty(b);
        if (empty > 2) return 0.0;

        double score = 0;

        // مكافأة الدمج المتاح
        score += mergePotential(b) * 50000.0;

        // عقوبة عدم وجود دمج
        if (mergePotential(b) == 0)
            score -= 500000.0;

        // مكافأة وجود مساحة حتى لو صغيرة
        score += empty * 200000.0;

        // عقوبة إذا أكبر tile ليس في الركن
        int mx = maxExponent(b);
        if (getCell(b, 0) != mx)
            score -= tileValue[mx] * 20.0;

        return score;
    }

    double operator()(Board b) const {
        int empty = countEmpty(b);
        int mx = maxExponent(b);

        double score = 0;

        score += empty * emptyWeight(empty);
        score += gradient(b);
        score += smoothness(b) * 15000.0;
        score += monotonicity(b) * 18000.0;
        score += mergePotential(b) * 30000.0;

        score += snakeBonus(b);          // 🐍
        score += strictCornerLock(b);    // 👑
        score += stabilityScore(b);       // ⚖️

        score += tileValue[mx] * 8000.0;

        score += endgameScore(b);        // 🏁

        return score;
    }
};

class Solver {
public:
    explicit Solver(SearchConfig cfg)
        : cfg_(cfg) {}

    AnalysisResult analyze(Board board) {
        tt_.clear();
        nodes_ = 0;
        cacheHits_ = 0;
        timedOut_ = false;
        deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg_.timeLimitMs);

        AnalysisResult best;
        best.bestMove = MOVE_NONE;

        int targetDepth = std::min(cfg_.maxDepth, recommendedDepth(board));
        if (cfg_.timeLimitMs <= 0) targetDepth = std::min(cfg_.maxDepth, recommendedDepth(board));

        for (int depth = 1; depth <= targetDepth; ++depth) {
            AnalysisResult candidate;
            candidate.depthCompleted = depth;
            bool complete = true;

            struct RootMoveScore {
                Move move;
                double orderScore;
                MoveResult result;
            };
            std::vector<RootMoveScore> ordered;
            for (Move mv : {MOVE_LEFT, MOVE_RIGHT, MOVE_UP, MOVE_DOWN}) {
                MoveResult mr = applyMove(board, mv);
                if (!mr.moved) continue;
                ordered.push_back({mv, evaluator_(mr.board) + mr.mergeScore * cfg_.scoreWeight, mr});
            }
            std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
                return a.orderScore > b.orderScore;
            });

            if (ordered.empty()) {
                candidate.bestMove = MOVE_NONE;
                best = candidate;
                break;
            }

            double bestScore = -std::numeric_limits<double>::infinity();
            int bestMove = MOVE_NONE;
            for (const auto& root : ordered) {
                candidate.legal[root.move] = true;

                double child = expectimax(root.result.board, depth - 1, false);
                double score =
                    child +
                    static_cast<double>(root.result.mergeScore) * cfg_.scoreWeight -
                    cfg_.movePenalty;

                if (timedOut_) {
                    complete = false;
                    break;
                }

                candidate.moveScores[root.move] = score;
                if (score > bestScore) {
                    bestScore = score;
                    bestMove = root.move;
                }
            }

            if (!complete) break;
            candidate.bestMove = bestMove;
            candidate.nodes = nodes_;
            candidate.cacheHits = cacheHits_;
            candidate.timedOut = false;
            best = candidate;
        }

        best.nodes = nodes_;
        best.cacheHits = cacheHits_;
        best.timedOut = timedOut_;
        return best;
    }

private:
    int recommendedDepth(Board b) const {
        int empty = countEmpty(b);

        if (empty >= 8) return 5;
        if (empty >= 6) return 6;
        if (empty >= 4) return 8;
        if (empty >= 2) return 10;
        return 12;   // Endgame killer 😈
    }

    void checkTimeout() {
        if ((nodes_ & 1023ULL) == 0ULL && cfg_.timeLimitMs > 0) {
            if (std::chrono::steady_clock::now() >= deadline_) timedOut_ = true;
        }
    }

    double expectimax(Board board, int depth, bool chanceNode) {
        ++nodes_;
        checkTimeout();
        if (timedOut_) return evaluator_(board);
        if (depth <= 0 || isTerminal(board)) return evaluator_(board);

        TTKey key{board, static_cast<uint8_t>(depth), static_cast<uint8_t>(chanceNode ? 1 : 0)};
        auto it = tt_.find(key);
        if (it != tt_.end()) {
            ++cacheHits_;
            return it->second.value;
        }

        double value;
        if (!chanceNode) {
            value = -std::numeric_limits<double>::infinity();
            std::array<std::pair<double, MoveResult>, 4> moves{};
            int count = 0;
            for (Move mv : {MOVE_LEFT, MOVE_RIGHT, MOVE_UP, MOVE_DOWN}) {
                MoveResult mr = applyMove(board, mv);
                if (!mr.moved) continue;
                moves[count++] = {evaluator_(mr.board) + mr.mergeScore * cfg_.scoreWeight, mr};
            }
            if (count == 0) {
                value = evaluator_(board);
            } else {
                std::sort(moves.begin(), moves.begin() + count, [](const auto& a, const auto& b) {
                    return a.first > b.first;
                });
                for (int i = 0; i < count; ++i) {
                    double child = expectimax(moves[i].second.board, depth - 1, true);
                    double moveValue =
                        child +
                        static_cast<double>(moves[i].second.mergeScore) * cfg_.scoreWeight -
                        cfg_.movePenalty;
                    value = std::max(value, moveValue);
                    if (timedOut_) break;
                }
            }
        } else {
            auto empties = emptyIndices(board);
            if (empties.empty()) {
                value = evaluator_(board);
            } else {
                value = 0.0;
                const double invEmpty = 1.0 / static_cast<double>(empties.size());
                for (int idx : empties) {
                    Board b1 = setCell(board, idx, 1);
                    Board b2 = setCell(board, idx, 2);
                    value += invEmpty * (kSpawnProb1 * expectimax(b1, depth - 1, false) +
                                         (1.0 - kSpawnProb1) * expectimax(b2, depth - 1, false));
                    if (timedOut_) break;
                }
            }
        }

        if (!timedOut_) tt_[key] = TTEntry{value};
        return value;
    }

    SearchConfig cfg_;
    Evaluator evaluator_{};
    std::unordered_map<TTKey, TTEntry, TTKeyHash> tt_{};
    uint64_t nodes_ = 0;
    uint64_t cacheHits_ = 0;
    bool timedOut_ = false;
    std::chrono::steady_clock::time_point deadline_{};
};

std::string analysisToJson(const AnalysisResult& r) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{";
    oss << "\"bestMove\":" << r.bestMove << ",";
    oss << "\"depthCompleted\":" << r.depthCompleted << ",";
    oss << "\"nodes\":" << r.nodes << ",";
    oss << "\"cacheHits\":" << r.cacheHits << ",";
    oss << "\"timedOut\":" << (r.timedOut ? "true" : "false") << ",";
    oss << "\"moves\":[";
    for (int i = 0; i < 4; ++i) {
        if (i) oss << ',';
        oss << "{";
        oss << "\"move\":" << i << ",";
        oss << "\"legal\":" << (r.legal[i] ? "true" : "false") << ",";
        if (r.legal[i]) oss << "\"score\":" << r.moveScores[i];
        else oss << "\"score\":null";
        oss << '}';
    }
    oss << "]}";
    return oss.str();
}

} // namespace qtl2048

extern "C" {

EXPORTED const char* analyze_board_json(const uint8_t* cells, int maxDepth, int timeLimitMs, double scoreWeight, double movePenalty) {
    static std::string result;
    std::array<uint8_t, qtl2048::kCells> data{};
    for (int i = 0; i < qtl2048::kCells; ++i) data[i] = cells[i] & 0xF;
    qtl2048::Board board = qtl2048::packBoard(data);
    qtl2048::SearchConfig cfg;
    cfg.maxDepth = maxDepth > 0 ? maxDepth : qtl2048::kDefaultMaxDepth;
    cfg.timeLimitMs = timeLimitMs;
    if (std::isfinite(scoreWeight)) cfg.scoreWeight = scoreWeight;
    if (std::isfinite(movePenalty)) cfg.movePenalty = movePenalty;
    qtl2048::Solver solver(cfg);
    result = qtl2048::analysisToJson(solver.analyze(board));
    return result.c_str();
}

}

#ifndef __EMSCRIPTEN__
int main(int argc, char** argv) {
    using namespace qtl2048;
    std::array<uint8_t, kCells> cells{};
    if (argc == 17) {
        for (int i = 0; i < 16; ++i) cells[i] = static_cast<uint8_t>(std::stoi(argv[i + 1]));
    } else {
        for (int i = 0; i < 16; ++i) {
            int v;
            if (!(std::cin >> v)) {
                std::cerr << "Expected 16 cell exponents (0..12).\n";
                return 1;
            }
            cells[i] = static_cast<uint8_t>(v);
        }
    }

    const char* json = analyze_board_json(cells.data(), kDefaultMaxDepth, 0, kDefaultScoreWeight, kDefaultMovePenalty);
    std::cout << json << "\n";
    return 0;
}
#endif
