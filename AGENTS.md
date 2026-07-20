# AGENTS.md

## Scope
- Applies to the entire repository.
- Favor minimal, reviewable diffs over broad refactors.

## Project Map
- `src/`, `inc/`: core daemon and public headers.
- `tests/`: Catch2 unit tests (`test_*.cpp`).
- `config/`: runtime config examples and D-Bus policy.
- `dbus/`: D-Bus XML.
- `schemas/`: JSON schema contracts.
- `docs/`: operator and contributor docs.
- `web/`: optional Go + Preact UI (`EDGE_WEB_UI=ON`).

## Build and Validate
- Debug + tests + sanitizers:
  ```bash
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DEDGE_BUILD_TESTS=ON -DEDGE_ENABLE_SANITIZERS=ON -DEDGE_FETCH_SDBUSCPP=ON
  cmake --build build -j "$(nproc)"
  ctest --test-dir build --output-on-failure -j "$(nproc)"
  ```
- Release:
  ```bash
  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DEDGE_FETCH_SDBUSCPP=ON
  cmake --build build -j "$(nproc)"
  ```
- Snapshot schema check:
  ```bash
  python3 scripts/validate_schema.py /run/health/state.json
  ```
- If `web/ui` changes:
  ```bash
  cd web/ui && npm run lint
  ```

## Coding Rules
- C++23 only, no compiler extensions.
- Keep strict warnings clean (`-Wall -Wextra -Wpedantic -Wshadow` and configured `-Werror` set).
- Preserve naming conventions: `snake_case` (files/functions), `PascalCase` (types), `SCREAMING_SNAKE_CASE` (macros).
- Prefer existing `std::expected`-style error handling patterns.

## Behavior and Compatibility
- Preserve offline-first behavior and graceful degradation when optional services are missing.
- Keep snapshot schema compatibility unless the task explicitly requires schema change.
- Keep atomic write semantics intact.

## D-Bus and Generated Header
- If `dbus/edge-health-manager.xml` changes, regenerate and commit:
  ```bash
  cmake --build build --target edge-dbus-generate
  ```
- Commit `inc/generated/HealthManagerAdaptor.hpp` with XML changes.

## Testing Expectations
- Update/add tests in `tests/test_<module>.cpp` for behavior changes.
- Cover both happy-path and failure/degraded-path behavior.
- Keep tests deterministic and quick.

## Documentation Policy
- Do not duplicate long docs in PR text or comments; point to canonical docs.
- Update docs when behavior/config/schema/workflow changes:
  - `README.md` for landing-page level messaging.
  - `docs/usage.md` for operator behavior.
  - `docs/development.md` for build/dev workflows.
  - `docs/edge.health.state.v1.1.md` (current contract) and `schemas/` for schema updates.

## Commit and PR Hygiene
- Conventional Commits only: `feat|fix|refactor|build|docs|test|chore|perf(scope): message`.
- Keep PR scope focused and include test evidence.
- Do not revert unrelated local changes.

## Security Hygiene (Public Repo)
- Never commit secrets, private keys, internal hostnames/URLs, or production identifiers.
- Use placeholders (`$REMOTE`, `user@host`, `/path/to/sdk`) in docs/scripts.
