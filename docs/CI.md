# Prototype continuous integration

The `Prototype CI` workflow gives pull requests and `main` repeatable feedback on
the portable engine and the private Apple Silicon package. It is an engineering
check for the approved prototype and recorder extension, not release certification.

## Automated coverage

The portable Ubuntu job runs the numeric/temporal core suite, the 50-cycle worker
lifecycle suite, publisher-name and adapter-diagnostic tests, package-verifier
tests, and the same three C++ suites under AddressSanitizer and
UndefinedBehaviorSanitizer. Sanitizer binaries use `build/sanitizers/`, so they do
not replace normal test outputs.

The Mac job runs on GitHub's `macos-15` standard arm64 image. It fetches the Rack
2.6.6 SDK and Syphon sources through the repository's checksum-pinned scripts,
builds the plugin, and runs the portable, Rack SDK-linked adapter/host, and Syphon
SDK loopback tests. It then calls the existing package script and verifies:

- the staged package and zip contain exactly the redistributable source tree used
  by the package script, including every current `res/`, `examples/`, and
  `licenses/` file;
- `plugin.json` is unchanged, declares RVX and has unique module slugs;
- the plugin is arm64-only, uses Rack's expected runtime install name, contains no
  local SDK path, and has a valid ad-hoc signature;
- the Syphon notice and license are present; and
- the zip has one safe `RVX/` root, no symlinks or traversal entries, and bytes
  identical to the staged folder.

The verified zip is retained as a private workflow artifact for seven days. The
workflow grants the token only read access to repository contents, stores no
checkout credentials, uses bounded job timeouts, and cancels obsolete runs for
the same pull request or branch. It uses `pull_request`, never
`pull_request_target`, and does not read secrets.

GitHub's hosted-runner reference lists `macos-15` as an M1 arm64 standard runner
for private repositories. The runner-image inventory confirms the label and
current image contents. The workflow pins the reviewed action revisions rather
than floating tags:

- `actions/checkout` v6.1.0 at
  `d23441a48e516b6c34aea4fa41551a30e30af803`
- `actions/upload-artifact` v7.0.0 at
  `bbbca2ddaa5d8feaa63e36b76fdaad77386f024f`

Sources: [GitHub-hosted runners reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners),
[macOS 15 arm64 image](https://github.com/actions/runner-images/blob/main/images/macos/macos-15-arm64-Readme.md),
[checkout releases](https://github.com/actions/checkout/releases), and
[upload-artifact releases](https://github.com/actions/upload-artifact/releases).

## Local reproduction

Portable checks need a C++17 compiler and Python 3:

```sh
make test test-lifecycle test-rack-adapter
python3 tests/package_verifier_test.py
make test-sanitizers
```

On Apple Silicon macOS, with Command Line Tools available:

```sh
make deps
make -j3
make test test-lifecycle test-rack test-syphon
make dist
python3 scripts/verify-package.py
```

`RACK_DIR` and `SYPHON_DIR` can point to equivalent existing pinned dependency
trees for local reproduction. The package verifier always compares the result to
the current checkout, so adding or removing nested resources changes the expected
package contents automatically.

## Coverage limits

The hosted Mac job has no physical audio device, user Rack profile, interactive
Rack window, or independent external Syphon application. Its SDK loopback is a
single-process noninteractive transport check. CI does not establish physical
CoreAudio input or reconnect, cable dragging and native UI interaction, hidden or
recreated windows, a live external source-to-Rack relay, audiovisual latency,
long stress/performance budgets, full combined acceptance, or resemblance to LZX
hardware. Those scenarios remain open in issue #13 and the native validation
record. The deferred waveform work in issue #16 is outside this workflow.

The first remote run is evidence about the exact workflow revision and hosted
image only. Record its URL and result in the pull request; local workflow syntax
and equivalent commands do not substitute for that run.


## First hosted run and clock-test repair

The first hosted run on September 9, 2026 ([34404100441](https://github.com/kevindjacobson/rvx-vcv-rack/actions/runs/34404100441),
head `2414923`) passed the Ubuntu suites and Mac dependency fetch/build. The Mac
core suite exposed an existing assertion that equated completed renders with
scheduled frame numbers, despite the engine's documented overload skipping.
Later Mac SDK/package steps were skipped in that failed run.

The test now records worker frame IDs, checks exact completed-render count and
last displayed identity, and bounds scheduled age by completed renders plus
skips. Skips can be recorded after the final render before shutdown. A deliberate
100 ms backend stall also verifies that skipping is actually exercised. Engine
behavior and timing contracts are unchanged. Subsequent hosted results are
recorded in PR #29; the first failed run remains part of the evidence.
