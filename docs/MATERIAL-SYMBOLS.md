# Home icons: Google Material Symbols Rounded

The firmware embeds four Google **Material Symbols Rounded** icons (`auto_awesome`, `mic`,
`settings`, `sensors`) as alpha masks; the Home Settings and Sensor tiles draw them. The Home
Sparkles tile draws a render from the device's own sparkle renderer instead
(`plugins/hermes/firmware/sparkle_image.h`, `tools/build_sparkle_image.c`), and the Ask tile draws the bot
art ([KOTARO.md](../plugins/hermes/KOTARO.md)).

## Provenance and licence

`assets/material-symbols/` contains byte-identical official SVG exports from the **symbols/** tree of
[`google/material-design-icons`](https://github.com/google/material-design-icons).
`assets/material-symbols/provenance.json` records the immutable upstream commit, the original
paths and SHA-256 hashes: Rounded, weight 400, grade 0, fill 0, optical size 48. The upstream
**Apache-2.0** licence is kept verbatim in `assets/material-symbols/LICENSE`. The original SVG paths
are not redrawn or distorted.

## Rebuild (offline)

With ImageMagick **7.1.2-15 Q16-HDRI**:

```sh
python3 tools/generate_material_symbols.py           # regenerate the named device's material_symbols.h
python3 tools/generate_material_symbols.py --check   # verify the committed header reproduces
```

The converter verifies the input hashes, rasterizes each SVG at 1152 DPI before downsampling to a
192x192 alpha mask (upscaling a small bitmap looks blurry), and translates the actual ink bounds to
the centre of the mask. The named device's `firmware/main/material_symbols.h` embeds those masks; the firmware only
alpha-blends them into its RGB565 frame (no fonts, network or extra display owner at runtime).
Centring by ink bounds allows a half-pixel rounding difference; it does not mirror asymmetric
official paths. The Home symbols occupy the centred 192 px region at (88,108) of each full-screen
Home page.
