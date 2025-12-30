#include "resample.h"
#include "timing.h"
#include "TaskSystem.h"
#include "Logger.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <future>
#ifdef PICCONV_USE_AVX2
    #include <immintrin.h>
#endif

// IVDEP 宏：提示编译器进行 vectorization（为可移植性在本地定义）
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

static inline double rgb_to_luminance(int r, int g, int b) {
    // Perceived luminance（Rec. 709）
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

#ifdef PICCONV_USE_AVX2
static inline uint32_t sum_u8_avx2(const uint8_t* ptr, int len) {
    __m256i acc = _mm256_setzero_si256();
    int i = 0;
    for (; i + 32 <= len; i += 32) {
        __m256i v0 = _mm256_loadu_si256((const __m256i*)(ptr + i));
        __m256i v1 = _mm256_loadu_si256((const __m256i*)(ptr + i + 16));
        acc = _mm256_add_epi64(acc, _mm256_sad_epu8(v0, _mm256_setzero_si256()));
        acc = _mm256_add_epi64(acc, _mm256_sad_epu8(v1, _mm256_setzero_si256()));
    }
    uint64_t lanes[4];
    _mm256_storeu_si256((__m256i*)lanes, acc);
    uint64_t sum = lanes[0] + lanes[1] + lanes[2] + lanes[3];
    for (; i < len; ++i) sum += ptr[i];
    return (uint32_t)sum;
}
#endif

static inline uint32_t sum_u8_scalar(const uint8_t* ptr, int len) {
    uint32_t s = 0;
    for (int i = 0; i < len; ++i) s += ptr[i];
    return s;
}

static inline uint32_t sum_u8(const uint8_t* ptr, int len) {
#ifdef PICCONV_USE_AVX2
    return sum_u8_avx2(ptr, len);
#else
    return sum_u8_scalar(ptr, len);
#endif
}

#ifdef PICCONV_USE_AVX2
static inline void sum_u8_pair(const uint8_t* base, int off0, int off1, int len, uint32_t &s0, uint32_t &s1) {
    __m256i acc0 = _mm256_setzero_si256();
    __m256i acc1 = _mm256_setzero_si256();
    const __m256i zero = _mm256_setzero_si256();
    int i = 0;
    for (; i + 32 <= len; i += 32) {
        __m256i v00 = _mm256_loadu_si256((const __m256i*)(base + off0 + i));
        __m256i v01 = _mm256_loadu_si256((const __m256i*)(base + off0 + i + 16));
        __m256i v10 = _mm256_loadu_si256((const __m256i*)(base + off1 + i));
        __m256i v11 = _mm256_loadu_si256((const __m256i*)(base + off1 + i + 16));
        acc0 = _mm256_add_epi64(acc0, _mm256_sad_epu8(v00, zero));
        acc0 = _mm256_add_epi64(acc0, _mm256_sad_epu8(v01, zero));
        acc1 = _mm256_add_epi64(acc1, _mm256_sad_epu8(v10, zero));
        acc1 = _mm256_add_epi64(acc1, _mm256_sad_epu8(v11, zero));
    }
    uint64_t lanes0[4];
    uint64_t lanes1[4];
    _mm256_storeu_si256((__m256i*)lanes0, acc0);
    _mm256_storeu_si256((__m256i*)lanes1, acc1);
    uint64_t sum0 = lanes0[0] + lanes0[1] + lanes0[2] + lanes0[3];
    uint64_t sum1 = lanes1[0] + lanes1[1] + lanes1[2] + lanes1[3];
    for (; i < len; ++i) {
        sum0 += base[off0 + i];
        sum1 += base[off1 + i];
    }
    s0 = (uint32_t)sum0;
    s1 = (uint32_t)sum1;
}
#else
static inline void sum_u8_pair(const uint8_t* base, int off0, int off1, int len, uint32_t &s0, uint32_t &s1) {
    s0 = sum_u8_scalar(base + off0, len);
    s1 = sum_u8_scalar(base + off1, len);
}
#endif

std::vector<Block> resample_to_blocks(const Image &img, int out_w, int out_h) {
    // 默认使用快速版本
    return resample_to_blocks_fast(img, out_w, out_h);
}



// 将交错的 RGB 展平为按通道的平面缓冲区（uint8），按行 tile 处理
static void flatten_to_planes(const Image &img, std::vector<uint8_t> &pr, std::vector<uint8_t> &pg, std::vector<uint8_t> &pb, PicConvertor::TaskSystem &pool, int tile_h) {
    int w = img.width, h = img.height;
    pr.resize((size_t)w * h);
    pg.resize((size_t)w * h);
    pb.resize((size_t)w * h);
    tile_h = std::min(tile_h, h);
    if (tile_h <= 0) tile_h = 64;
    int chunks = (h + tile_h - 1) / tile_h;
    std::vector<std::future<void>> futs;
    futs.reserve(chunks);
    for (int c = 0; c < chunks; ++c) {
        int y0 = c * tile_h;
        int y1 = std::min(h, y0 + tile_h);
        futs.push_back(pool.submitTask([=,&img,&pr,&pg,&pb]() {
            for (int y = y0; y < y1; ++y) {
                const uint8_t* src = img.pixels.data() + (size_t)y * img.width * img.channels;
                uint8_t* rdst = pr.data() + (size_t)y * img.width;
                uint8_t* gdst = pg.data() + (size_t)y * img.width;
                uint8_t* bdst = pb.data() + (size_t)y * img.width;
                for (int x = 0; x < img.width; ++x) {
                    rdst[x] = src[x * img.channels + 0];
                    gdst[x] = src[x * img.channels + 1];
                    bdst[x] = src[x * img.channels + 2];
                }
            }
        }));
    }
    for (auto &f : futs) f.get();
}

struct Run { int start; int end; int len; };

// 每行水平框求和到紧凑宽度 (out_w) 缓冲区。使用等宽分组与 dual-box AVX2。
static void horizontal_box_sum(const std::vector<uint8_t> &pr, const std::vector<uint8_t> &pg, const std::vector<uint8_t> &pb,
                               int w, int h, int out_w,
                               const std::vector<int> &x0s,
                               const std::vector<Run> &runs,
                               std::vector<uint32_t> &hr, std::vector<uint32_t> &hg, std::vector<uint32_t> &hb,
                               PicConvertor::TaskSystem &pool, int tile_h_rows) {
    hr.resize((size_t)h * out_w);
    hg.resize((size_t)h * out_w);
    hb.resize((size_t)h * out_w);
    tile_h_rows = std::min(tile_h_rows, h);
    if (tile_h_rows <= 0) tile_h_rows = 64;
    int chunks = (h + tile_h_rows - 1) / tile_h_rows;
    std::vector<std::future<void>> futs;
    futs.reserve(chunks);
    for (int c = 0; c < chunks; ++c) {
        int y0 = c * tile_h_rows;
        int y1 = std::min(h, y0 + tile_h_rows);
        futs.push_back(pool.submitTask([=,&pr,&pg,&pb,&hr,&hg,&hb,&x0s,&runs]() {
            for (int y = y0; y < y1; ++y) {
                const uint8_t* rowR = pr.data() + (size_t)y * w;
                const uint8_t* rowG = pg.data() + (size_t)y * w;
                const uint8_t* rowB = pb.data() + (size_t)y * w;
                uint32_t* dstR = hr.data() + (size_t)y * out_w;
                uint32_t* dstG = hg.data() + (size_t)y * out_w;
                uint32_t* dstB = hb.data() + (size_t)y * out_w;
                for (const auto &run : runs) {
                    int len = run.len;
                    int bx = run.start;
                    int end = run.end;
                    for (; bx + 1 < end; bx += 2) {
                        uint32_t sR0, sR1, sG0, sG1, sB0, sB1;
                        sum_u8_pair(rowR, x0s[bx], x0s[bx+1], len, sR0, sR1);
                        sum_u8_pair(rowG, x0s[bx], x0s[bx+1], len, sG0, sG1);
                        sum_u8_pair(rowB, x0s[bx], x0s[bx+1], len, sB0, sB1);
                        dstR[bx] = sR0; dstR[bx+1] = sR1;
                        dstG[bx] = sG0; dstG[bx+1] = sG1;
                        dstB[bx] = sB0; dstB[bx+1] = sB1;
                    }
                    if (bx < end) {
                        int x0 = x0s[bx];
                        dstR[bx] = sum_u8(rowR + x0, len);
                        dstG[bx] = sum_u8(rowG + x0, len);
                        dstB[bx] = sum_u8(rowB + x0, len);
                    }
                }
            }
        }));
    }
    for (auto &f : futs) f.get();
}


BlockPlanes resample_to_planes_fast(const Image &img, int out_w, int out_h) {
    PicConvertor::TaskSystem pool;
    return resample_to_planes_fast(img, out_w, out_h, pool, 64, -1);
}

BlockPlanes resample_to_planes_fast(const Image &img, int out_w, int out_h, PicConvertor::TaskSystem &pool, int tile_h, int tile_h_horiz) {
    BlockPlanes out;
    out.width = out_w;
    out.height = out_h;
    out.r.resize(out_w * out_h);
    out.g.resize(out_w * out_h);
    out.b.resize(out_w * out_h);
    if (img.width <=0 || img.height <=0) {
        out.r.clear(); out.g.clear(); out.b.clear();
        out.width = out.height = 0;
        return out;
    }

    Stopwatch sw;
    if (tile_h <= 0) tile_h = 64;

    // 预计算每个 bx 的 x 范围与每个 by 的 y 范围，以避免重复的除法/floor/ceil
    std::vector<int> x0s(out_w), x1s(out_w);
    for (int bx=0; bx<out_w; ++bx) {
        int x0 = (int)std::floor((double)bx * img.width / out_w);
        int x1 = (int)std::ceil((double)(bx+1) * img.width / out_w);
        x0s[bx] = std::max(0, std::min(img.width, x0));
        x1s[bx] = std::max(0, std::min(img.width, x1));
    }
    // 将等宽的连续框分组以减少水平过程中的每框工作量
    std::vector<Run> runs;
    if (out_w > 0) {
        int cur_len = x1s[0] - x0s[0];
        int run_start = 0;
        for (int bx=1; bx<=out_w; ++bx) {
            int len = (bx==out_w) ? -1 : (x1s[bx] - x0s[bx]);
            if (len != cur_len) {
                runs.push_back(Run{run_start, bx, cur_len});
                run_start = bx;
                cur_len = len;
            }
        }
    }
    std::vector<int> y0s(out_h), y1s(out_h);
    for (int by=0; by<out_h; ++by) {
        int y0 = (int)std::floor((double)by * img.height / out_h);
        int y1 = (int)std::ceil((double)(by+1) * img.height / out_h);
        y0s[by] = std::max(0, std::min(img.height, y0));
        y1s[by] = std::max(0, std::min(img.height, y1));
    }

    Stopwatch sw_flat;
    std::vector<uint8_t> pr, pg, pb;
    PC_LOG_INFO("Flattening RGB into planar buffers...");
    flatten_to_planes(img, pr, pg, pb, pool, tile_h);
    PC_LOG_INFO("Flatten to planes completed in " + std::to_string(sw_flat.elapsed_us()) + "us (tile_h=" + std::to_string(tile_h) + ")");

    Stopwatch sw_horiz;
    std::vector<uint32_t> hr, hg, hb;
    int tile_h_h_run = tile_h_horiz;
    if (tile_h_h_run <= 0) {
        int scaled = tile_h * 4;
        if (scaled <= 0) scaled = 64;
        tile_h_h_run = std::max(tile_h, scaled);
    }
    tile_h_h_run = std::min(tile_h_h_run, img.height);
    PC_LOG_INFO("Horizontal box pass (planar)...");
    horizontal_box_sum(pr, pg, pb, img.width, img.height, out_w, x0s, runs, hr, hg, hb, pool, tile_h_h_run);
    PC_LOG_INFO("Horizontal pass completed in " + std::to_string(sw_horiz.elapsed_us()) + "us (tile_h_horiz=" + std::to_string(tile_h_h_run) + ")");

    // 从水平求和直接进行垂直采样
    int tile_h_rows = std::min(tile_h, out_h);
    int num_chunks = (out_h + tile_h_rows - 1) / tile_h_rows;
    std::vector<std::future<void>> sampleFuts;
    sampleFuts.reserve(num_chunks);
    Stopwatch sw_sample;
    for (int c=0;c<num_chunks;++c) {
        int by0 = c * tile_h_rows;
        int by1 = std::min(out_h, by0 + tile_h_rows);
        sampleFuts.push_back(pool.submitTask([=,&out,&hr,&hg,&hb,&x0s,&x1s,&y0s,&y1s]() {
            for (int by = by0; by < by1; ++by) {
                int y0 = y0s[by];
                int y1 = y1s[by];
                IVDEP
                for (int bx = 0; bx < out_w; ++bx) {
                    int count = (x1s[bx] - x0s[bx]) * (y1 - y0);
                    if (count <= 0) count = 1;
                    uint64_t rsum = 0, gsum = 0, bsum = 0;
                    for (int sy = y0; sy < y1; ++sy) {
                        size_t idx = (size_t)sy * out_w + bx;
                        rsum += hr[idx];
                        gsum += hg[idx];
                        bsum += hb[idx];
                    }
                    size_t idx_out = (size_t)by * out_w + bx;
                    out.r[idx_out] = (int)(rsum / count);
                    out.g[idx_out] = (int)(gsum / count);
                    out.b[idx_out] = (int)(bsum / count);
                }
            }
        }));
    }
    for (auto &f : sampleFuts) f.get();
    PC_LOG_INFO("Sampling (vertical box) completed in " + std::to_string(sw_sample.elapsed_us()) + "us (tile_h=" + std::to_string(tile_h_rows) + ")");

    PC_LOG_INFO("Resample total time: " + std::to_string(sw.elapsed_us()) + "us");
    PC_LOG_INFO("Resample completed in " + std::to_string(sw.elapsed_us()) + "us");
    return out;
}

// Legacy API：先构建 SoA 然后转换为 AoS，以兼容仍使用 Block vector 的调用方
std::vector<Block> resample_to_blocks_fast(const Image &img, int out_w, int out_h) {
    PicConvertor::TaskSystem pool;
    return resample_to_blocks_fast(img, out_w, out_h, pool, 64);
}

// 并行实现（AoS 包装）
std::vector<Block> resample_to_blocks_fast(const Image &img, int out_w, int out_h, PicConvertor::TaskSystem &pool, int tile_h) {
    BlockPlanes planes = resample_to_planes_fast(img, out_w, out_h, pool, tile_h, -1);
    std::vector<Block> out;
    out.resize(planes.width * planes.height);
    size_t total = (size_t)planes.width * planes.height;
    for (size_t i=0;i<total;++i) {
        int r = planes.r[i];
        int g = planes.g[i];
        int b = planes.b[i];
        out[i] = Block{r,g,b,rgb_to_luminance(r,g,b)};
    }
    return out;
}
