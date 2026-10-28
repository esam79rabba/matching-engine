# Contributing

Thanks for your interest in the Matching Engine Platform. This guide covers how to build, test, and submit changes.

## Requirements

- C++20 compiler: GCC 11+, Clang 15+, or AppleClang 15+
- CMake 3.20+
- OpenSSL 3.0+

## Building and Testing

```bash
cmake -S . -B build -DENABLE_QUICKFIX=OFF && cmake --build build -j && ctest --test-dir build
```

The build is warning-clean under `-Wall -Wextra -Wpedantic -Werror`. Keep it that way: a change that introduces a warning will fail the build.

## Running the Components

- Portal: see [portal/README.md](portal/README.md)
- Participant terminal: see [participant-terminal/README.md](participant-terminal/README.md)

## Branching and Commits

- Create feature branches off `main`. Do not commit directly to `main`.
- Write commit messages in [Conventional Commits](https://www.conventionalcommits.org/) form: `feat`, `fix`, `docs`, `refactor`, `test`, or `chore`, followed by a short description.
- Keep commits focused. Prefer several small commits over one large one.
- A DCO line (`Signed-off-by: Your Name <you@example.com>`) is appreciated but not required.

## Tests

- New features need tests. Bug fixes should include a regression test where practical.
- Do not disable or skip existing tests to make a change pass.
- Aim to avoid regressing test coverage.

## Reporting Bugs

Open an issue on GitHub Issues with steps to reproduce, the commit or release you tested, and the relevant logs or output. For security vulnerabilities, do not open a public issue; follow [SECURITY.md](SECURITY.md) instead.

## Code of Conduct

Participation in this project is governed by the [Code of Conduct](CODE_OF_CONDUCT.md).

## Reviews

A maintainer will review pull requests within 2 weeks. Larger changes may take longer; if you have not heard back after that, leave a comment on the pull request.
