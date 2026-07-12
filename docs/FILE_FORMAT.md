# プロジェクトファイルフォーマット (.ppproj)

プロジェクトは単一ファイルではなく **`<作品名>.ppproj` フォルダ** として保存する。フォルダ内の各ファイルは
スキーマバージョン付きの独自バイナリコンテナ(拡張子 `.ppam`)。リトルエンディアン。

## フォルダ構成

```
<作品名>.ppproj/
    project.ppam       プロジェクト構造(軽量。シーン→カットIDの並び、パレット)
    storyboard.ppam     絵コンテ(全シーンぶん、絵込み)
    boards.ppam         設定ボード(絵込み)
    cuts/
        cut_<ID>.ppam    カット1個ぶん(作画・エフェクト・撮影設定など一式)
        cut_<ID>.ppam
        ...
```

- `project.ppam` の `scenes[].cutIds` に列挙されているIDに対応する `cuts/cut_<ID>.ppam` だけが
  そのプロジェクトの一部とみなされる。保存時、リストに無い `cut_*.ppam` は孤児ファイルとして削除される
- `open()` (アプリの「開く」)は `.ppproj` フォルダ、またはその中の `project.ppam` のどちらのパスでも
  受け付ける(`ProjectIO::load` が正規化する)

## カットの永続ID

- 各カットは `Cut::id()` (uint64) を持つ。IDは保存時に初めて確定する: 新規作成直後のカットは `id()==0`
  (未採番)で、`ProjectIO::save()` が `project.nextCutId` から順に採番してからファイル名 `cut_<ID>.ppam`
  として書き出す
- IDはカットの並べ替え・改名では変わらない。カット削除で空いたIDが再利用されることもない
  (`nextCutId` は増加するのみ)

## 部分保存 (SaveOptions)

上書き保存では、変更があったファイルだけを書き直せるように `ProjectIO::save()` が `SaveOptions` を
受け取れる:

```cpp
struct SaveOptions {
    bool writeProject = true;
    bool writeStoryboard = true;
    bool writeBoards = true;
    bool writeAllCuts = true;   // trueなら全カット書き出し(cutIdsは無視)
    std::set<uint64_t> cutIds;  // writeAllCuts=falseのとき書き出す個別カットのID
};
```

- `options == nullptr` (既定)なら全ファイルを書き出す
- 保存先フォルダに `project.ppam` が存在しない(=新規保存先)場合は `SaveOptions` の指定に関わらず
  常に全書き出しする
- 孤児 `cut_*.ppam` の掃除は `writeProject == true` のときのみ行う(カット構成の全体像が分かるのが
  `project.ppam` を書いたときだけのため)
- MainWindow側は変更箇所を `DirtyScope`(project/storyboard/boards/allCuts/個別cutIds)として追跡し、
  上書き保存時にこの `SaveOptions` を組み立てる。判断に迷う変更は安全側(該当ファイルを丸ごと書き直す)
  に倒している

## コンテナ構造 (各 .ppam ファイル共通)

| オフセット | サイズ | 内容 |
|---|---|---|
| 0 | 4 | マジック `"PPAM"` |
| 4 | 4 | コンテナバージョン (uint32, 現在 1) |
| 8 | 8 | JSONヘッダのバイト数 (uint64) |
| 16 | 可変 | JSONヘッダ (UTF-8) |
| 16+jsonSize | 可変 | ブロブ領域 (zlib圧縮ピクセルデータの連結) |

JSONヘッダの内容はファイル種別 (`fileType`: `"project"` / `"storyboard"` / `"boards"` / `"cut"`) と
`schemaVersion` (現在2) を持ち、以降はファイル種別ごとに異なる。

### project.ppam

```json
{
  "fileType": "project",
  "schemaVersion": 2,
  "name": "...",
  "nextCutId": 4,
  "scenes": [{ "name": "...", "cutIds": [1, 2, 3] }],
  "palette": [[255, 0, 0, 255], ...]   // 省略可(空なら省略)
}
```

### storyboard.ppam

```json
{
  "fileType": "storyboard",
  "schemaVersion": 2,
  "scenes": [{
    "panels": [{
      "cutLabel": "1", "action": "...", "dialogue": "...", "duration": 24,
      "width": 1920, "height": 600, "blobOffset": 0, "blobSize": 123, "rawSize": 4608000
    }]
  }]
}
```

### boards.ppam

```json
{
  "fileType": "boards",
  "schemaVersion": 2,
  "boards": [{
    "name": "...",
    "width": 1920, "height": 1080, "blobOffset": 0, "blobSize": 123, "rawSize": 8294400,
    "colorSpecs": [{ "name": "肌", "r": 255, "g": 224, "b": 196, "a": 255 }]  // 省略可
  }]
}
```

### cuts/cut_<ID>.ppam

```json
{
  "fileType": "cut",
  "schemaVersion": 2,
  "id": 1,
  "name": "...",
  "frameCount": 24,
  "action": "...",
  "dialogue": "...",
  "status": 0,
  "cels": [{
    "name": "...",
    "visible": true,
    "exposure": [0, 0, 1, 1, ...],
    "positionKeys": [[0, 0.0, 0.0], [23, -1920.0, 0.0]],
    "paperWidth": 3840,   // 省略可(引きセル。0=キャンバスサイズに従う既定)
    "paperHeight": 1080,  // 省略可
    "layers": [{
      "name": "...",
      "visible": true,
      "role": "normal",   // "normal" | "colorTrace" | "correction"
      "frames": [{ "width": 1920, "height": 1080, "blobOffset": 0, "blobSize": 123, "rawSize": 8294400 }]
    }]
  }],
  "previz": { ... },        // 省略可(空シーンなら省略)
  "cameraKeys": [{ "frame": 0, "cx": 960.0, "cy": 540.0, "scale": 1.0 }],  // 省略可
  "effects": [{
    "type": 0, "enabled": true, "targetCel": -1,
    "params": { "radius": 10.0 },
    "paramCurves": { "radius": [{ "frame": 0, "value": 0.0 }, { "frame": 23, "value": 10.0 }] },  // 省略可
    "mask": { "width": 1920, "height": 1080, "blobOffset": 0, "blobSize": 123, "rawSize": 2073600 }  // 省略可
  }],
  "multiplane": {           // 省略可(無効かつ段が空なら省略)
    "enabled": true,
    "camera": { "focal": 50.0, "sensor": 36.0, "fstop": 2.0, "focus": 500.0 },
    "samples": 8,
    "planes": [{ "cel": 0, "distance": 500.0, "width": 400.0 }],
    "backlight": {          // 省略可(無効なら省略)
      "enabled": true, "intensity": 4.0, "r": 1.0, "g": 0.92, "b": 0.78,
      "tau": 0.1, "bloomRadius": 24.0, "bloomStrength": 0.5
    }
  }
}
```

- 空ビットマップのフレーム/マスクは `width:0, height:0` でブロブキーなし
- ピクセルはRGBA8を行順に並べzlib(deflate)で圧縮
- ブロブオフセットは各ファイル内で完結する(`blobOffset`はそのファイルのブロブ領域先頭からの相対値)

## 互換性ポリシー

- `schemaVersion` が読み手の対応値より大きい場合は読み込みを拒否する
- 項目追加は同一schemaVersionのまま任意キー追加で行う(未知キーは無視)
- 構造の破壊的変更時にschemaVersionを上げる
- **開発中(v1.0リリースまで)は旧バージョンとの後方互換を維持しない**(ユーザー決定 2026-07-10)。互換対応は
  リリース後から。単一ファイル `.ppam` 形式(旧)からフォルダ形式 `.ppproj` への移行も互換なし

## 自動保存・クラッシュリカバリ

- 自動保存先はアプリデータフォルダ配下の `autosave.ppproj` フォルダ(常に全ファイル書き出し)
- 起動時、`autosave.ppproj/project.ppam` が存在すればリカバリを提案する
- リカバリ「いいえ」または正常終了時は `autosave.ppproj` フォルダごと削除する

## 使用ライブラリ

- nlohmann-json (MIT)
- zlib (zlib License)
