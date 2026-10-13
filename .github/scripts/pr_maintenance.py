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
- 'Check workflows' commit status reports whether .github/workflows of
  pull request head matches base branch ('workflows-update' label skips
  the check).
- Pull requests with new head commit are reported for path based
  labeling, which is done by actions/labeler in the workflow.

'Check workflows' status also marks pull request head as processed: it
is tied to head commit and its description contains base branch
workflows tree it was checked against, so only new or changed pull
requests are processed.

Usage:
  pr_maintenance.py check   - update labels, write step outputs
  pr_maintenance.py report  - report statuses from STATUSES variable
"""

from datetime import date, timedelta
import json
import os
import subprocess
import sys

CI_LABEL = "needs-ci-approval"
WORKFLOWS_LABEL = "workflows-update"
STATUS_CONTEXT = "Check workflows"
SKIPPED = f"Skipped, '{WORKFLOWS_LABEL}' label present"

# Runs never approved stay waiting forever, skip old ones
CI_RUNS_MAX_AGE_DAYS = 30

# Limit API usage, remaining pull requests are handled by next runs
MAX_PULLS_PER_RUN = 20

# Workflows trees are queried with object() which returns null if path
# doesn't exist, Commit.file() fails whole query (and gh pagination) instead
TREES_PER_QUERY = 100

REPO = os.environ.get("GITHUB_REPOSITORY", "")

PULLS_QUERY = """
query (
  $owner: String!
  $repo: String!
  $context: String!
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
        headRefOid
        baseRefName
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
              status {
                context(name: $context) {
                  state
                  description
                }
              }
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

TREE_FIELD = 'c%d: object(expression: "%s:.github/workflows") { oid }'


def gh(*args):
    """Runs gh command, returns its output"""
    return subprocess.run(["gh", *args], check=True, capture_output=True,
                          text=True).stdout


def error(message):
    print(f"::error::{message}")


def workflows_trees(commits):
    """Returns dict of commit to its .github/workflows tree (or None)"""
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


def list_pulls():
    owner, repo = REPO.split("/")
    pages = json.loads(gh("api", "graphql", "--paginate", "--slurp",
                          "-f", f"query={PULLS_QUERY}",
                          "-f", f"owner={owner}", "-f", f"repo={repo}",
                          "-f", f"context={STATUS_CONTEXT}"))
    pulls = []

    for page in pages:
        for node in page["data"]["repository"]["pullRequests"]["nodes"]:
            commit = node["commits"]["nodes"][0]["commit"]
            # Commits list is limited, skip pull request if head is missing
            if commit["oid"] != node["headRefOid"]:
                print(f"#{node['number']}: head commit not found, skipping")
                continue

            base = (node["baseRef"] or {}).get("target") or {}
            pulls.append({
                "number": node["number"],
                "head": commit["oid"],
                "labels": [l["name"] for l in node["labels"]["nodes"]],
                "status": (commit["status"] or {}).get("context"),
                "base_name": node["baseRefName"],
                "base_head": base.get("oid"),
            })

    trees = workflows_trees([p["head"] for p in pulls] +
                            [p["base_head"] for p in pulls if p["base_head"]])

    for pull in pulls:
        base_head = pull.pop("base_head")
        base_name = pull.pop("base_name")
        base_tree = trees.get(base_head)
        pull["tree"] = trees[pull["head"]]
        pull["base_tree"] = base_tree
        pull["base"] = f"{base_name}@{(base_tree or 'none')[:12]}"

    return pulls


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

    issue = f"repos/{REPO}/issues"
    label_created = False
    errors = 0

    for pull in pulls:
        has_label = CI_LABEL in pull["labels"]
        if (pull["head"] in waiting) == has_label:
            continue

        try:
            if has_label:
                gh("api", "-X", "DELETE",
                   f"{issue}/{pull['number']}/labels/{CI_LABEL}")
                print(f"#{pull['number']}: removed '{CI_LABEL}' label")
            else:
                if not label_created:
                    gh("label", "create", CI_LABEL, "-R", REPO, "--force",
                       "--color", "E3650b")
                    label_created = True
                gh("api", "-X", "POST", f"{issue}/{pull['number']}/labels",
                   "-f", f"labels[]={CI_LABEL}")
                print(f"#{pull['number']}: added '{CI_LABEL}' label")
        except subprocess.CalledProcessError as e:
            error(f"#{pull['number']}: {e.stderr.strip()}")
            errors += 1

    return errors


def workflows_status(pull):
    """Returns status to report, or None if reported one is up to date"""
    status = pull["status"]

    if WORKFLOWS_LABEL in pull["labels"]:
        new = {"state": "success", "description": SKIPPED}
    elif pull["tree"] == pull["base_tree"]:
        new = {"state": "success",
               "description": f"Workflows match {pull['base']}"}
    else:
        new = {"state": "failure",
               "description": f"Workflows differ from {pull['base']}, "
                              f"rebase or apply '{WORKFLOWS_LABEL}' label"}

    if status and (status["state"], status["description"]) == \
            (new["state"], new["description"]):
        return None

    return {"sha": pull["head"], **new}


def check():
    """
    Updates CI approval label and returns number of errors and step
    outputs:
    pr-numbers - newline separated pull requests for path based labeling
    statuses - JSON list of workflows commit statuses to report
    """
    pulls = list_pulls()
    print(f"{len(pulls)} open pull requests")
    errors = update_ci_label(pulls)
    statuses = [s for s in map(workflows_status, pulls) if s]
    statuses = statuses[:MAX_PULLS_PER_RUN]
    reported = {s["sha"] for s in statuses}

    # No status on head commit means pull request was updated
    labels = [str(p["number"]) for p in pulls
              if not p["status"] and p["head"] in reported]

    for status in statuses:
        print(f"{status['sha']}: {status['description']}")

    return errors, {
        "pr-numbers": "\n".join(labels),
        "statuses": json.dumps(statuses),
    }


def report(statuses, target_url):
    """Reports statuses computed by check(), returns number of errors"""
    errors = 0

    for status in statuses:
        try:
            gh("api", "-X", "POST", f"repos/{REPO}/statuses/{status['sha']}",
               "-f", f"state={status['state']}",
               "-f", f"description={status['description']}",
               "-f", f"context={STATUS_CONTEXT}",
               "-f", f"target_url={target_url}")
            print(f"{status['sha']}: {status['state']}")
        except subprocess.CalledProcessError as e:
            error(f"{status['sha']}: {e.stderr.strip()}")
            errors += 1

    return errors


def write_outputs(outputs):
    with open(os.environ["GITHUB_OUTPUT"], "a") as f:
        for key, value in outputs.items():
            if "\n" in value:
                f.write(f"{key}<<EOF\n{value}\nEOF\n")
            else:
                f.write(f"{key}={value}\n")


def main():
    if sys.argv[1:] == ["check"]:
        try:
            errors, outputs = check()
        except subprocess.CalledProcessError as e:
            sys.exit(f"Failed to list pull requests: {e.stderr.strip()}")
        # Outputs are written even on errors so following steps can
        # handle pull requests that were processed successfully
        write_outputs(outputs)
    elif sys.argv[1:] == ["report"]:
        url = f"{os.environ['GITHUB_SERVER_URL']}/{REPO}/actions/runs/" \
              f"{os.environ['GITHUB_RUN_ID']}"
        errors = report(json.loads(os.environ["STATUSES"]), url)
    else:
        sys.exit(__doc__)

    if errors:
        sys.exit(f"{errors} error(s) occurred")


if __name__ == "__main__":
    main()
