#include "renderer.h"
#include "timing.h"
#include "Logger.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <algorithm>
#include <array>
#include "TaskSystem.h"
#ifdef PICCONV_USE_AVX2
  #include <immintrin.h>
#endif
// IVDEP 宏：提示编译器进行 vectorization（向量化）
#ifndef IVDEP
  #if defined(__INTEL_COMPILER)
    #define IVDEP _Pragma("ivdep")
  #elif defined(__GNUC__) || defined(__clang__)
    #define IVDEP _Pragma("GCC ivdep")
  #elif defined(_MSC_VER)
    #define IVDEP __pragma(loop(ivdep))
  #else
    #define IVDEP
  #endif
#endif
static inline std::string codepoint_to_utf8(int code) {
    std::string s;
    if (code <= 0x7F) s.push_back((char)code);
    else if (code <= 0x7FF) {
        s.push_back((char)(0xC0 | ((code >> 6) & 0x1F)));
        s.push_back((char)(0x80 | (code & 0x3F)));
    } else if (code <= 0xFFFF) {
        s.push_back((char)(0xE0 | ((code >> 12) & 0x0F)));
        s.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
        s.push_back((char)(0x80 | (code & 0x3F)));
    } else {
        s.push_back((char)(0xF0 | ((code >> 18) & 0x07)));
        s.push_back((char)(0x80 | ((code >> 12) & 0x3F)));
        s.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
        s.push_back((char)(0x80 | (code & 0x3F)));
    }
    return s;
}

// ANSI 颜色辅助
static inline std::string fg_rgb(int r, int g, int b) {
    std::ostringstream ss;
    ss << "\x1b[38;2;" << r << ";" << g << ";" << b << "m";
    return ss.str();
}

// Perceived luminance（渲染器使用）
static inline double rgb_to_luminance(int r, int g, int b) {
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

static inline std::string reset() { return "\x1b[0m"; }

// 共享字形表
static const std::vector<std::string> SHADING_CHARS = {u8" ", u8"░", u8"▒", u8"▓", u8"█"};
static const std::vector<std::string> BLOCKS_ELEMS = {u8" ", u8"▏", u8"▎", u8"▍", u8"▌", u8"▋", u8"▊", u8"▉", u8"█"};

Charset charset_from_string(const std::string &s) {
    std::string lower = s;
    for (char &c : lower) c = (char)std::tolower((unsigned char)c);
    if (lower=="high") return Charset::high;
    return Charset::low; // 默认
}



// 辅助：背景 ANSI
static inline std::string bg_rgb(int r, int g, int b) {
    std::ostringstream ss;
    ss << "\x1b[48;2;" << r << ";" << g << ";" << b << "m";
    return ss.str();
}

// Low 渲染器：仅背景映射。highres 应采样为 out_w*8 × out_h*8
std::string render_low(const BlockPlanes &highres, int out_w, int out_h) {
    std::ostringstream out;
    int high_w = highres.width;
    int high_h = highres.height;
    for (int by=0; by<out_h; ++by) {
        int prev_br = -1, prev_bg = -1, prev_bb = -1;
        for (int bx=0; bx<out_w; ++bx) {
            long long rsum=0, gsum=0, bsum=0; int count=0;
            for (int dy=0; dy<8; ++dy) {
                for (int dx=0; dx<8; ++dx) {
                    int sx = bx*8 + dx; int sy = by*8 + dy;
                    size_t idx = (size_t)sy * high_w + sx;
                    rsum += highres.r[idx];
                    gsum += highres.g[idx];
                    bsum += highres.b[idx];
                    ++count;
                }
            }
            int br = (int)(rsum / count); int bg = (int)(gsum / count); int bb = (int)(bsum / count);
            if (br != prev_br || bg != prev_bg || bb != prev_bb) {
                out << bg_rgb(br,bg,bb);
                prev_br = br; prev_bg = bg; prev_bb = bb;
            }
            out << " ";
        }
        out << reset() << '\n';
    }
    return out.str();
}

// High 渲染器：构建基于 mask 的字形集合，并选择能最小化像素误差的字形与 fg/bg 颜色
// 已优化：在 highres_blocks 上使用积分并按行并行化
std::string render_high(const BlockPlanes &highres, int out_w, int out_h, PicConvertor::TaskSystem &pool, int prune_threshold, PruneStats* stats, bool measure_only) {
    const int SUB_W = 8, SUB_H = 8;
    std::ostringstream out;
    int high_w = highres.width;
    int high_h = highres.height;

    // 构建对 highres_blocks 的积分和及平方和（尺寸 (high_w+1)*(high_h+1)）
    std::vector<uint64_t> sumR((high_w+1)*(high_h+1)), sumG((high_w+1)*(high_h+1)), sumB((high_w+1)*(high_h+1));
    std::vector<uint64_t> sumR2((high_w+1)*(high_h+1)), sumG2((high_w+1)*(high_h+1)), sumB2((high_w+1)*(high_h+1));
    Stopwatch sw_integral;
    for (int y=0;y<high_h;++y) {
        uint64_t rowR=0,rowG=0,rowB=0;
        uint64_t rowR2=0,rowG2=0,rowB2=0;
        for (int x=0;x<high_w;++x) {
            size_t idx = (size_t)y * high_w + x;
            int r = highres.r[idx];
            int g = highres.g[idx];
            int b = highres.b[idx];
            rowR += r; rowG += g; rowB += b;
            rowR2 += (uint64_t)r * (uint64_t)r;
            rowG2 += (uint64_t)g * (uint64_t)g;
            rowB2 += (uint64_t)b * (uint64_t)b;
            int ii = (y+1)*(high_w+1) + (x+1);
            int ii_up = (y)*(high_w+1) + (x+1);
            sumR[ii] = sumR[ii_up] + rowR;
            sumG[ii] = sumG[ii_up] + rowG;
            sumB[ii] = sumB[ii_up] + rowB;
            sumR2[ii] = sumR2[ii_up] + rowR2;
            sumG2[ii] = sumG2[ii_up] + rowG2;
            sumB2[ii] = sumB2[ii_up] + rowB2;
        }
    }
    PC_LOG_INFO("Integral+sq build completed in " + std::to_string(sw_integral.elapsed_us()) + "us");

    auto rect_sum3 = [&](const std::vector<uint64_t> &S, int x0,int y0,int x1,int y1)->uint64_t{
        uint64_t A = S[y0*(high_w+1)+x0];
        uint64_t B = S[y0*(high_w+1)+x1];
        uint64_t C = S[y1*(high_w+1)+x0];
        uint64_t D = S[y1*(high_w+1)+x1];
        return D + A - B - C;
    };
    auto rect_sum3_sq = [&](const std::vector<uint64_t> &S, int x0,int y0,int x1,int y1)->uint64_t{
        uint64_t A = S[y0*(high_w+1)+x0];
        uint64_t B = S[y0*(high_w+1)+x1];
        uint64_t C = S[y1*(high_w+1)+x0];
        uint64_t D = S[y1*(high_w+1)+x1];
        return D + A - B - C;
    };

    // 构建带简单 mask 描述符（rectangles 或 quadrant）的字形表
    struct GDesc { int code; enum {H, V, Q, F, S} type; int level; int qidx; };
    // 为加速 pruning 按顺序排列字形：F、S、quadrants、horizontals（大->小）、verticals（大->小）
    static const std::vector<GDesc> glyphs = [](){
        std::vector<GDesc> g;
        g.push_back({0x2588, GDesc::F, 0,0}); // full（填充）
        g.push_back({0x20, GDesc::S, 0, 0}); // space（空格）
        // quadrants（象限）
        g.push_back({0x2598, GDesc::Q, 0, 0});
        g.push_back({0x259D, GDesc::Q, 0, 1});
        g.push_back({0x2596, GDesc::Q, 0, 2});
        g.push_back({0x259E, GDesc::Q, 0, 3});
        // horizontals（从大到小）
        for (int level=8; level>=1; --level) g.push_back({0x2580 + level, GDesc::H, level,0});
        // verticals（从大到小）
        const int vert_codes[8] = {0x258F,0x258E,0x258D,0x258C,0x258B,0x258A,0x2589,0x2588};
        for (int i=7;i>=0;--i) g.push_back({vert_codes[i], GDesc::V, 8-i,0});
        return g;
    }();

    int threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::string> parts(threads);

    for (int tid=0; tid<threads; ++tid) {
        int row0 = (out_h * tid) / threads;
        int row1 = (out_h * (tid+1)) / threads;
        pool.submit([=,&measure_only,&stats,&out,&parts,&sumR,&sumG,&sumB,&sumR2,&sumG2,&sumB2]() {
            std::string local;
            local.reserve((row1 - row0) * out_w * 12); // 粗略预留以减少 reallocs
            for (int by=row0; by<row1; ++by) {
                if (stats) stats->total_cells.fetch_add((uint64_t)out_w);
                int prev_br = -1, prev_bg = -1, prev_bb = -1;
                int prev_fr = -1, prev_fg = -1, prev_fb = -1;
                for (int bx=0; bx<out_w; ++bx) {
                    int x0c = bx*SUB_W, y0c = by*SUB_H, x1c = x0c + SUB_W, y1c = y0c + SUB_H;
                    uint64_t totalR = rect_sum3(sumR, x0c, y0c, x1c, y1c);
                    uint64_t totalG = rect_sum3(sumG, x0c, y0c, x1c, y1c);
                    uint64_t totalB = rect_sum3(sumB, x0c, y0c, x1c, y1c);
                    uint64_t totalR2 = rect_sum3(sumR2, x0c, y0c, x1c, y1c);
                    uint64_t totalG2 = rect_sum3(sumG2, x0c, y0c, x1c, y1c);
                    uint64_t totalB2 = rect_sum3(sumB2, x0c, y0c, x1c, y1c);

                    double best_err = 1e308; int best_cp = 0x20;
                    int best_fr=0,best_fg=0,best_fb=0,best_br=0,best_bg=0,best_bb=0;
                    uint64_t tot = (uint64_t)SUB_W * SUB_H;
                    // 剪枝的快速近似值
                    int total_avg_r = (int)(totalR / tot);
                    int total_avg_g = (int)(totalG / tot);
                    int total_avg_b = (int)(totalB / tot);
                    const int PRUNE_COLOR_DIFF = prune_threshold; // 可调阈值（通道绝对差之和）
                    // 测量每单元 prune 检查时间与每次评估时间以定位 SIMD 热点
                    Stopwatch sw_cell_prune;
                    Stopwatch sw_eval_local;
                    IVDEP
                    for (const auto &gd : glyphs) {
                        uint64_t fgR=0,fgG=0,fgB=0; uint64_t fgR2=0,fgG2=0,fgB2=0; uint64_t fgCnt=0;
                        if (stats) stats->candidates_considered.fetch_add(1);
                        if (gd.type == GDesc::H) {
                            int rows = (int)std::ceil(gd.level * (double)SUB_H / 8.0);
                            int fy0 = y1c - rows, fy1 = y1c;
                            fgCnt = (uint64_t)SUB_W * (fy1 - fy0);
                            fgR = rect_sum3(sumR, x0c, fy0, x1c, fy1);
                            fgG = rect_sum3(sumG, x0c, fy0, x1c, fy1);
                            fgB = rect_sum3(sumB, x0c, fy0, x1c, fy1);
                            fgR2 = rect_sum3(sumR2, x0c, fy0, x1c, fy1);
                            fgG2 = rect_sum3(sumG2, x0c, fy0, x1c, fy1);
                            fgB2 = rect_sum3(sumB2, x0c, fy0, x1c, fy1);
                        } else if (gd.type == GDesc::V) {
                            int cols = (int)std::ceil(gd.level * (double)SUB_W / 8.0);
                            int fx0 = x0c, fx1 = x0c + cols;
                            fgCnt = (uint64_t)(fx1 - fx0) * SUB_H;
                            fgR = rect_sum3(sumR, fx0, y0c, fx1, y1c);
                            fgG = rect_sum3(sumG, fx0, y0c, fx1, y1c);
                            fgB = rect_sum3(sumB, fx0, y0c, fx1, y1c);
                            fgR2 = rect_sum3(sumR2, fx0, y0c, fx1, y1c);
                            fgG2 = rect_sum3(sumG2, fx0, y0c, fx1, y1c);
                            fgB2 = rect_sum3(sumB2, fx0, y0c, fx1, y1c);
                        } else if (gd.type == GDesc::Q) {
                            int qx0 = (gd.qidx % 2) ? (x0c + SUB_W/2) : x0c;
                            int qx1 = qx0 + SUB_W/2;
                            int qy0 = (gd.qidx < 2) ? y0c : (y0c + SUB_H/2);
                            int qy1 = qy0 + SUB_H/2;
                            fgCnt = (uint64_t)(qx1 - qx0) * (qy1 - qy0);
                            fgR = rect_sum3(sumR, qx0, qy0, qx1, qy1);
                            fgG = rect_sum3(sumG, qx0, qy0, qx1, qy1);
                            fgB = rect_sum3(sumB, qx0, qy0, qx1, qy1);
                            fgR2 = rect_sum3(sumR2, qx0, qy0, qx1, qy1);
                            fgG2 = rect_sum3(sumG2, qx0, qy0, qx1, qy1);
                            fgB2 = rect_sum3(sumB2, qx0, qy0, qx1, qy1);
                        } else if (gd.type == GDesc::F) {
                            fgCnt = tot;
                            fgR = totalR; fgG = totalG; fgB = totalB;
                            fgR2 = totalR2; fgG2 = totalG2; fgB2 = totalB2;
                        } else { // space（空格）
                            fgCnt = 0; fgR = fgG = fgB = 0; fgR2 = fgG2 = fgB2 = 0;
                        }
                        uint64_t bgCnt = tot - fgCnt;

                        // 使用平均颜色差进行快速剪枝
                        int fr = 0, fgc = 0, fb = 0, br = 0, bgcol = 0, bb = 0;
                        if (fgCnt>0) { fr = (int)(fgR/fgCnt); fgc = (int)(fgG/fgCnt); fb = (int)(fgB/fgCnt); }
                        if (bgCnt>0) { br = (int)((totalR - fgR)/bgCnt); bgcol = (int)((totalG - fgG)/bgCnt); bb = (int)((totalB - fgB)/bgCnt); }
                        auto t0p = sw_cell_prune.elapsed_us();
                        int color_diff = abs(fr - br) + abs(fgc - bgcol) + abs(fb - bb);
                        auto t1p = sw_cell_prune.elapsed_us();
                        if (stats) stats->prune_check_us.fetch_add(t1p - t0p);
                        if (color_diff < prune_threshold) {
                            // 几乎相同，跳过详细误差计算
                            if (stats) stats->candidates_skipped.fetch_add(1);
                            continue;
                        }
                        // 执行完整评估
                        if (stats) stats->evaluations.fetch_add(1);
                        auto t0e = sw_eval_local.elapsed_us();


                        // 使用平方和技巧计算每通道误差
                        double err = 0.0;
#ifdef PICCONV_USE_AVX2
                        // Fast AVX2 路径：常见情况（fg 与 bg 均存在）
                        if (fgCnt > 0 && bgCnt > 0) {
                            __m256d v_fg = _mm256_setr_pd((double)fgR, (double)fgG, (double)fgB, 0.0);
                            __m256d v_bg = _mm256_setr_pd((double)(totalR - fgR), (double)(totalG - fgG), (double)(totalB - fgB), 0.0);
                            __m256d v_fg_sq = _mm256_mul_pd(v_fg, v_fg);
                            __m256d v_bg_sq = _mm256_mul_pd(v_bg, v_bg);
                            __m256d v_fg_cnt = _mm256_set1_pd((double)fgCnt);
                            __m256d v_bg_cnt = _mm256_set1_pd((double)bgCnt);
                            __m256d v_term_fg = _mm256_div_pd(v_fg_sq, v_fg_cnt);
                            __m256d v_term_bg = _mm256_div_pd(v_bg_sq, v_bg_cnt);
                            __m256d v_total2 = _mm256_setr_pd((double)totalR2, (double)totalG2, (double)totalB2, 0.0);
                            __m256d v_err = _mm256_sub_pd(_mm256_sub_pd(v_total2, v_term_fg), v_term_bg);
                            // 对 0..2 通道求和
                            double lane0 = _mm256_cvtsd_f64(v_err);
                            __m128d hi = _mm256_extractf128_pd(v_err, 1);
                            double lane1 = _mm_cvtsd_f64(hi);
                            double lane2 = _mm_cvtsd_f64(_mm_shuffle_pd(hi, hi, 1));
                            err = lane0 + lane1 + lane2;
                        } else
#endif
                        {
                            // 标量回退（处理 fgCnt==0 或 bgCnt==0 的情况）
                            // R 通道
                            if (fgCnt > 0) {
                                double term_fg = (double)fgR * (double)fgR / (double)fgCnt;
                                double term_bg = 0.0;
                                if (bgCnt > 0) {
                                    double bgRsum = (double)(totalR - fgR);
                                    term_bg = bgRsum * bgRsum / (double)bgCnt;
                                }
                                err += (double)totalR2 - term_fg - term_bg;
                            } else {
                                // 全为背景
                                if (bgCnt > 0) err += (double)totalR2 - (double)(totalR * totalR) / (double)bgCnt; else err += (double)totalR2;
                            }
                            // G 通道
                            if (fgCnt > 0) {
                                double term_fg = (double)fgG * (double)fgG / (double)fgCnt;
                                double term_bg = 0.0;
                                if (bgCnt > 0) { double bgGsum = (double)(totalG - fgG); term_bg = bgGsum * bgGsum / (double)bgCnt; }
                                err += (double)totalG2 - term_fg - term_bg;
                            } else {
                                if (bgCnt > 0) err += (double)totalG2 - (double)(totalG * totalG) / (double)bgCnt; else err += (double)totalG2;
                            }
                            // B 通道
                            if (fgCnt > 0) {
                                double term_fg = (double)fgB * (double)fgB / (double)fgCnt;
                                double term_bg = 0.0;
                                if (bgCnt > 0) { double bgBsum = (double)(totalB - fgB); term_bg = bgBsum * bgBsum / (double)bgCnt; }
                                err += (double)totalB2 - term_fg - term_bg;
                            } else {
                                if (bgCnt > 0) err += (double)totalB2 - (double)(totalB * totalB) / (double)bgCnt; else err += (double)totalB2;
                            }
                        }
                        auto t1e = sw_eval_local.elapsed_us();
                        if (stats) stats->eval_us.fetch_add(t1e - t0e);
                        if (err < best_err) {
                            best_err = err; best_cp = gd.code;
                            if (fgCnt>0) { best_fr = (int)(fgR/fgCnt); best_fg = (int)(fgG/fgCnt); best_fb = (int)(fgB/fgCnt); }
                            if (bgCnt>0) { best_br = (int)((totalR - fgR)/bgCnt); best_bg = (int)((totalG - fgG)/bgCnt); best_bb = (int)((totalB - fgB)/bgCnt); }
                        }
                        // 若 measure_only，不构建字符串；继续下一个字形
                        if (measure_only) continue;
                    }
                    // 测量模式下跳过字符串组装
                    if (measure_only) continue;
                    // 按行合并颜色区间
                    if (best_br != prev_br || best_bg != prev_bg || best_bb != prev_bb) {
                        local += bg_rgb(best_br,best_bg,best_bb);
                        prev_br = best_br; prev_bg = best_bg; prev_bb = best_bb;
                    }
                    if (best_fr != prev_fr || best_fg != prev_fg || best_fb != prev_fb) {
                        local += fg_rgb(best_fr,best_fg,best_fb);
                        prev_fr = best_fr; prev_fg = best_fg; prev_fb = best_fb;
                    }
                    local += codepoint_to_utf8(best_cp);
                }
                if (!measure_only) local += reset();
                local += '\n';
            }
            parts[tid] = std::move(local);
        });
    }
    pool.wait_idle();
    for (int t=0;t<threads;++t) out << parts[t];
    return out.str();
}


