# Film Preset Notes

The shooting film presets are intentionally named as "<stock>風". They are not
manufacturer LUTs; they are editable starting points built from public product
descriptions, technical data sheets, grain/resolution data, and published sample
characteristics.

## Parameter Mapping

- `contrast`: overall D-log-like toe/straight/shoulder strength.
- `saturation`: global color separation before dye crosstalk.
- `fade`: lifted black / minimum density impression.
- `warmth`: simple red-blue balance.
- `crosstalk`: dye-layer mixing. Higher values soften color separation.
- `grain` and `grainSize`: perceived density grain strength and scale.
- `halation`: warm highlight fog around hot areas.
- `respR/G/B0..4`: per-layer response curve, sampled at input 0, .25, .5, .75, 1.

## Preset Intent

- `Kodak UltraMax 400風`: everyday ISO 400 C-41 negative. Bright, saturated,
  forgiving, with more visible 400-speed grain.
- `Kodak EKTAR 100風`: low-speed C-41 negative. High saturation, crisp color,
  very fine grain, modest halation.
- `Kodak Portra 400風`: ISO 400 professional C-41 negative. Smooth skin tone,
  gentle contrast, warm highlights, fine but not invisible grain.
- `Fujichrome Provia 100F風`: E-6 reversal. Neutral, faithful, medium contrast
  and saturation, very fine grain.
- `Fujichrome Velvia 50風`: E-6 reversal. Deep shadows, strong saturation,
  vivid greens/reds, restrained grain and halation.
- `Fujicolor Superia X-TRA 400風`: everyday ISO 400 C-41 negative. Bright color,
  good gray balance, natural skin, smooth high-speed grain.

## Public Sources Used

- Kodak Alaris technical publications and product pages for UltraMax 400, EKTAR
  100, and Portra 400.
- Fujifilm product/support pages and data sheets for Provia 100F, Velvia 50, and
  Superia X-TRA 400.

