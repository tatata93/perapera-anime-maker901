#pragma once

#include <algorithm>
#include <functional>
#include <thread>
#include <vector>

namespace core {

// [begin, end)の行範囲をハードウェア並列数で分割して並列実行する(STLのみ、Qt非依存)。
// fnは(rowBegin, rowEnd)の半開区間を受け取り、その範囲だけを処理する。
// 行ごとに独立した画素処理専用: 各行の結果が他の行に依存しないこと(結果はシリアル実行と
// バイト単位で同一になる=決定論を保つ)。範囲が小さい場合はそのままシリアル実行する
inline void parallelForRows(int begin, int end, const std::function<void(int, int)>& fn) {
    const int total = end - begin;
    if (total <= 0) return;

    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;  // 取得できない環境では控えめな既定値
    const int chunks = std::min(total, static_cast<int>(hw));
    if (chunks <= 1 || total < 16) {  // 小さすぎる仕事はスレッド起動コストの方が高い
        fn(begin, end);
        return;
    }

    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(chunks));
    const int step = (total + chunks - 1) / chunks;
    for (int c = 0; c < chunks; ++c) {
        const int b = begin + c * step;
        const int e = std::min(end, b + step);
        if (b >= e) break;
        threads.emplace_back(fn, b, e);
    }
    for (std::thread& t : threads) t.join();
}

}  // namespace core
