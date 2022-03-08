# Git Hooks

This folder contains client-side hooks that perform:
- verify that the source code meet the formatting style (using clang-format with
  the Google style).

## Enable the hooks

The hooks can be enable by running:
```bash
git config core.hooksPath git_hooks
```

## pre-commit

### Code style (clang-format with the Google style)

Currently a pre-commit hook is used to check the formatting style of the .h and
.cc files. If the test fails, follow the recommendation to resolve the issues.

