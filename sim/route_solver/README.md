# Route solver tests

- `make -C sim test-route-solver` runs the standalone feasibility, allocation
  and margin tests. It only needs a C++17 compiler.
- `make -C sim test-route-optimizer` runs the production solver through the
  simulator, including 20-point navigation, admission timing, unsupported tasks,
  GPS loss, cylinder entry, and route import limits.
- `make -C sim test-route-guidance` covers the live simulator lab, LCD guidance,
  and the pointer pixel fixture.

The desktop solver used as a comparison in the tests is independent of the
firmware build. See [firmware behavior and hardware results](FIRMWARE.md) and
[the live lab guide](GUIDANCE.md).
