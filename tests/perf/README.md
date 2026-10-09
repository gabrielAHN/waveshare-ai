# Home renderer microbenchmark

The fixture is test-only and has no production hook, task, network, NVS, or private configuration.
It allocates two equal RGB565 frames plus the large `home_ui` fixture from heap; the ESP build requests
all three from PSRAM and records Home render, accent composition, and cache-miss `home_compose` stages
with `esp_timer_get_time()`. The component treats any function frame over 4096 bytes as a build error.

The current helper requires the named-device layout in this checkout. From the current repository
root, its host smoke/proxy command is:

```sh
tests/perf/run_home_renderer_bench.sh "$PWD" "${TMPDIR:-/tmp}/home-current"
```

A retained pre-relocation tree has a different source layout. Test it only with that tree's own
benchmark helper and IDF project; do not pass it to the current helper:

```sh
/path/to/retained-pre-relocation-tree/tests/perf/run_home_renderer_bench.sh \
  /path/to/retained-pre-relocation-tree /tmp/home-reference
HOME_SOURCE_ROOT=/path/to/retained-pre-relocation-tree \
  idf.py -C /path/to/retained-pre-relocation-tree/tests/perf -B /path/to/reference-build build
```

For a parent-run current device measurement, use the current project with the current root:

```sh
HOME_SOURCE_ROOT="$PWD" idf.py -C tests/perf -B /path/to/current-build set-target esp32s3
HOME_SOURCE_ROOT="$PWD" idf.py -C tests/perf -B /path/to/current-build build
```

Compiler stubs used by documentation tests are path controls, not benchmark results. Actual builds
require PSRAM and a fixed 240 MHz CPU (`sdkconfig.defaults`), the same public board
sdkconfig, a clean build directory, and the same parent-owned flash procedure. The fixture fails at
compile time without PSRAM, allocates both RGB565 buffers and `home_ui` specifically from PSRAM, and
yields outside every timed interval without changing task ownership or watchdog configuration.
Capture every `HOME_RENDER_BENCH` and
`HOME_RENDER_STAGE` line. These CPU-stage measurements do not establish panel FPS or physical
smoothness; also capture the existing production `MOTION_FRAME` render/transfer/interval/sample-age
telemetry during a separately paced held gesture.
