#pragma once
#include "resample.h"
#include <string>
#include <vector>

namespace PicConvertor { class TaskSystem; } // forward

// 新的简洁模式：
// - low：每字符单元的纯 background-color 映射（视觉更简洁）
// - high：使用 horizontal/vertical/quadrant glyphs 的高精度子像素映射
enum class Charset { low, high };

// Low：仅背景渲染器。highres_blocks 应采样为 (out_w*8) × (out_h*8)
std::string render_low(const BlockPlanes &highres, int out_w, int out_h);

// High：advanced renderer，使用 subpixel masks 和 glyph search。highres_blocks 应采样为 (out_w*8) × (out_h*8)
// 现在接受一个 TaskSystem 引用（在 main 中创建）用于并行化
#include <atomic>

struct PruneStats {
    std::atomic<uint64_t> total_cells{0};
    std::atomic<uint64_t> candidates_considered{0};
    std::atomic<uint64_t> candidates_skipped{0};
    std::atomic<uint64_t> evaluations{0};
    // 累计微秒计时器，用于定位 SIMD 优化热点
    std::atomic<uint64_t> prune_check_us{0};
    std::atomic<uint64_t> eval_us{0};
};

// High：advanced renderer，使用 subpixel masks 和 glyph search。highres_blocks 应采样为 (out_w*8) × (out_h*8)
// prune_threshold：用于快速 pruning 的通道绝对差之和阈值
// measure_only：为 true 时不组装字符串，仅收集统计与代价
std::string render_high(const BlockPlanes &highres, int out_w, int out_h, PicConvertor::TaskSystem &pool, int prune_threshold = 24, PruneStats* stats = nullptr, bool measure_only = false);

// 快速重采样辅助：用于从图像构建 highres_blocks
std::vector<Block> resample_to_blocks_fast(const Image &img, int out_w, int out_h);
BlockPlanes resample_to_planes_fast(const Image &img, int out_w, int out_h, PicConvertor::TaskSystem &pool, int tile_h, int tile_h_horiz);
BlockPlanes resample_to_planes_fast(const Image &img, int out_w, int out_h);

// 辅助：从字符串选择 charset
Charset charset_from_string(const std::string &s);
