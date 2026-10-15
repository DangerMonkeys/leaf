---
name: generate-release-notes
description: Draft concise, nontechnical Leaf release notes from changes between a previous release tag and a target branch or commit. Use when preparing Leaf release notes or updating an existing release draft.
---

# Generate release notes

Create a short, publishable draft for Leaf users from the actual repository changes. Save it in `release-notes/<version>.md` unless the user specifies another location. Use `release-notes/v0.2.4.md` as a style example, not a source of claims for later releases.

## Establish the comparison

- Follow the repository's `AGENTS.md`. Inspect the checkout and existing changes before editing; preserve unrelated work.
- Use the release version and previous release tag provided by the user. If the previous release is unspecified, identify the latest published stable release reachable from the target; do not silently use a prerelease or an unrelated newer tag. Clarify only if the baseline is ambiguous.
- Default the target to the latest `origin/main` when preparing an upcoming release. Refresh the remote branch and tags first. If refresh is unavailable, say the draft uses the locally available revision.
- Record the resolved base and target commit IDs. Compare that fixed pair throughout the review so new upstream commits cannot silently change the scope.
- Review every commit in `<base>..<target>` and the cumulative diff. Read relevant source, documentation, and tests to verify user-visible behavior. Inspect individual commits when titles are vague or combine several changes. Check for reverts and later superseding changes.

## Write for Leaf users

Use these two main categories, each with the three subsections shown:

```markdown
# Leaf <version>

## Leaf Vario firmware

### A. New features

### B. Improvements

### C. Bug fixes and stability

## Other tools and support

### A. New features

### B. Improvements

### C. Bug fixes and stability
```

- Include the device-hosted Leaf Web App under firmware. Put the simulator, public website, documentation, manufacturing tools, and other supporting tools in the second category.
- Keep bullets short and concrete, using familiar words and the benefit or visible behavior. Avoid implementation jargon, commit titles, PR numbers, and promotional claims in public copy.
- Group related commits into one bullet where useful. Cover every meaningful user-facing change, including secondary changes in mixed commits. Omit internal refactoring, research assets, vendored dependencies, and test-only changes unless they have a relevant user or support benefit.
- Classify relative to the previous release, not an intermediate commit. A newly documented feature may already exist. Improvements made while developing a wholly new feature belong with that new feature.
- Verify important conditions such as experimental opt-ins, hardware restrictions, and whether a default changes existing saved preferences. Do not imply a settings reset is required unless the evidence says so.
- Do not claim placeholder buttons, planned features, or backlog proposals are implemented. Distinguish simulated pilots from online multiplayer.
- If a subsection has no qualifying changes, write "No changes in this category." Do not invent an entry to fill it.

## Save and check

- Keep comparison metadata and a compact commit-to-bullet coverage record in an HTML comment at the top of the draft, separate from public copy. Explain intentional internal-only omissions there.
- Check each bullet against the code and the baseline, and account for all commits. Remove duplication and unsupported claims. Check Markdown and whitespace with `git diff --check`; inspect new files explicitly because unstaged untracked files are absent from `git diff`.
- A documentation-only task does not require formatting firmware, compiling, scanning ports, or flashing hardware.
- Leave the draft and any requested instruction changes reviewable in the current checkout. Commit, push, tag, create a release, or publish only when the user requests it.
- Return links to the draft and this workflow, state the comparison used, and report whether the files are uncommitted, committed, or pushed. The user can request the next run with "Generate release notes for <version> since <previous-tag>."
