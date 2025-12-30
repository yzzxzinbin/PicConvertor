#pragma once
#include <windows.h>
#include <cstdint>

// 轻量级高精度计时器，使用 QueryPerformanceCounter（微秒）
struct Stopwatch {
    LARGE_INTEGER freq{};
    LARGE_INTEGER start{};
    Stopwatch() { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start); }
    void reset() { QueryPerformanceCounter(&start); }
    uint64_t elapsed_us() const {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return (uint64_t)((t.QuadPart - start.QuadPart) * 1000000 / freq.QuadPart);
    }
    static uint64_t now_us() {
        LARGE_INTEGER f, t;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t);
        return (uint64_t)((t.QuadPart * 1000000) / f.QuadPart);
    }
};