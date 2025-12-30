#pragma once
#include "image.h"
#include <vector>
#include <cstdint>

namespace PicConvertor { class TaskSystem; }

struct Block {
    int r, g, b; // 平均颜色
    double luminance; // perceived luminance（感知亮度）
};

// 用于 high-res blocks 的 Structure-of-arrays 布局（SoA）。宽/高为逻辑网格尺寸（例如 out_w*8 × out_h*8，用于 high 模式采样）。
struct BlockPlanes {
    int width = 0;
    int height = 0;
    std::vector<int> r;
    std::vector<int> g;
    std::vector<int> b;
};

// 将图像重采样为宽×高的块网格（朴素实现）
std::vector<Block> resample_to_blocks(const Image &img, int out_w, int out_h);

// SoA 快速重采样辅助
BlockPlanes resample_to_planes_fast(const Image &img, int out_w, int out_h);
BlockPlanes resample_to_planes_fast(const Image &img, int out_w, int out_h, PicConvertor::TaskSystem &pool, int tile_h = 64, int tile_h_horiz = -1);

// 使用积分图的快速重采样（对大输出更快）
std::vector<Block> resample_to_blocks_fast(const Image &img, int out_w, int out_h);

// 并行快速重采样变体：使用提供的 TaskSystem 实现按行并行
std::vector<Block> resample_to_blocks_fast(const Image &img, int out_w, int out_h, PicConvertor::TaskSystem &pool, int tile_h = 64);
