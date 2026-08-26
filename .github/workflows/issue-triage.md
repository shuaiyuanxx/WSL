---
name: Agentic Issue Triage
emoji: 🏷️
description: Triage newly opened issues by labeling, duplicate detection, clarification requests, and assignment.
on:
  issues:
    types: [opened]
  roles: all
permissions:
  contents: read
  issues: read
strict: true
tools:
  github:
    mode: gh-proxy
    toolsets: [repos, issues, labels, search]
steps:
  - name: Prefetch issue triage context
    uses: actions/github-script@v9
    with:
      script: |
        const fs = require('fs');
        fs.mkdirSync('/tmp/gh-aw/data', { recursive: true });

        const issue = context.payload.issue;
        const owner = context.repo.owner;
        const repo = context.repo.repo;
        const labels = await github.paginate(github.rest.issues.listLabelsForRepo, {
          owner,
          repo,
          per_page: 100,
        });

        const duplicateQuery = [
          issue.title,
          `repo:${owner}/${repo}`,
          'is:issue',
          `-number:${issue.number}`,
        ].join(' ');
        const duplicates = await github.rest.search.issuesAndPullRequests({
          q: duplicateQuery,
          per_page: 10,
        });

        fs.writeFileSync('/tmp/gh-aw/data/issue.json', JSON.stringify({
          number: issue.number,
          title: issue.title,
          body: issue.body || '',
          author: issue.user?.login,
          author_association: issue.author_association,
          labels: issue.labels?.map((label) => label.name) || [],
          url: issue.html_url,
        }, null, 2));
        fs.writeFileSync('/tmp/gh-aw/data/repository-labels.json', JSON.stringify(
          labels.map((label) => ({ name: label.name, description: label.description || '' })),
          null,
          2
        ));
        fs.writeFileSync('/tmp/gh-aw/data/duplicate-candidates.json', JSON.stringify(
          duplicates.data.items.map((item) => ({
            number: item.number,
            title: item.title,
            url: item.html_url,
            labels: item.labels?.map((label) => typeof label === 'string' ? label : label.name) || [],
          })),
          null,
          2
        ));
safe-outputs:
  add-labels:
    allowed:
      - bug
      - enhancement
      - documentation
      - question
      - duplicate
      - invalid
      - help wanted
      - good first issue
      - "priority: critical"
      - "priority: high"
      - "priority: medium"
      - "priority: low"
      - needs-author-feedback
    max: 4
    target: triggering
  add-comment:
    max: 1
    target: triggering
    issues: true
    pull-requests: false
  assign-to-user:
    allowed: [shuaiyuanxx]
    max: 1
    target: triggering
---

# Agentic Issue Triage

## Task

Triage the newly opened issue using the pre-fetched context in:

- `/tmp/gh-aw/data/issue.json`
- `/tmp/gh-aw/data/repository-labels.json`
- `/tmp/gh-aw/data/duplicate-candidates.json`

Classify the issue by type and priority. Prefer existing repository labels from `repository-labels.json`; if a suitable priority label is allowed but absent, still choose the best priority label so maintainers can create or reconcile it.

Identify duplicates by comparing the issue title and body with `duplicate-candidates.json` and targeted GitHub searches when needed. Only mark an issue as `duplicate` when there is a high-confidence existing issue that covers the same report or request.

Ask one concise clarifying question when the report lacks essential information needed for triage, such as repro steps, expected behavior, actual behavior, Windows version, WSL version, or diagnostics. Add `question` or `needs-author-feedback` when asking for clarification.

Assign the issue to `shuaiyuanxx` only when maintainer follow-up is clearly needed after labeling or clarification. If CODEOWNERS or repository context indicates another team is more appropriate, mention the suggested owner in the comment instead of assigning an unavailable user.

## Safe Outputs

- Use `add-labels` for type, priority, duplicate, and clarification labels.
- Use `add-comment` only for duplicate explanations, clarification questions, or suggested ownership notes.
- Use `assign-to-user` only for the allowed repository assignee.
- Call `noop` with a short reason when the issue already has enough labels, is not actionable, or no visible change is needed.
