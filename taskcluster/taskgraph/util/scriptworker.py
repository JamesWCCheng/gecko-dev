# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Make scriptworker.cot.verify more user friendly by making scopes dynamic.

Scriptworker uses certain scopes to determine which sets of credentials to use.
Certain scopes are restricted by branch in chain of trust verification, and are
checked again at the script level.  This file provides functions to adjust
these scopes automatically by project; this makes pushing to try, forking a
project branch, and merge day uplifts more user friendly.

In the future, we may adjust scopes by other settings as well, e.g. different
scopes for `push-to-candidates` rather than `push-to-releases`, even if both
happen on mozilla-beta and mozilla-release.
"""
from __future__ import absolute_import, print_function, unicode_literals
import functools
import os


"""Map signing scope aliases to sets of projects.

Currently m-c and m-a use nightly signing; m-b and m-r use release signing.
We will need to add esr support at some point. Eventually we want to add
nuance so certain m-b and m-r tasks use dep or nightly signing, and we only
release sign when we have a signed-off set of candidate builds.  This current
approach works for now, though.

This is a list of list-pairs, for ordering.
"""
SIGNING_SCOPE_ALIAS_TO_PROJECT = [[
    'all-nightly-branches', set([
        'mozilla-central',
        'mozilla-aurora',
    ])
], [
    'all-release-branches', set([
        'mozilla-beta',
        'mozilla-release',
    ])
]]

"""Map the signing scope aliases to the actual scopes.
"""
SIGNING_CERT_SCOPES = {
    'all-release-branches': 'project:releng:signing:cert:release-signing',
    'all-nightly-branches': 'project:releng:signing:cert:nightly-signing',
    'default': 'project:releng:signing:cert:dep-signing',
}

"""Map beetmover scope aliases to sets of projects.
"""
BEETMOVER_SCOPE_ALIAS_TO_PROJECT = [[
    'all-nightly-branches', set([
        'mozilla-central',
        'mozilla-aurora',
        'mozilla-beta',
        'mozilla-release',
    ])
], [
    'all-release-branches', set([
        'mozilla-beta',
        'mozilla-release',
    ])
]]

"""The set of all beetmover release target tasks.

Used for both `BEETMOVER_SCOPE_ALIAS_TO_TARGET_TASK` and `get_release_build_number`
"""
BEETMOVER_RELEASE_TARGET_TASKS = set([
    'candidates_fennec',
])

"""Map beetmover tasks aliases to sets of target task methods.

This is a list of list-pairs, for ordering.
"""
BEETMOVER_SCOPE_ALIAS_TO_TARGET_TASK = [[
    'all-nightly-tasks', set([
        'nightly_fennec',
        'nightly_linux',
        'mozilla_beta_tasks',
        'mozilla_release_tasks',
    ])
], [
    'all-release-tasks', BEETMOVER_RELEASE_TARGET_TASKS
]]

"""Map the beetmover scope aliases to the actual scopes.
"""
BEETMOVER_BUCKET_SCOPES = {
    'all-release-tasks': {
        'all-release-branches': 'project:releng:beetmover:bucket:release',
    },
    'all-nightly-tasks': {
        'all-nightly-branches': 'project:releng:beetmover:bucket:nightly',
    },
    'default': 'project:releng:beetmover:bucket:dep',
}

"""Map the beetmover tasks aliases to the actual action scopes.
"""
BEETMOVER_ACTION_SCOPES = {
    'all-release-tasks': 'project:releng:beetmover:action:push-to-candidates',
    'all-nightly-tasks': 'project:releng:beetmover:action:push-to-nightly',
    'default': 'project:releng:beetmover:action:push-to-staging',
}

"""Map balrog scope aliases to sets of projects.

This is a list of list-pairs, for ordering.
"""
BALROG_SCOPE_ALIAS_TO_PROJECT = [[
    'nightly', set([
        'mozilla-central',
    ])
], [
    'aurora', set([
        'mozilla-aurora',
    ])
], [
    'beta', set([
        'mozilla-beta',
    ])
], [
    'release', set([
        'mozilla-release',
    ])
]]

"""Map the balrog scope aliases to the actual scopes.
"""
BALROG_SERVER_SCOPES = {
    'nightly': 'project:releng:balrog:nightly',
    'aurora': 'project:releng:balrog:nightly',
    'beta': 'project:releng:balrog:nightly',
    'release': 'project:releng:balrog:nightly',
    'default': 'project:releng:balrog:nightly',
}


# scope functions {{{1
def get_scope_from_project(alias_to_project_map, alias_to_scope_map, config):
    """Determine the restricted scope from `config.params['project']`.

    Args:
        alias_to_project_map (list of lists): each list pair contains the
            alias and the set of projects that match.  This is ordered.
        alias_to_scope_map (dict): the alias alias to scope
        config (dict): the task config that defines the project.

    Returns:
        string: the scope to use.
    """
    for alias, projects in alias_to_project_map:
        if config.params['project'] in projects and alias in alias_to_scope_map:
            return alias_to_scope_map[alias]
    return alias_to_scope_map['default']


def get_scope_from_target_method(alias_to_tasks_map, alias_to_scope_map, config):
    """Determine the restricted scope from `config.params['target_tasks_method']`.

    Args:
        alias_to_tasks_map (list of lists): each list pair contains the
            alias and the set of target methods that match. This is ordered.
        alias_to_scope_map (dict): the alias alias to scope
        config (dict): the task config that defines the target task method.

    Returns:
        string: the scope to use.
    """
    for alias, tasks in alias_to_tasks_map:
        if config.params['target_tasks_method'] in tasks and alias in alias_to_scope_map:
            return alias_to_scope_map[alias]
    return alias_to_scope_map['default']


def get_scope_from_target_method_and_project(alias_to_tasks_map, alias_to_project_map,
                                             aliases_to_scope_map, config):
    """Determine the restricted scope from both `target_tasks_method` and `project`.

    On certain branches, we'll need differing restricted scopes based on
    `target_tasks_method`.  However, we can't key solely on that, since that
    `target_tasks_method` might be run on an unprivileged branch.  This method
    checks both.

    Args:
        alias_to_tasks_map (list of lists): each list pair contains the
            alias and the set of target methods that match. This is ordered.
        alias_to_project_map (list of lists): each list pair contains the
            alias and the set of projects that match.  This is ordered.
        aliases_to_scope_map (dict of dicts): the task alias to project alias to scope
        config (dict): the task config that defines the target task method and project.

    Returns:
        string: the scope to use.
    """
    project = config.params['project']
    target = config.params['target_tasks_method']
    for alias1, tasks in alias_to_tasks_map:
        for alias2, projects in alias_to_project_map:
            if target in tasks and project in projects and \
                    aliases_to_scope_map.get(alias1, {}).get(alias2):
                return aliases_to_scope_map[alias1][alias2]
    return aliases_to_scope_map['default']


get_signing_cert_scope = functools.partial(
    get_scope_from_project,
    SIGNING_SCOPE_ALIAS_TO_PROJECT,
    SIGNING_CERT_SCOPES
)

get_beetmover_bucket_scope = functools.partial(
    get_scope_from_target_method_and_project,
    BEETMOVER_SCOPE_ALIAS_TO_TARGET_TASK,
    BEETMOVER_SCOPE_ALIAS_TO_PROJECT,
    BEETMOVER_BUCKET_SCOPES
)

get_beetmover_action_scope = functools.partial(
    get_scope_from_target_method,
    BEETMOVER_SCOPE_ALIAS_TO_TARGET_TASK,
    BEETMOVER_ACTION_SCOPES
)

get_balrog_server_scope = functools.partial(
    get_scope_from_project,
    BALROG_SCOPE_ALIAS_TO_PROJECT,
    BALROG_SERVER_SCOPES
)


# build_number {{{1
def get_release_build_number(config):
    """Get the build number for a release task.

    Currently only applies to beetmover tasks.

    Args:
        config (dict): the task config that defines the target task method.

    Raises:
        ValueError: if a release graph doesn't define a valid
            `os.environ['BUILD_NUMBER']`

    Returns:
        int: the build number, if applicable.
    """
    if config.params['target_tasks_method'] in BEETMOVER_RELEASE_TARGET_TASKS:
        build_number = str(os.environ.get("BUILD_NUMBER", ""))
        if not build_number.isdigit():
            raise ValueError("Release graphs must specify `BUILD_NUMBER` in the environment!")
        return int(build_number)
