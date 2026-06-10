# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

SwingBot — a C++17 swing-trading bot for QQQ (EMA crossover + RSI strategy) with IBKR/Alpaca broker integrations, a CSV backtester, SQLite persistence, and a WebSocket dashboard feed. Comments, log messages, and documentation are in French — keep new code consistent with that.

## Build & Test Commands

The build requires the vcpkg toolchain (Boost.Beast/Asio/System, nlohmann-json, sqlite3, CURL, GTest). The canonical dev environment is the Docker container `swing_bot_dev` (Ubuntu 24.04 + vcpkg at `/vcpkg`), driven from a Windows host via `dev.ps1`:

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

Run the bot: `./build/swing_bot` (paper trading, default) · `--live` (real money, 5 s abort window) · `--account DU123456`. IBKR requires an authenticated Client Portal Gateway on `https://localhost:5000`; Alpaca builds need `ALPACA_KEY`/`ALPACA_SECRET` env vars.

## Architecture

Almost everything is **header-only** (implementation in `.hpp`); the single translation unit shared by all targets is `include/core/ws_server.cpp`. There are two distinct layers:

### 1. Trading engine — `trading::` namespace, interface-driven

`include/core/Interfaces.hpp` defines the abstractions: `IDataFeed`, `IBroker`, `IStrategy`, `IRiskManager`, `ILogger`, `IIndicator<T>`. `trading::TradingBot` (`include/bot/TradingBot.hpp`) is the orchestrator and depends **only** on these interfaces; concrete implementations are injected in the composition root (the `main_*.cpp` files). `runOnce()` is the single, unit-testable trading cycle: fetch bars → evaluate strategy → check exits (stop-loss/take-profit/trailing via `IRiskManager`) → enter/exit position.

- `include/brokers/` — implementations: `IBKRDataFeed`/`IBKRBroker` (CP Gateway REST), `AlpacaDataFeed`/`AlpacaBroker` (REST), `PaperBroker` (simulated fills), `CsvDataFeed` (backtesting from `QQQ.csv`)
- `include/strategies/` — `SwingStrategy` (EMA fast/slow crossover + RSI filter, configured via `SwingConfig`), `DayTradeStrategy`
- `include/indicators/` — `EMA`, `RSI`, `CrossoverDetector` (with EMA-convergence warmup)
- `include/backtest/BackTester.hpp` — full backtest engine + performance report (Sharpe, drawdown, win rate…)
- `include/bot/Mocks.hpp` — hand-rolled mocks (`trading::mocks`) with bar-series builders used by tests

### 2. Operational infrastructure — global namespace, `include/core/`

- `bot_state.h` — `BotState`: thread-safe shared dashboard state (mutex-guarded, JSON serialization via nlohmann)
- `ws_server.h/.cpp` — `WsServer`: Boost.Beast WebSocket broadcast server (PIMPL) feeding a React dashboard on port 9001
- `db_logger.h` — `DbLogger`: SQLite (WAL) persistence — `logs`, `trades`, `equity_curve`, `signals` tables
- `watchdog.h` — `Watchdog`: heartbeat-based freeze detection with email/Twilio-SMS/Discord-Slack-webhook alerts via libcurl

The composition roots tie both layers together: the main loop calls `bot.runOnce()`, then `watchdog.heartbeat()`, syncs `BotState` from the broker account, snapshots equity to SQLite, and broadcasts JSON over the WebSocket.

### Composition roots / entry points

Only **`main_ibkr.cpp`** is built (target `swing_bot` in CMakeLists.txt). `main.cpp` (CSV backtester), `main_alpaca.cpp`, and `main_v2.cpp` are alternate entry points — switching means editing `add_executable(swing_bot …)`.

## Gotchas

- **Two unrelated `BotState` types exist**: `trading::BotState` in `TradingBot.hpp` (position state: `inPosition`, `buyPrice`, `peakPrice`, `holdDays`) and global `::BotState` in `core/bot_state.h` (dashboard state). `main_ibkr.cpp` uses both.
- `main.cpp` includes `"backtest/Backtester.hpp"` but the file is `BackTester.hpp` — a latent case-sensitivity break on Linux if that entry point is ever re-enabled.
- The root `gitignore` file is missing its leading dot, so nothing is actually ignored — `cmake-build-debug/` (stale CLion artifacts, including a vendored googletest under `_deps/`) is committed. Exclude it when searching the codebase.
- `docker-compose.yml`'s test services reference `target: builder`, which only exists in `Dockerfile.multistage`, not the default `Dockerfile`.

## Testing conventions

Tests live in `tests/unit/` and `tests/integration/` (one file per component, five each). `gtest_discover_tests` registers every test as a separate process with prefixes `Unit.` / `Integration.` and hard timeouts (15 s unit, 30 s integration) — tests must be fast and fully independent. Suites are named `<Component>Unit` / `<Component>Integration`. `WsServer` tests construct the server with `port=0` and read `actual_port()` so parallel test processes never collide on a port. Unit tests use the mocks in `include/bot/Mocks.hpp` — no network access.
