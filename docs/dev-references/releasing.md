---
title: Creating a firmware release
description: Checklist and required evidence before publishing a Leaf firmware release.
---

# Creating a firmware release

This checklist applies to a new **published firmware release**, including prereleases.
Runtime resource assessment reports are release evidence, not a per-PR requirement.
Publication remains an explicit operator action; this document does not automate it.

## Prepare the candidate

- Select a clean candidate commit from main and record the full commit ID, previous published
  release, intended version/tag, and supported hardware variants.
- Establish the intended version tag on that candidate before building assessment artifacts.
  `versioning.py` derives firmware identity from Git tags and dirty state, so adding a tag
  after assessment can change the binary even when the commit is unchanged. Creating/pushing
  the tag remains a separate operator action; this checklist does not authorize it by itself.
- Run formatting and the relevant regression checks. Build the matching PlatformIO release
  environments. Preserve firmware binaries, ELF files, build diagnostics, and hashes.
- Follow the repository hardware verification instructions, selecting the connected unit's
  correct variant. Record what was built, flashed, and actually exercised.

## Required runtime reports

Before publication, create and review simulator and real-device reports using
[Runtime assessment](runtime-assessment.md). Stage 1 uses manual capture and workloads.

- Identify the exact candidate, binary/executable hashes, settings, scenario/profile,
  toolchain, device, SD card, duration, and actual workload coverage. Simulator results must
  remain distinct from measured hardware deadlines and memory.
- Capture supported flight, web-app, and transition behavior separately as appropriate.
  Document omitted features and limitations. Preserve report HTML/JSON, raw captures,
  metadata, console/crash evidence, and comparisons with an approved compatible baseline.
- Review all 100 slots for occupancy, late starts, deadline crossings and coalesced ticks.
  Review internal heap, largest block, stacks, allocation failures, monitoring overhead,
  and incomplete coverage. Investigate material changes; record accepted existing issues.
- A report from another commit, an unavailable device, missing evidence, or a crash is not a
  passing assessment. An exception must explicitly name the missing assessment, reason,
  candidate, and approving maintainer in the release checklist/draft.
- Attach reports as release assets, or link durable retained artifacts from the release draft.
  Save the reviewed baseline identity for the next release. Do not replace a baseline silently.

Reports currently require human review; automatic pass/fail thresholds, burdened workloads,
test-station orchestration, and publication enforcement are deferred. No PR gate is added.

## Draft, publish, and verify

- Draft user-facing release notes from the fixed previous-release/candidate comparison using
  the repository's generate-release-notes skill. Keep runtime evidence and limitations in
  the maintainer checklist rather than cluttering public change bullets.
- Create a draft release and verify the tag resolves to the tested candidate. Check that each
  advertised hardware variant has the correct artifact and that required reports or explicit
  exceptions are present before publishing.
- The current GitHub build workflow listens for `release: created`, and also supports manual
  dispatch. Obtain and assess candidate artifacts before publication, using manual dispatch
  against the fixed tag if CI artifacts are needed. Do not depend on a publication-time build
  to supply required pre-publication evidence.
- Account for any later asset rebuild/upload. Verify hashes against assessed binaries, including
  tag identity and toolchain/host differences. The reproducibility script normalizes paths and
  timestamps but explicitly does not equalize all precompiled library strings across host OSes.
  Do not silently substitute different firmware for assessed artifacts; record a discrepancy
  and repeat affected assessment/review before promoting that artifact.
- Publish only after review. Check public notes, asset names, versions metadata, and the
  published tag. Retain the full assessment bundle and any exceptions with the release.
