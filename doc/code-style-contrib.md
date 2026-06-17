<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Contribution Guidelines and Coding Style for Developers

This document lays out the basic contribution policies for developers. It
describes the generel workflow, describes some coding rules, and how code
review is performed.

[[_TOC_]]

## General

Duranta OpenAirInterface employs both human review and automated CI tests to
judge whether a code contribution is ready to be merged.

The contributor has to sign a contributor license agreement (CLA) as described
in [`CONTRIBUTING.md`](../CONTRIBUTING.md). After creating an account on
Github, the contributor can open a pull request from a fork: he becomes the
"author" of such code contribution. A senior Duranta OAI member will review
this work, and make suggestions for possible improvements. Each week, we
discuss the progress of the pull requests in a [weekly developer
call](https://github.com/duranta-project/openairinterface5g/wiki/Developer-Meetings),
and discuss which pull requests can be merged.

The CI consists in various Jenkins pipelines that run on each pull request.
See [`TESTBenches.md`](./TESTBenches.md) for more details about the CI setup.

There is the official [Github Help](https://docs.github.com/) that can help you
with any questions regarding Github. Note that unlike Gitlab, password-based
code pushes are not allowed. Also, we recommend reading the [Git
Book](https://git-scm.com/book/en/v2) to use Git properly.

## Basic coding rules

You should respect the `.clang-format` file in the root of the repository. The
`clang-format` tool will pick up this file when being applied to code in the
repository. Please also refer to the [corresponding
documentation](./clang-format.md).

A number of high-level comments:

- Indentation is two spaces, no tabs; try to limit the number of indentations.
- Line length is 132, not more than one statement per line; no whitespace at
  the end of lines
- The opening brace after a function is on a new line; after control flow
  statements (`if`, `while`, `switch`, ...), it is on the same line
- Pointer or reference operators (`*`, `&`) are right-aligned
- Do not commit code that is commented out
- Use strong typing (no `void *`, use complex data types such as `c16_t` over
  `uint32_t` in L1, ...)
- Do not use [magic numbers](https://en.wikipedia.org/wiki/Magic_number_(programming)#Unnamed_numerical_constants)
  for unnamed numerical constants and do not hardcode values
- Don't cast the result of `malloc()`: it is not needed, and can lead to bugs.
- Use `AssertFatal()` and `DevAssert()` to check for invariants, not for error
  handling: Assertions are for preventing bugs (e.g., unforeseen state),
  not to sanitize input.
- Use `const` on pointer function arguments that are input to that function;
  put output variables (via a pointer) last
- Do not do premature optimization; measure the code before writing SIMD
  instructions by hand, and measure again to show it is faster.
- Avoid variables marked with `extern`. Function prototypes must be in one
  unique header file that should be included by all source files that define
  this function or use it.

If in doubt, check out code that has been recently written (e.g., use the pull
requests page to check for code that has recently been added) and follow that
style. Checking surrounding code is usually not the best idea, as OAI has a
long history in which coding rules were not really enforced.

There is an old [OAI coding guidelines
document](https://gitlab.eurecom.fr/oai/openairinterface5g/-/wikis/documents/openair_coding_guidelines_v0.3.pdf)
that might be useful; if this document and `.clang-format` contradict,
`.clang-format` takes precedence.

## Main Workflow and Versioning

### Workflow

You should be familiar with git branching, merging, and rebasing.

To make branches simple to read for a reviewer, developer, or anyone interested
in the code, please keep your branch history linear (i.e., no merges).  Each commit should be able to
compile, and ideally be able to run an End-to-End test of gNB/UE using RFsim.
This can be achieved by making each commit a **logical change** to be applied to
the code base, which also facilitates the review of your changes. The Linux
kernel has some [documentation on what a logical change
is](https://www.kernel.org/doc/html/latest/process/submitting-patches.html#separate-your-changes).
From a practical point of view, this means that your history should not have
commits that "clean up" a previous commit (indicated by commit messages such as
`Fix bug` or `Review addressed`). They don't describe what the fix is about,
and make review more difficult because the changes are not self-contained, but
spread across commits, incurring mental overhead. Instead, the commit series
should be written from the first commit as if you knew how the final code looks
like. In other words, you should guide the reader of your commits. This includes
that every commit message describes _why_ a particular change is necessary and
correct(!). Note that the rule of making commits small still applies! In
summary:

- Make logical changes, which are **small, self-contained** commits,
- Don't fix up changes later: **rewrite the history** to guide the
  reader/reviewer, and
- In the commit message, **explain why** changes are necessary and correct.

A commit message can (and often should) take several lines.  One-line commit
messages should be reserved for very simple changes. If in doubt, prefer to
explain your work more than less.

### Release Strategy

The target branch for every contribution, and the general development branch,
is `develop`. Typically every week, we collect multiple pull requests in an
"integration branch" that gets tested by the CI individually. If everything is
fine, we merge to develop and tag it `YYYY.wXX` with `YYYY` the current year,
and `XX` the week number.

After some time, we make a stable release using a semantic version number,
e.g., `v3.0`. We target to make releases bi-yearly.

### How to manage your own branch

Before starting to work, please make sure to branch off the latest `develop`
branch.  Make commits as appropriate.
```bash
$ git fetch origin
$ git checkout develop
$ git checkout -b my-new-feature # name as appropriate
$ git add -p                     # add changes for change set 1, use `-p` to review what to include
$ git commit                     # in the editor, describe your changes
$ git add -p                     # add changes for change set 2
$ git commit                     # in the editor, describe your changes
```

Again, commit message should take multiple lines; after the initial title, a
blank line should follow. Read the `DISCUSSION` section in `man git commit` for
more information.

If your development takes longer, make sure to synchronize regularly with
`origin/develop` using `git rebase`:
```bash
$ git fetch origin
$ git rebase -i origin/develop
```

If you do logical changes, you should not have to resolve the same conflicts
over and over again. Note that if you jumped over multiple develop tags, you
can also rebase in intermediate steps, in case you fear the differences might
be too big.
```
$ git rebase -i 2023.w38
$ git rebase -i 2023.w41
$ git rebase -i develop
```

Once you rebased, push the changes to the remote
```
$ git push origin my-new-feature --force-with-lease # force with lease let's you only overwrite what you also have locally in origin/my-new-feature
```

### Use of git commit trailers

As noted in the [contribution guidelines](../CONTRIBUTING.md), you have to sign
all your commits. Thus, every commit must have a git commit trailer that reads

```
Signed-off-by: Full Name <email-for-cla>
```

There are additional commit trailers that you can or should use:

- `Assisted-by: <Name>:<model>`: if you have been assisted by an AI/LLM, you
  must disclose this by indicating both the LLM name and model. Note that LLMs
  do not author, as _the submission is under your name_ (i.e., NEVER add an LLM
  through `Co-authored:by:`). The [Linux kernel documentation on AI
  assistents](https://docs.kernel.org/process/coding-assistants.html)
  might be helpful.
- `Reviewed-by: Full Name <email>` for a person that reviewed a code. We attach
  this trailer to the merge commit for people that reviewed a pull request.
- `Co-authored-by: Full Name <email>` for a person that significantly
  contributed to a commit and has co-authorship.
- `Fixes: <commit> ("<title>")` if a given commit fixes bug in an earlier,
  referenced commit. For ease-of-use, please include the commit title, and only
  the commit SHA, not a link.
- `Closes: #Issue` if a specific commit closes a bug. If the pull request
  description includes this, we add this to the merge commit.
- `Reported-by: Full Name <email>` if a person reported a bug or other useful
  information that led to this commit.
- `Tested-By: Full Name <email>` if a person tested a given patch.

This list is non-exhaustive, and you might attach more trailers. Please also
check the documentation via `man git-interpret-trailers`, and note that the
general free-form format is (from the documentation):

```
   key: value

This means that the trimmed <key> and <value> will be separated by ": " (one colon followed by one space).
```

### AI Assistants

These guidelines are mostly based on [linux kernel
guidelines](https://docs.kernel.org/process/coding-assistants.html)

This document provides guidance for AI tools and developers using AI assistance
when contributing to the respository.

AI tools helping with openairinterface development should follow the standard
openairinterface developement procedure. They should comply with Duranta OAI’s
licensing requirements:

- All code must be compatible with CSSL v1.0
- Use appropriate SPDX license identifiers

AI agents MUST NOT add `Signed-off-by` nor `Co-authored-by` tags.  Only humans
can legally certify the Developer Certificate of Origin (DCO).  The human
submitter is responsible for:

- Reviewing all AI-generated code
- Ensuring compliance with licensing requirements
- Adding their own Signed-off-by tag to certify the DCO
- Taking full responsibility for the contribution

When AI tools contribute to openairinterface,
proper commit message helps track the evolving role of AI in the development process.
Contributions should include an `Assisted-by` tag in the following format:

    Assisted-by: AGENT_NAME:MODEL_VERSION

- `AGENT_NAME` is the name of the AI tool or framework
- `MODEL_VERSION` is the specific model version used

Example:

    Assisted-by: Claude:claude-3-opus

## Pull Requests

A pull request (PR) can be submitted as soon as the code is considered stable
and reviewable. The idea is to start the review early enough so that the code
author (the PR owner) can incorporate fixes while the reviewer is giving
feedback. Note that while it should not be common, a refusal of a pull request
is a valid outcome of a pull request review (subject to proper justification).

When preparing a contribution that is large, the developer is responsible for
warning the maintainers team, so that the review work can start as early as possible
and run in parallel to the contribution finalization. Failing to do so, there
is a risk that the work will take a long time to be merged or might even not be
merged at all if judged too complex by the maintainers team. Also, note that big
contributions should be cut into small commits each containing a logical
change, as described above. Finally, as a rule of thumb, the smaller the pull
request, the easier it will be to review and merge.

The reviewer comments on code changes ("open comments") that should be
addressed by the author. Most reviewers prefer to mark open comments as
resolved by themselves to double check the modifications and close such
comments. As an author, please don't resolve open comments (don't click the
"Resolve thread" button) unless explicitly instructed by the reviewer.

Note that the _pull request author_ asks for inclusion of code, so _they
should make the review easy_; in particular, if facilitating review incurs
extra work to make a simpler code review (e.g., rewriting entire commits or
their order), this extra work is justified. This particularly(!) applies for
big pull requests.

When opening a pull requests, the author should select `develop` as the target
branch, and add at least one of these labels when opening the pull request:

- https://github.com/duranta-project/openairinterface5g/labels/documentation:
  don't perform any testing stages, for documentation
- https://github.com/duranta-project/openairinterface5g/labels/BUILD-ONLY:
  execute only build stages, for code improvements without impact on 4G or 5G
  code
- https://github.com/duranta-project/openairinterface5g/labels/4G-LTE: perform
  4G tests
- https://github.com/duranta-project/openairinterface5g/labels/5G-NR: perform
  5G tests
- https://github.com/duranta-project/openairinterface5g/labels/nrUE: perform
  only 5G-UE related tests including physims

Failure to add a label will prevent the CI from running. If in doubt about the
right label, add both 4G and 5G labels. The CI posts the results in the
comments section of the pull request. Both pull request authors and reviewers
are responsible for manual inspection and pre-filtering of the CI results. An
overview of the CI tests is in [`TESTBenches.md`](./TESTBenches.md).

To communicate the review progress both between author and reviewer, as well as
to the outside world, we (ab-)use the milestones feature of Github to track the
current progress. The milestone can be set when opening the pull request, and
during its lifetime in the sidebar on the right. Following options:

- _no milestone_: not ready for review yet and is generally used to wait for a
  first CI run that the author will inspect and fix problems detected by the CI
  (please limit the time in which your code is in that phase)
- [REVIEW_CAN_START](https://github.com/duranta-project/openairinterface5g/milestone/2): the reviewer can start the review
- [REVIEW_IN_PROGRESS](https://github.com/duranta-project/openairinterface5g/milestone/4): the reviewer is currently doing review, and might
  request changes to the code that the author should include (or refute with
  justification)
- [REVIEW_COMPLETED_AND_APPROVED](https://github.com/duranta-project/openairinterface5g/milestone/3): the reviewer is happy with code changes
  (*open comments still have to be addressed!*)
- [OK_TO_BE_MERGED](https://github.com/duranta-project/openairinterface5g/milestone/1): the maintainers team plans to merge this; *do not push any changes
  anymore at this point*.

## Review Form

The following is a check list that might be used by a reviewer to check that
code contribution fulfils minimum standard w.r.t. formatting, data types,
assertions, etc. The reviewer might copy/paste this form into a pull request,
or simply check that all have been filled.

All points should be marked to complete a review.

```md
### Review by @username

- [ ] `.clang-format` respected
- [ ] No merges, i.e., the branch has a linear history; every commit compiles (and ideally runs in RFsim)
- [ ] For L1: uses complex data types. In general: prefers strong/adapted types/typedefs over `void`/generic `int`, or otherwise primitive types.
- [ ] Documentation updated (doxygen summary of functions; in `doc/`, or the corresponding folder; `FEATURE_SET.md`)
- [ ] Uses assertions were appropriate
- [ ] No commented/dead code (or such code has been removed), no function duplication
- [ ] No magic numbers: use defines, enums, and variables to make the meaning of a number clear.
- [ ] The changes don't have patch noise (no unnecessary whitespace changes unrelevant to the changed code; reformatting is ok)
- [ ] No unnecessary/excessive logs introduced. Prefer LOG_D for frequent logs

Additional remarks, if applicable:
```

Additional optional questions in case they apply:
- Has a new tool/dependency been introduced? Needs to be discussed if to be added.
- Is a new CI test necessary? Can it be done in simulators?

## Reporting bugs

Please report only true bugs in the [issue tracker](../../issues). Do not
report general user problems; use the [mailing
lists](https://github.com/duranta-project/openairinterface5g/wiki/MailingList)
instead.  If in doubt, prefer the mailing lists and if needed and requested by
the maintainers team, an issue will be opened.

When reporting a bug, please clearly
- explain the problem,
- note what you expected to happen (what should happen),
- show what happens instead (what did happen), and
- give steps on how to reproduce (including, if needed, configuration files and
  command lines).

You are encouraged to use these bullet points to structure your issue for easy
understanding.  Use code tags (the "insert code" button with symbol &lt;/&gt; in
the github editor) for logs and small code snippets.
