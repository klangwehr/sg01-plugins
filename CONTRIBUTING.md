# Contributing an SG/01 plugin

Contributions are welcome through a pull request to `staging`. By submitting a
contribution, you agree to license it under Apache-2.0 unless its directory
contains a clearly identified compatible third-party licence and notice.

Do not include firmware internals, private credentials, signing keys, or
vendor code whose redistribution terms are unknown. Use only the pinned `sdk`
submodule. A pull request must update the module version and descriptor version
when it changes a published module or the SDK revision.

Public CI builds and validates code only. Klangwehr reviews approved modules
before they can be signed and added to an official catalog.
# Official release automation

After an approved merge to `staging`, the changed modules are built and the
repository requests a protected `rc` catalog publication. An approved merge to
`main` does the same for `stable`. The public workflow never receives signing
or cloud credentials. It needs the organization-managed `CORE_DISPATCH_TOKEN`
only to request the private publisher; that credential must be a narrowly
scoped GitHub App installation token (preferred) or fine-grained token able to
dispatch workflows in `klangwehr/sg01-core` only.

An SDK C-header change runs the full compatibility matrix. The private
publisher compares the resulting artifacts with immutable package paths: a
module version bump is required only when its bytes change. An SDK ABI-contract
change is version-sensitive for every module and is rejected unless each
package version is bumped.
