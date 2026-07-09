# perapera-anime-maker901

OpenToonzのような本格2Dアニメーション制作ソフトウェア(Windows向け)。現在はMVP(v0.1.0)段階。

- 仕様: [docs/SPEC.md](docs/SPEC.md)
- 作業手順書: [docs/WORKPLAN.md](docs/WORKPLAN.md)
- ファイルフォーマット: [docs/FILE_FORMAT.md](docs/FILE_FORMAT.md)

## 現在できること (MVP)

- ラスター手描き(ペンタブ/液タブの筆圧対応、マウスも可)
- ペン/消しゴム切り替え(ペン後端の消しゴム側も自動認識)
- フレームの追加・削除・切り替え、フレーム一覧パネル
- オニオンスキン表示(前フレーム=赤系/次フレーム=緑系)
- パラパラ再生(FPS 1〜60)
- プロジェクトの保存/読み込み(.ppam形式)

## ショートカット

| キー | 機能 |
|---|---|
| P / E | ペン / 消しゴム |
| , / . | 前 / 次フレーム |
| A | フレーム追加 |
| Delete | フレーム削除 |
| O | オニオンスキン切り替え |
| Space | 再生 / 停止 |
| Ctrl+N / O / S | 新規 / 開く / 保存 |

## 技術スタック

- C++20
- Qt 6 (LGPLv3、動的リンク)
- OpenGL
- CMake + vcpkg (catch2 / nlohmann-json / zlib)

## 開発環境セットアップ

1. Visual Studio Build Tools (C++ workload, Windows SDK)
2. CMake 3.21+
3. Qt 6 (aqtinstallでのインストールを推奨。`C:\Qt` 配下を想定)
4. vcpkg (`C:\dev\vcpkg` を想定。パスが異なる場合はCMakePresets.jsonを調整)

## ビルドとテスト

```
cmake --preset default
cmake --build build --config Release
build\tests\Release\perapera_core_tests.exe   # 単体テスト
build\src\Release\perapera-anime-maker901.exe # 起動
```

## 動作確認用フック(ヘッドレス検証)

| 引数 | 内容 |
|---|---|
| `--stroke-test <png>` | 自動ストローク描画→画面保存 |
| `--onion-test <png>` | 3フレーム作成→オニオンスキン表示→画面保存 |
| `--play-test <png1> <png2>` | 再生中の画面を2回保存(差分でフレーム送り確認) |
| `--io-test <ppam> <png>` | 描画→保存→新規→読み込み→画面保存 |
| `--ui-test <png>` | ウィンドウ全体(メニュー/パネル込み)を保存 |
