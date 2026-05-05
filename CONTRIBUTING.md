# Contributing

Thank you for your interest in contributing to this project.

This guide explains how to set up the project, propose changes, and open a Pull Request.

## Before You Start

Before working on a change, please check whether there is already an open issue or Pull Request related to it.

If you want to make a large change, add a new feature, or modify the project architecture, please open an issue first so the proposal can be discussed before implementation.

Small bug fixes, documentation improvements, and minor cleanups can usually be submitted directly as a Pull Request.

## Fork and Clone the Repository

First, fork the repository on GitHub.

Then clone your fork locally:

```bash
git clone https://github.com/PabloPicose/SimplyNodeFramework.git
cd SimplyNodeFramework
```

Add the original repository as `upstream`:

```bash
git remote add upstream https://github.com/PabloPicose/SimplyNodeFramework.git
```

Keep your fork up to date:

```bash
git fetch upstream
git checkout main
git merge upstream/main
```

## Create a Branch

Create a dedicated branch for your change:

```bash
git checkout -b feature/short-description
```

Use a clear branch name, for example:

```text
feature/add-config-loader
fix/crash-on-empty-file
docs/update-build-instructions
refactor/dependency-scanner
```

Avoid working directly on `main`.

## Build the Project

Please make sure the project builds successfully before opening a Pull Request.

For a CMake-based project, the usual build steps are:

```bash
cmake -S . -B build
cmake --build build
```

If the project requires specific dependencies, compilers, Qt versions, environment variables, or platform-specific setup, please follow the instructions in the `README.md`.

## Make Your Changes

Try to keep each Pull Request focused on a single purpose.

Good examples:

- Fix one specific bug.
- Add one specific feature.
- Improve one part of the documentation.
- Refactor one isolated component.

Avoid mixing unrelated changes in the same Pull Request. For example, do not combine a bug fix, a formatting cleanup, and a new feature unless they are directly related.

## Code Style

Please follow the style already used in the project.

Before submitting your Pull Request, check that:

- The project builds successfully.
- No unnecessary warnings are introduced.
- The code is readable and maintainable.
- Names for variables, functions, classes, and files are clear.
- Formatting changes are not mixed with unrelated code changes.
- There is a .clang-format in the repository, and your code is formatted according to it.

## Tests

If the project includes tests, run them before opening the Pull Request.

For CMake projects using CTest:

```bash
ctest --test-dir build
```

If you fix a bug or add a new feature, add or update tests when reasonable.

If tests are not available, explain in the Pull Request how you manually verified the change.

## Commit Messages

Use clear and descriptive commit messages.

Good examples:

```text
Fix crash when loading an empty config file
Add option to disable MySQL checks
Update build instructions for Red Hat 8
Refactor dependency scanning logic
```

Avoid vague messages such as:

```text
fix
update
changes
wip
stuff
```

## Open a Pull Request

Push your branch to your fork:

```bash
git push origin feature/short-description
```

Then open a Pull Request on GitHub.

In the Pull Request description, include:

- What problem the change solves.
- What changes were made.
- How the change was tested.
- Any limitations or known issues.
- Screenshots, logs, or examples if they help explain the change.

## Review Process

Your Pull Request may receive comments or requested changes.

If changes are requested, update the same branch. Do not open a new Pull Request for the requested fixes.

The Pull Request will be reviewed based on correctness, maintainability, scope, and consistency with the rest of the project.

## License

By contributing to this project, you agree that your contribution will be licensed under the same license as the repository.
