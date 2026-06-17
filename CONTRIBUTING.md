<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Contributing to OpenAirInterface

We want to make contributing to this project as easy and transparent as possible.

1. Create an account on [GitHub](https://github.com/). Only contributions
   against [`duranta-project/openairinterface5g/`](https://github.com/duranta-project/openairinterface5g/)
   are accepted.
2. Fork the repository, and open pull requests for your contributions from your
   fork.
3. The contributing policies are described in the [corresponding documentation
   page](doc/code-style-contrib.md).
4. [Sign the CLA](https://github.com/duranta-project/governance/blob/main/docs/easy_cla_process.md)
   either before doing making your first pull request or after submitting the
   pull request.
5. Mandatory signing of all the commits using the email address used for CLA.

## Commit Guidelines

### Signing Commits

To sign commits:

You can also get the verified label on your commits via using [SSH keys or GPG
keys](https://docs.github.com/en/authentication/managing-commit-signature-verification/signing-commits)

```
# Edit .git/config in the git repository you are working on
# Add the user section
[user]
    name = YOUR NAME
    email = YOUR EMAIL ADDRESS

# If you use a signing key, use the below configuration instead
[user]
    name = YOUR NAME
    email = YOUR EMAIL ADDRESS
    signingkey = LOCATION OF SSH KEYS or GPG KEY

[gpg]
    format = ssh

[commit]
    gpgsign = true
```

> **NOTE:** If your commits are not signed the CI framework will not accept the PR.

For more information regarding contribution guidelines
please check [this document](doc/code-style-contrib.md)

## License

By contributing to OpenAirInterface, you agree that your contributions will be
licensed under

1. [CSSL v1.0 license](LICENSES/preferred/CSSL-v1.0.txt): for RAN and UE
   related source code and test scripts
2. [CC-BY-4.0](LICENSES/preferred/CC-BY-4.0.txt): All the documentation
3. [MIT](LICENSES/preferred/MIT.txt): Orchestration (helm-charts, docker
   compose)

Certain files are using different licenses; you can read about them in
[NOTICE](NOTICE).
