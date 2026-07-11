# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

SwingBot — a C++17 swing-trading bot for QQQ (EMA 9/21 crossover + RSI + SMA200 regime filter) with IBKR/Alpaca broker integrations, a CSV backtester, an offline validation harness (walk-forward / grid / Monte-Carlo), SQLite persistence, and a WebSocket dashboard feed. QQQ is the only traded symbol; SPY/IWM/MDY (and `_max` long-history variants back to ~1999) are validation-only assets. Comments, log messages, and documentation are in French — keep new code consistent with that.

**Status: paper-only.** No out-of-sample edge has been demonstrated (see ROADMAP.md, Sprint 8-ter). Real-money `--live` stays mechanically locked — see "Live-safety rules" below.

## Build & Test Commands

The build needs Boost.Beast/Asio/System, nlohmann-json, sqlite3, CURL and GTest — either via the vcpkg toolchain or plain system packages (the CI path: `apt-get install libboost-dev libboost-system-dev nlohmann-json3-dev libcurl4-openssl-dev libsqlite3-dev libgtest-dev googletest ninja-build`, then configure **without** `-DCMAKE_TOOLCHAIN_FILE`). The canonical dev environment is the Docker container `swing_bot_dev` (Ubuntu 24.04 + vcpkg at `/vcpkg`), driven from a Windows host via `dev.ps1`:

```powershell
.\dev.ps1 start          # start container + configure CMake (once)
.\dev.ps1 build          # incremental build
.\dev.ps1 test           # all tests
.\dev.ps1 unit           # unit tests only
.\dev.ps1 integ          # integration tests only
.\dev.ps1 watch <name>   # re-run one test in a loop
.\dev.ps1 shell          # bash inside the container
```

Inside the container (or any Linux box with vcpkg), the underlying commands are:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake -G Ninja
cmake --build build -j$(nproc)

cd build && ctest --output-on-failure -j4          # all tests
ctest -R '^Unit\.' --output-on-failure -j4         # unit only
ctest -R '^Integration\.' --output-on-failure      # integration only
ctest -R 'SwingStrategyUnit.FlatMarketReturnsHold' # single test (regex on name)
# or: ./build/unit_tests --gtest_filter='SwingStrategyUnit.*'
```

CMake defines **four executables**: `swing_bot` (the prod bot, from `main_ibkr.cpp`), `validate` (the Sprint-7 validation harness, from `main_validate.cpp`), `unit_tests`, and `integration_tests`. To inspect the edge (walk-forward IS/OOS, grid sensitivity, Monte-Carlo, multi-asset data-quality audit) build and run the harness — its numbers are human-readable and locked by the integration tests:

```bash
cmake --build build --target validate
./build/validate
```

CSV paths (`SWINGBOT_*_CSV`, `SWINGBOT_*_MAX_CSV`) and the prod-config path (`SWINGBOT_PROD_CONFIG_JSON`) are injected as compile definitions by CMake, so tests and `validate` find the data without a working-directory assumption.

Run the bot: `./build/swing_bot` (paper trading, default) · `--live` (real money — gated: see "Live-safety rules" below) · `--account DU123456`. IBKR requires an authenticated Client Portal Gateway on `https://localhost:5000`; Alpaca builds need `ALPACA_KEY`/`ALPACA_SECRET` env vars. Production config lives in `config/prod.json` (strategy + regime filter + kill-switch thresholds + `liveTradingApproved`), loaded and strictly validated at startup by `include/strategies/ConfigLoader.hpp` — the SAME file is backtested by the prod-config golden tests. Alert-channel secrets come from `SWINGBOT_*` environment variables (`.env.example`).

**CI** (`.github/workflows/ci.yml`, system-packages path — no vcpkg) runs six jobs on every push/PR, all blocking: Debug build+ctest, Release build+ctest (the prod binary shape), ASan+UBSan, a targeted blocking TSan pass over the concurrent suites (`WsServer|Watchdog|BotState`), static lint (cppcheck), and coverage (gcovr, 95% line threshold on `include/`).

## Architecture

Almost everything is **header-only** (implementation in `.hpp`); the single translation unit shared by all targets is `include/core/ws_server.cpp`. There are two distinct layers:

### 1. Trading engine — `trading::` namespace, interface-driven

`include/core/Interfaces.hpp` defines the abstractions: `IDataFeed`, `IBroker`, `IStrategy`, `IRiskManager`, `ILogger`, `IIndicator<T>`, `IStateStore` (persisted position state). `trading::TradingBot` (`include/bot/TradingBot.hpp`) is the orchestrator and depends **only** on these interfaces; concrete implementations are injected in the composition root (the `main_*.cpp` files). `runOnce()` is the single, unit-testable trading cycle: fetch bars → evaluate strategy → check exits (stop-loss/take-profit/trailing via `IRiskManager`) → enter/exit position.

- `include/brokers/` — implementations: `IBKRDataFeed`/`IBKRBroker` (CP Gateway REST), `AlpacaDataFeed`/`AlpacaBroker` (REST), `PaperBroker` (simulated fills), `CsvDataFeed` (backtesting from the repo-root CSVs)
- `include/strategies/` — `SwingStrategy` (EMA fast/slow crossover + RSI filter + SMA200 regime filter, configured via `SwingConfig`), `DayTradeStrategy`; `ConfigLoader.hpp` parses `config/prod.json` into `ProdSettings`; `ProdConfig.hpp` (`prodSwingConfig()`) returns the production wiring backtested by the golden tests
- `include/indicators/` — `Indicators.hpp` (EMA, SMA, RSI, with EMA-convergence warmup) and `DayIndicators.hpp` (ATR + intraday indicators)
- `include/backtest/` — `BackTester.hpp` (backtest engine + `ReplayDataFeed` anti-look-ahead + performance report: Sharpe, drawdown, win rate…). Validation harness (Sprint 7): `WalkForward.hpp` (IS/OOS walk-forward), `GridOptimizer.hpp` (grid search judged OOS), `MonteCarlo.hpp` (bootstrap CAGR/drawdown distribution), `DataQuality.hpp` (multi-asset data-quality audit)
- `include/models/` — `Models.hpp` (`Bar`, `Signal`, `Order`, `Position`, `Account`, `KillSwitchConfig`, `RiskConfig`, `trading::BotState`) and `Result.hpp` (`Result<T>` explicit error channel)
- `include/bot/Mocks.hpp` — hand-rolled mocks (`trading::mocks`: `MockDataFeed` with bar-series builders, `MockStrategy`, `MockBroker`, `MockLogger`, `MockStateStore`) used by unit tests

### 2. Operational infrastructure — global namespace, `include/core/`

- `bot_state.h` — `BotState`: thread-safe shared dashboard state (mutex-guarded, JSON serialization via nlohmann)
- `ws_server.h/.cpp` — `WsServer`: Boost.Beast WebSocket broadcast server (PIMPL) feeding a React dashboard on port 9001
- `db_logger.h` — `DbLogger`: SQLite (WAL) persistence — `logs`, `trades`, `equity_curve`, `signals` tables
- `state_store.h` — `SqliteStateStore` (implements `IStateStore`): persists `trading::BotState` position state to SQLite
- `watchdog.h` — `Watchdog`: heartbeat-based freeze detection with email/Twilio-SMS/Discord-Slack-webhook alerts via libcurl
- `HttpClient.hpp` — shared libcurl HTTP client used by the REST brokers/feeds and the watchdog; `curl_global.h` — `CurlGlobal` RAII guard for the thread-unsafe `curl_global_init/cleanup`
- `market_calendar.h` — US market calendar / Eastern-time helpers (open-close, DST, `usEasternDateOfUtc`)

The composition roots tie both layers together: the main loop re-checks Gateway auth each cycle, calls `bot.runOnce()`, beats `watchdog.heartbeat()` **only if `bot.lastCycleHealthy()`** (a starved heartbeat is how outages become alerts), skips the equity sync when the account is not ACTIVE, and broadcasts JSON over the WebSocket. `include/core/LiveGate.hpp` implements the four-layer `--live` gate.

### Composition roots / entry points

Two entry points are built: **`main_ibkr.cpp`** → `swing_bot` (the prod bot) and **`main_validate.cpp`** → `validate` (the offline validation harness). `main.cpp` (CSV backtester), `main_alpaca.cpp`, and `main_v2.cpp` are alternate roots wired into no target — switching means editing the relevant `add_executable(…)` in CMakeLists.txt.

## Live-safety rules (OVERRIDE everything else)

- **Never enable real-money trading without the edge DoD.** `--live` is mechanically gated: `liveTradingApproved` in `config/prod.json` must be `true`, at least one alert channel must be configured, stdin must be a TTY, and the operator must type « OUI ». The integration test `LiveTradingStaysDisapprovedUntilEdgeDoD` locks the flag to `false` — do NOT flip it or re-pin that test unless the user explicitly decided to go live after the RUNBOOK.md pre-live checklist (decision recorded in the ROADMAP changelog).
- **`config/prod.json` is governed**: any change to it must keep the prod-config golden tests green or come with a documented re-baseline (old → new values in the ROADMAP changelog). Never tweak it casually — it is the file that trades. Its fields are the strategy params (`emaFast/emaSlow`, `rsiPeriod/rsiBuyMax/rsiSellMin`, `stopLossPct/takeProfitPct/trailingStopPct`, `riskPerTradePct`, `minHoldDays`), the regime filter (`smaTrendPeriod`, `rsiSellOnlyIfRegimeDown`, `regimeReentry`), the `killSwitch` block (`maxDailyDrawdownPct`, `maxConsecutiveLosses`, `maxOrdersPerDay`), and `liveTradingApproved`.
- **Secrets never live in code or commits.** All credentials come from `SWINGBOT_*` environment variables (`.env`, git-ignored; template `.env.example`). Never hardcode, log, or commit a credential.
- **Do not modify `prompt-executer-sprint.md`, `prompt-mise-a-jour-roadmap.md`, or any DoD wording on your own.** Propose the diff and wait for an explicit user decision — the process must not be able to rewrite its own guardrails.

## Gotchas

- **Two unrelated `BotState` types exist**: `trading::BotState` in `include/models/Models.hpp` (position state: `inPosition`, `buyPrice`, `peakPrice`, `holdDays`, `stopArmed`, `lastExitDate` — persisted via `IStateStore`) and global `::BotState` in `core/bot_state.h` (dashboard state). `main_ibkr.cpp` uses both.
- The backtester executes orders at the **open of bar i+1** (decision at close of bar i) — the anti-look-ahead convention locked by `BacktesterIntegration.FillsAtNextBarOpenNotAtDecisionClose`. Golden metric values are fill-date based; re-baseline them only for documented behavior changes (see ROADMAP.md changelog).
- The runtime SQLite databases live under `data/` (`data/swingbot_ibkr.db`, `data/swingbot_ibkr_state.db`) — that's the directory docker-compose mounts as a host volume.
- `IBKRDataFeed` delivers **closed bars only**: the forming bar of the current Eastern-time day is dropped (`usEasternDateOfUtc`), matching Alpaca's `end=yesterday`. The resident stop's orderId is re-discovered from IBKR's open orders after a restart (cOID tag `swingbot-SYM-STOP-`).
- **Backtest data lives at the repo root**, not under `data/`: `QQQ.csv`/`SPY.csv`/`IWM.csv`/`MDY.csv` (recent) plus `*_max.csv` long-history variants (~1999+, dot-com/2008 included, regenerated by `scripts/export_total_return.py`). Tests and `validate` reach them through the `SWINGBOT_*_CSV` compile-defs — never hardcode a path.
- The whole build runs under `-Wall -Wextra -Werror` — new warnings are build errors, on every target including tests.

## Testing conventions

Tests live in `tests/unit/` and `tests/integration/` (one file per component — 28 unit and 17 integration files). `gtest_discover_tests` registers every test as a separate process with prefixes `Unit.` / `Integration.` and hard timeouts (15 s unit, 30 s integration) — tests must be fast and fully independent. Suites are named `<Component>Unit` / `<Component>Integration`. `WsServer` tests construct the server with `port=0` and read `actual_port()` so parallel test processes never collide on a port. Unit tests use the mocks in `include/bot/Mocks.hpp` — no network access; the HTTP-client and watchdog-webhook integration tests use the real loopback stub `tests/support/MiniHttpServer.hpp` (also `port=0`).

Load-bearing golden/locked tests (all in `tests/integration/`) — change their numbers only via a documented re-baseline in the ROADMAP changelog:
- `BacktesterIntegration.GoldenProdConfigPerformanceOnQqqCsv` / `…TradeBreakdownOnQqqCsv` — pin the prod-config backtest metrics and trade counts on `QQQ.csv`.
- `BacktesterIntegration.FillsAtNextBarOpenNotAtDecisionClose` — locks the anti-look-ahead fill convention (decision at close[i] → fill at open[i+1]).
- `BacktesterIntegration.LiveTradingStaysDisapprovedUntilEdgeDoD` — asserts the real `config/prod.json` has `liveTradingApproved == false`.
- `CandidateValidationIntegration.Candidate*OosVerdictIsLocked` and `MonteCarloIntegration` golden — freeze the out-of-sample verdict and Monte-Carlo distribution (seed-fixed).

## Documentation map

- `README.md` — overview + paper-trading quickstart (system-packages path).
- `RUNBOOK.md` — operator procedures: startup, alert secrets, pre-live checklist, risk.
- `ROADMAP.md` — the workflow source of truth: sprints, discoveries (`D<n>`), OOS verdicts, test-state, changelog. Driven by `prompt-executer-sprint.md` (run a sprint) and `prompt-mise-a-jour-roadmap.md` (close a sprint) — both governed (see Live-safety rules).
- `CLAUDE.md` — this file: code rules and conventions.
- `documentation/` — only `SwingBot_UML.mermaid` is kept current (class diagram); the PDF/DOCX are marked obsolete in `documentation/README.md`.
