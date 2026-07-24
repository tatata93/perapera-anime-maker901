# perapera-anime-maker901

OpenToonzのような本格2Dアニメーション制作ソフトウェア(Windows向け)。現在はMVP(v0.1.0)段階。

- 仕様: [docs/SPEC.md](docs/SPEC.md)
- 作業手順書: [docs/WORKPLAN.md](docs/WORKPLAN.md)
- ファイルフォーマット: [docs/FILE_FORMAT.md](docs/FILE_FORMAT.md)

## 現在できること

- ラスター手描き(ペンタブ/液タブの筆圧対応、マウスも可)。太さ・色の変更、Undo/Redo
- ペン/消しゴム/塗りつぶし(主線で囲んだ領域を彩色、現場の彩色と同じ流れ)
- セル・レイヤー構造(Cut→セル→レイヤー→フレーム)。レイヤーパネルで追加/削除/移動/可視切替、
  種別設定(通常/色トレス線/作監修正)
- カラーパレット(プロジェクトに保存)
- **タイムシート(Xsheet)**: ACTION(原画番号・中割記号)とCELL(実際に出す動画)を並べて表示。
  原画/中割の追加、尺変更、1/2/3コマ打ち、コピー、空セル、直接編集に対応
- 動画の追加・割付、パラパラ再生(FPS 1〜60、既定24)
- キャンバスのズーム/パン/回転、下敷き表示(静止画・連番=3DCGなぞり)
- プロジェクトの保存/読み込み(.ppam v2)、自動保存・クラッシュリカバリ
- **書き出し(Ctrl+E)**: 連番PNG / mp4(ffmpeg.exe検出時)。コマ範囲・セル選択・
  トレス線/修正の含有を指定可。最終画はCPU合成(トレス線・修正は既定で除外)
- 作画支援: 手ブレ補正、左右反転表示(H)、動画複製、ライトテーブル(任意動画の透かし)、
  仕上げ表示(T)、塗分け線(非表示でも塗り境界として機能)

## ショートカット

| キー | 機能 |
|---|---|
| P / E / F | ペン / 消しゴム / 塗りつぶし |
| , / . | 前 / 次フレーム |
| A | フレーム追加 |
| Delete | フレーム削除 |
| O | オニオンスキン切り替え |
| Space | 再生 / 停止 |
| ホイール / Alt+ホイール / 中ドラッグ | ズーム / 回転 / パン |
| Ctrl+0 | ビューをリセット |
| Ctrl+Z / Ctrl+Y | 元に戻す / やり直す |
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
