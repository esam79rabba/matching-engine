# CI Pipeline Configuration — FR-1 Draft

**Status**: Implementation-ready draft
**Decisions**: D-01 (C++) CLOSED
**Stack**: CMake + GCC 13 / Clang 17 + Google Test
**CI Platform assumption**: GitHub Actions (working assumption; adaptable to GitLab CI)

---

## Pipeline Overview

```
Push / PR
    │
    ├── format-check     (clang-format — fail fast)
    ├── lint              (clang-tidy — parallel with build)
    │
    ├── build-gcc         (CMake + GCC 13, Debug + Release)
    ├── build-clang       (CMake + Clang 17, Debug)
    │
    ├── test-unit         (Google Test — all unit tests)
    ├── test-simulation   (Deterministic replay tests)
    ├── test-sanitizers   (ASan + UBSan — Debug build)
    │
    ├── static-analysis   (cppcheck or clang-analyzer)
    │
    └── [optional] benchmark  (nightly only, not on PR)
```

---

## GitHub Actions Workflow

### `.github/workflows/ci.yml`

```yaml
name: CI

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main, develop]

env:
  BUILD_TYPE_DEBUG: Debug
  BUILD_TYPE_RELEASE: Release

jobs:

  # ─── STAGE 1: Format Check (fast fail) ───────────
  format-check:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - name: Install clang-format
        run: sudo apt-get install -y clang-format-17
      - name: Check formatting
        run: |
          find engine/ gateway/ risk/ protocol/ -name '*.h' -o -name '*.cpp' | \
            xargs clang-format-17 --dry-run --Werror
          echo "Formatting check passed"

  # ─── STAGE 2: Build (GCC Debug + Release) ────────
  build-gcc:
    runs-on: ubuntu-24.04
    needs: format-check
    strategy:
      matrix:
        build_type: [Debug, Release]
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake g++-13 ninja-build
      - name: Configure
        run: |
          cmake -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=${{ matrix.build_type }} \
            -DCMAKE_CXX_COMPILER=g++-13
      - name: Build
        run: cmake --build build --parallel $(nproc)
      - name: Upload build artifacts
        uses: actions/upload-artifact@v4
        with:
          name: build-gcc-${{ matrix.build_type }}
          path: build/
          retention-days: 1

  # ─── STAGE 2b: Build (Clang Debug) ───────────────
  build-clang:
    runs-on: ubuntu-24.04
    needs: format-check
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake clang-17 ninja-build
      - name: Configure
        run: |
          cmake -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_COMPILER=clang++-17
      - name: Build
        run: cmake --build build --parallel $(nproc)

  # ─── STAGE 3: Unit Tests ─────────────────────────
  test-unit:
    runs-on: ubuntu-24.04
    needs: build-gcc
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Install dependencies
        run: sudo apt-get install -y cmake g++-13 ninja-build
      - name: Build Debug
        run: |
          cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
          cmake --build build --parallel $(nproc)
      - name: Run tests
        run: cd build && ctest --output-on-failure --parallel $(nproc)
      - name: Upload test results
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: test-results
          path: build/Testing/

  # ─── STAGE 3b: Simulation Tests ──────────────────
  test-simulation:
    runs-on: ubuntu-24.04
    needs: build-gcc
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Install dependencies
        run: sudo apt-get install -y cmake g++-13 ninja-build
      - name: Build Debug
        run: |
          cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
          cmake --build build --parallel $(nproc)
      - name: Run simulation tests
        run: cd build && ctest -L simulation --output-on-failure

  # ─── STAGE 3c: Sanitizer Tests ───────────────────
  test-sanitizers:
    runs-on: ubuntu-24.04
    needs: build-gcc
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Install dependencies
        run: sudo apt-get install -y cmake g++-13 ninja-build
      - name: Build with sanitizers
        run: |
          cmake -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_COMPILER=g++-13 \
            -DENABLE_SANITIZERS=ON
          cmake --build build --parallel $(nproc)
      - name: Run tests with ASan + UBSan
        run: cd build && ctest --output-on-failure --parallel $(nproc)
        env:
          ASAN_OPTIONS: detect_leaks=1:halt_on_error=1
          UBSAN_OPTIONS: halt_on_error=1

  # ─── STAGE 4: Static Analysis ────────────────────
  static-analysis:
    runs-on: ubuntu-24.04
    needs: format-check
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Install dependencies
        run: sudo apt-get install -y cmake g++-13 cppcheck ninja-build
      - name: Configure (for compile_commands.json)
        run: |
          cmake -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_COMPILER=g++-13 \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
      - name: Run cppcheck
        run: |
          cppcheck --project=build/compile_commands.json \
            --enable=warning,performance,portability \
            --error-exitcode=1 \
            --suppress=missingIncludeSystem \
            --inline-suppr
```

### `.github/workflows/benchmark.yml` (Nightly)

```yaml
name: Benchmark

on:
  schedule:
    - cron: '0 3 * * *'  # 03:00 UTC daily
  workflow_dispatch:

jobs:
  benchmark:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Install dependencies
        run: sudo apt-get install -y cmake g++-13 ninja-build
      - name: Build Release
        run: |
          cmake -B build -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_CXX_COMPILER=g++-13
          cmake --build build --parallel $(nproc)
      - name: Run benchmarks
        run: cd build && ctest -L benchmark --output-on-failure
      - name: Upload benchmark results
        uses: actions/upload-artifact@v4
        with:
          name: benchmark-results-${{ github.sha }}
          path: build/benchmark_results/
```

---

## Branch and PR Rules

| Rule | Setting |
|------|---------|
| Default branch | `main` |
| Development branch | `develop` |
| Feature branches | `feature/<name>` from `develop` |
| PR required for merge to `develop` | Yes |
| PR required for merge to `main` | Yes (release only) |
| CI must pass before merge | Yes (all jobs green) |
| Minimum reviewers for `engine/core/` | 2 |
| Minimum reviewers for other modules | 1 |
| Force push to `main` | Blocked |
| Delete branch after merge | Yes |

---

## `.clang-format`

```yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 100
AllowShortFunctionsOnASingleLine: Empty
AllowShortIfStatementsOnASingleLine: Never
AlwaysBreakTemplateDeclarations: Yes
BreakBeforeBraces: Attach
PointerAlignment: Left
NamespaceIndentation: None
SortIncludes: CaseSensitive
IncludeBlocks: Regroup
```

---

## `.clang-tidy`

```yaml
Checks: >
  -*,
  bugprone-*,
  cppcoreguidelines-*,
  modernize-*,
  performance-*,
  readability-*,
  -modernize-use-trailing-return-type,
  -readability-magic-numbers,
  -cppcoreguidelines-avoid-magic-numbers
WarningsAsErrors: >
  bugprone-*,
  performance-*
HeaderFilterRegex: '(engine|gateway|risk|protocol)/.*'
```

---

## Pipeline Duration Targets

| Stage | Target Duration | Notes |
|-------|:---------------:|-------|
| Format check | < 30 seconds | Fast-fail gate |
| Build (single config) | < 3 minutes | Ninja parallel build |
| Unit tests | < 2 minutes | Google Test |
| Simulation tests | < 5 minutes | Replay scenarios |
| Sanitizer tests | < 5 minutes | ASan + UBSan overhead |
| Static analysis | < 3 minutes | cppcheck |
| **Total PR pipeline** | **< 15 minutes** | All stages parallel where possible |
| Benchmark (nightly) | < 10 minutes | Not on PR path |
