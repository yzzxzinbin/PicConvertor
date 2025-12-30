#include <iostream>
#include <windows.h>
#include <fstream>
#include <string>
#include <cstring>
#include <cmath>
#include "image.h"
#include "resample.h"
#include "renderer.h"
#include "TaskSystem.h"
#include "timing.h"
#include "Logger.h"

void print_usage() {
    std::cout << "Usage: picconvertor -i <input.jpg> [-w width_chars] [-h height_chars] [-s charset] [-T tile_height] [-o output.txt]\n";
    std::cout << "  -s charset: low | high (default low)\n";
    std::cout << "  -T tile_height: tile height (rows) used for tile-based resampling (default 64)\n";
    std::cout << "  -p <int>: prune threshold for render_high (sum abs color diff), default 24\n";
    std::cout << "  -P: run prune threshold sweep (useful for tuning)\n";
}

int main(int argc, char** argv) {
    // 确保控制台使用 UTF-8 编码
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    std::ios_base::sync_with_stdio(false);

    // 启用虚拟终端处理以支持 ANSI 转义（Windows 10+）。若不可用则静默回退以保持兼容性。
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
            SetConsoleMode(hOut, dwMode);
        }
    }

    if (argc < 2) { print_usage(); return 1; }

    // 提前初始化 Logger 以记录时间日志
    PicConvertor::Logger::getInstance().initialize("picconvertor.log");
    PC_LOG_INFO(std::string("Program started. Input: ") + (argc>1?argv[1]:""));
    std::string infile;
    std::string outfile;
    int out_w = 80;
    int out_h = 0;
    std::string charset_str = "shading";
    bool dither = false; // 为向后兼容保留，但 low/high 不使用
    int tile_h = 64; // 默认 tile height
    int prune_thresh = 24; // 默认 pruning 阈值
    for (int i=1;i<argc;i++) {
        if (strcmp(argv[i],"-i")==0 && i+1<argc) infile = argv[++i];
        else if (strcmp(argv[i],"-o")==0 && i+1<argc) outfile = argv[++i];
        else if (strcmp(argv[i],"-w")==0 && i+1<argc) out_w = atoi(argv[++i]);
        else if (strcmp(argv[i],"-h")==0 && i+1<argc) out_h = atoi(argv[++i]);
        else if (strcmp(argv[i],"-s")==0 && i+1<argc) charset_str = argv[++i];
        else if (strcmp(argv[i],"-T")==0 && i+1<argc) tile_h = atoi(argv[++i]);
        else if (strcmp(argv[i],"-p")==0 && i+1<argc) prune_thresh = atoi(argv[++i]);
        else { print_usage(); return 1; }
    }
    if (infile.empty()) { std::cerr << "No input file specified.\n"; print_usage(); return 1; }

    Image img;
    if (!img.load_from_file(infile)) return 2;

    // 若未提供输出高度则计算
    if (out_h <= 0) {
        // 近似字符单元纵横比：高度约为宽度的两倍 -> 使用 0.5
        double aspect = 0.5;
        out_h = std::max(1, (int)std::round((double)img.height * out_w * aspect / img.width));
    }

    Charset cs = charset_from_string(charset_str);
    std::string rendered;
    // 两种模式均对每字符使用 8x8 high-res 采样
    // 提前创建 TaskSystem，以便线程创建与重采样工作并行
    PicConvertor::TaskSystem pool;
    pool.preheat();
    Stopwatch sw;
    PC_LOG_INFO("TaskSystem created and preheated, elapsed: " + std::to_string(sw.elapsed_us()) + "us; tile_h=" + std::to_string(tile_h));

    auto t0 = Stopwatch();
    auto high_planes = resample_to_planes_fast(img, out_w*8, out_h*8, pool, tile_h, -1);
    PC_LOG_INFO("Resample completed in " + std::to_string(t0.elapsed_us()) + "us");

    if (cs == Charset::high) {
        Stopwatch tr;
        rendered = render_high(high_planes, out_w, out_h, pool, prune_thresh, nullptr, false);
        PC_LOG_INFO("render_high completed in " + std::to_string(tr.elapsed_us()) + "us (prune=" + std::to_string(prune_thresh) + ")");
    } else {
        Stopwatch tr;
        rendered = render_low(high_planes, out_w, out_h);
        PC_LOG_INFO("render_low completed in " + std::to_string(tr.elapsed_us()) + "us");
    }

    if (outfile.empty()) {
        std::cout << rendered;
    } else {
        std::ofstream ofs(outfile, std::ios::binary);
        if (!ofs) { std::cerr << "Failed to open output file\n"; return 3; }
        ofs << rendered;
        ofs.close();
    }

    return 0;
}
