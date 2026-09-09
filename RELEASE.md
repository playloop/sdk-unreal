# Releasing the Playloop Unreal SDK

Version 0.5.0 is distributed from this Git repository. Install the pinned
`v0.5.0` tag using the instructions in README.md. Package registry and
marketplace publication are not required for Git installation.

For a release:

1. Update the version and CHANGELOG.md.
2. Run the repository tests and documentation checks.
3. Run `.githooks/release-check.sh` from a clean checkout.
4. Create and push the matching version tag after the changes are merged.

Users clone the tagged source into their project Plugins directory.
GitHub also provides a source archive for every tag.
