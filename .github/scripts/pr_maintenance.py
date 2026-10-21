#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

"""
Periodic maintenance of open pull requests, run by scheduled workflow on
master. Pull request code is never checked out or executed, everything
is done through GitHub API using gh tool.

- 'needs-ci-approval' label is set on pull requests with workflow runs
  waiting for approval on their head commit, and removed otherwise.
- 'workflows-outdated' label is set on pull requests if .github of
  base branch changed since merge base, and removed otherwise.
- 'workflows-update' label is set on pull requests modifying .github
  (compared to merge base), and removed otherwise.
- All pull requests are reported for path based labeling, which is
  done by actions/labeler in the workflow.

Workflows labels are informative only, workflow runs of contributors
without write access need approval anyway.

Pull requests without activity for MAX_AGE_DAYS are skipped, also
when marked with 'stale' label (set by stale workflow) until new commit
is pushed.
"""

from datetime import date, timedelta
import json
import os
import subprocess
import sys

CI_LABEL = "needs-ci-approval"
UPDATE_LABEL = "workflows-update"
OUTDATED_LABEL = "workflows-outdated"
STALE_LABEL = "stale"

# Keep in sync with days-before-pr-stale in stale.yml
MAX_AGE_DAYS = 365

# Runs never approved stay waiting forever, skip old ones
CI_RUNS_MAX_AGE_DAYS = 30

# .github trees are queried with object() which returns null if path
# doesn't exist, Commit.file() fails whole query (and gh pagination) instead
TREES_PER_QUERY = 100

REPO = os.environ.get("GITHUB_REPOSITORY", "")

PULLS_QUERY = """
query (
  $owner: String!
  $repo: String!
  $endCursor: String
) {
  repository(owner: $owner, name: $repo) {
    pullRequests(
      states: OPEN
      first: 50
      after: $endCursor
      orderBy: { field: UPDATED_AT, direction: DESC }
    ) {
      pageInfo {
        hasNextPage
        endCursor
      }
      nodes {
        number
        updatedAt
        headRefOid
        labels(first: 100) {
          nodes {
            name
          }
        }
        baseRef {
          target {
            oid
          }
        }
        commits(last: 1) {
          nodes {
            commit {
              oid
              committedDate
            }
          }
        }
      }
    }
  }
}
"""

TREES_QUERY = """
query ($owner: String!, $repo: String!) {
  repository(owner: $owner, name: $repo) {
%s
  }
}
"""

TREE_FIELD = 'c%d: object(expression: "%s:.github") { oid }'


def gh(*args):
    """Runs gh command, returns its output"""
    return subprocess.run(["gh", *args], check=True, capture_output=True,
                          text=True).stdout


def error(message):
    print(f"::error::{message}")


def github_trees(commits):
    """Returns dict of commit to its .github tree (or None)"""
    owner, repo = REPO.split("/")
    commits = sorted(set(commits))
    trees = {}

    for i in range(0, len(commits), TREES_PER_QUERY):
        chunk = commits[i:i + TREES_PER_QUERY]
        fields = "\n".join(TREE_FIELD % (n, c) for n, c in enumerate(chunk))
        data = json.loads(gh("api", "graphql",
                             "-f", f"query={TREES_QUERY % fields}",
                             "-f", f"owner={owner}", "-f", f"repo={repo}"))
        result = data["data"]["repository"]
        for n, c in enumerate(chunk):
            trees[c] = (result[f"c{n}"] or {}).get("oid")

    return trees


def merge_base(base, head):
    return gh("api", f"repos/{REPO}/compare/{base}...{head}?per_page=1",
              "--jq", ".merge_base_commit.sha").strip()


def list_pulls():
    """Returns list of pull requests and number of errors"""
    owner, repo = REPO.split("/")
    pages = json.loads(gh("api", "graphql", "--paginate", "--slurp",
                          "-f", f"query={PULLS_QUERY}",
                          "-f", f"owner={owner}", "-f", f"repo={repo}"))
    since = (date.today() - timedelta(days=MAX_AGE_DAYS)).isoformat()
    pulls = []
    skipped = 0

    for page in pages:
        for node in page["data"]["repository"]["pullRequests"]["nodes"]:
            labels = [l["name"] for l in node["labels"]["nodes"]]
            commit = node["commits"]["nodes"][0]["commit"]
            # Adding stale label updates pull request, stale workflow
            # removes label only on its next run but rebase is new commit
            if node["updatedAt"] < since or \
                    (STALE_LABEL in labels and commit["committedDate"] < since):
                skipped += 1
                continue

            # Commits list is limited, skip pull request if head is missing
            if commit["oid"] != node["headRefOid"]:
                print(f"#{node['number']}: head commit not found, skipping")
                continue

            base = (node["baseRef"] or {}).get("target") or {}
            pulls.append({
                "number": node["number"],
                "head": commit["oid"],
                "labels": labels,
                "base_head": base.get("oid"),
            })

    print(f"{skipped} stale pull requests skipped")

    trees = github_trees([p["head"] for p in pulls] +
                         [p["base_head"] for p in pulls if p["base_head"]])
    merge_bases = {}
    errors = 0

    for pull in pulls:
        base_tree = trees.get(pull["base_head"])
        pull["tree"] = trees[pull["head"]]
        pull["base_tree"] = base_tree

        # Same tree as base means pull request neither modifies .github
        # nor is out of date, merge base is not needed
        if pull["tree"] == base_tree or not pull["base_head"]:
            pull["merge_base_tree"] = base_tree
            continue

        try:
            merge_bases[pull["number"]] = merge_base(pull["base_head"],
                                                     pull["head"])
        except subprocess.CalledProcessError as e:
            error(f"#{pull['number']}: {e.stderr.strip()}")
            errors += 1

    trees = github_trees(merge_bases.values())
    for pull in pulls:
        if pull["number"] in merge_bases:
            pull["merge_base_tree"] = trees[merge_bases[pull["number"]]]

    return pulls, errors


def sync_label(pulls, label, color, wanted):
    """
    Sets label on pull requests for which wanted() returns True and
    removes it from others, returns number of errors
    """
    issue = f"repos/{REPO}/issues"
    label_created = False
    errors = 0

    for pull in pulls:
        has_label = label in pull["labels"]
        if wanted(pull) == has_label:
            continue

        try:
            if has_label:
                gh("api", "-X", "DELETE",
                   f"{issue}/{pull['number']}/labels/{label}")
                print(f"#{pull['number']}: removed '{label}' label")
            else:
                if not label_created:
                    gh("label", "create", label, "-R", REPO, "--force",
                       "--color", color)
                    label_created = True
                gh("api", "-X", "POST", f"{issue}/{pull['number']}/labels",
                   "-f", f"labels[]={label}")
                print(f"#{pull['number']}: added '{label}' label")
        except subprocess.CalledProcessError as e:
            error(f"#{pull['number']}: {e.stderr.strip()}")
            errors += 1

    return errors


def update_ci_label(pulls):
    """Returns number of errors"""
    since = date.today() - timedelta(days=CI_RUNS_MAX_AGE_DAYS)
    try:
        waiting = set(gh("api", "--paginate",
                         f"repos/{REPO}/actions/runs?status=action_required"
                         f"&created=>={since.isoformat()}&per_page=100",
                         "--jq", ".workflow_runs[].head_sha").split())
    except subprocess.CalledProcessError as e:
        error(f"Failed to list workflow runs: {e.stderr.strip()}")
        return 1

    return sync_label(pulls, CI_LABEL, "E3650b",
                      lambda p: p["head"] in waiting)


def update_outdated_label(pulls):
    """Returns number of errors"""
    return sync_label(pulls, OUTDATED_LABEL, "FEF2C0",
                      lambda p: p["base_tree"] != p["merge_base_tree"])


def update_update_label(pulls):
    """Returns number of errors"""
    return sync_label(pulls, UPDATE_LABEL, "FBCA04",
                      lambda p: p["tree"] != p["merge_base_tree"])


def write_outputs(outputs):
    with open(os.environ["GITHUB_OUTPUT"], "a") as f:
        for key, value in outputs.items():
            if "\n" in value:
                f.write(f"{key}<<EOF\n{value}\nEOF\n")
            else:
                f.write(f"{key}={value}\n")


def main():
    """
    Updates labels and writes step outputs:
    pr-numbers - newline separated pull requests for path based labeling
    """
    try:
        pulls, errors = list_pulls()
    except subprocess.CalledProcessError as e:
        sys.exit(f"Failed to list pull requests: {e.stderr.strip()}")

    print(f"{len(pulls)} open pull requests")
    # Written first so following step can handle pull requests even if
    # updating labels fails
    write_outputs({"pr-numbers": "\n".join(str(p["number"]) for p in pulls)})
    errors += update_ci_label(pulls)
    # Pull requests without merge base tree failed and were reported
    pulls = [p for p in pulls if "merge_base_tree" in p]
    errors += update_outdated_label(pulls)
    errors += update_update_label(pulls)

    if errors:
        sys.exit(f"{errors} error(s) occurred")


if __name__ == "__main__":
    main()
