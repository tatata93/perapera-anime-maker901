# プロジェクトファイルフォーマット (.ppam)

スキーマバージョン付きの独自バイナリコンテナ。リトルエンディアン。

## コンテナ構造

| オフセット | サイズ | 内容 |
|---|---|---|
| 0 | 4 | マジック `"PPAM"` |
| 4 | 4 | コンテナバージョン (uint32, 現在 1) |
| 8 | 8 | JSONヘッダのバイト数 (uint64) |
| 16 | 可変 | JSONヘッダ (UTF-8) |
| 16+jsonSize | 可変 | ブロブ領域 (zlib圧縮ピクセルデータの連結) |

## JSONヘッダ

```json
{
  "schemaVersion": 1,
  "project": {
    "name": "...",
    "scenes": [{
      "name": "...",
      "cuts": [{
        "name": "...",
        "layers": [{
          "name": "...",
          "frames": [{
            "width": 1920,
            "height": 1080,
            "blobOffset": 0,     // ブロブ領域先頭からのオフセット
            "blobSize": 123456,  // 圧縮後サイズ
            "rawSize": 8294400   // 展開後サイズ (width*height*4)
          }]
        }]
      }]
    }]
  }
}
```

- 空ビットマップのフレームは `width:0, height:0` でブロブキーなし
- ピクセルはRGBA8を行順に並べzlib(deflate)で圧縮

## 互換性ポリシー

- `schemaVersion` が読み手の対応値より大きい場合は読み込みを拒否する
- 項目追加は同一schemaVersionのまま任意キー追加で行う(未知キーは無視)
- 構造の破壊的変更時にschemaVersionを上げる

## 使用ライブラリ

- nlohmann-json (MIT)
- zlib (zlib License)
