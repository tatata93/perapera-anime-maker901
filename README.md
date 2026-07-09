# perapera-anime-maker901

OpenToonzのような本格2Dアニメーション制作ソフトウェア(Windows向け)。

- 仕様: [docs/SPEC.md](docs/SPEC.md)
- 作業手順書: [docs/WORKPLAN.md](docs/WORKPLAN.md)

## 技術スタック

- C++17/20
- Qt 6 (LGPLv3)
- OpenGL
- CMake + vcpkg

## 開発環境セットアップ

1. Visual Studio Build Tools (C++ workload, Windows SDK)
2. CMake
3. Qt 6 (aqtinstallでのインストールを推奨。`C:\Qt` 配下を想定)
4. vcpkg (`C:\dev\vcpkg` を想定)

## ビルド

```
cmake --preset default
cmake --build build
```
