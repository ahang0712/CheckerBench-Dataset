# Patch-overlapping Python context after the fix

## `psiturk/experiment.py` — `function:advertisement` (lines 284-392)

```python
def advertisement():
    """
    This is the url we give for the ad for our 'external question'.  The ad has
    to display two different things: This page will be called from within
    mechanical turk, with url arguments hitId, assignmentId, and workerId.
    If the worker has not yet accepted the hit:
        These arguments will have null values, we should just show an ad for
        the experiment.
    If the worker has accepted the hit:
        These arguments will have appropriate values and we should enter the
        person in the database and provide a link to the experiment popup.
    """
    user_agent_string = request.user_agent.string
    user_agent_obj = user_agents.parse(user_agent_string)
    browser_ok = True
    browser_exclude_rule = CONFIG.get('Task Parameters', 'browser_exclude_rule')
    for rule in browser_exclude_rule.split(','):
        myrule = rule.strip()
        if myrule in ["mobile", "tablet", "touchcapable", "pc", "bot"]:
            if (myrule == "mobile" and user_agent_obj.is_mobile) or\
               (myrule == "tablet" and user_agent_obj.is_tablet) or\
               (myrule == "touchcapable" and user_agent_obj.is_touch_capable) or\
               (myrule == "pc" and user_agent_obj.is_pc) or\
               (myrule == "bot" and user_agent_obj.is_bot):
                browser_ok = False
        elif myrule == "Safari" or myrule == "safari":
            if "Chrome" in user_agent_string and "Safari" in user_agent_string:
                pass
            elif "Safari" in user_agent_string:
                browser_ok = False
        elif myrule in user_agent_string:
            browser_ok = False

    if not browser_ok:
        # Handler for IE users if IE is not supported.
        raise ExperimentError('browser_type_not_allowed')

    if not ('hitId' in request.args and 'assignmentId' in request.args):
        raise ExperimentError('hit_assign_worker_id_not_set_in_mturk')
    hit_id = request.args['hitId']
    assignment_id = request.args['assignmentId']
    mode = request.args['mode']
    if hit_id[:5] == "debug":
        debug_mode = True
    else:
        debug_mode = False
    already_in_db = False
    if 'workerId' in request.args:
        worker_id = request.args['workerId']
        # First check if this workerId has completed the task before (v1).
        nrecords = Participant.query.\
            filter(Participant.assignmentid != assignment_id).\
            filter(Participant.workerid == worker_id).\
            count()

        if nrecords > 0:  # Already completed task
            already_in_db = True
    else:  # If worker has not accepted the hit
        worker_id = None
    try:
        part = Participant.query.\
            filter(Participant.hitid == hit_id).\
            filter(Participant.assignmentid == assignment_id).\
            filter(Participant.workerid == worker_id).\
            one()
        status = part.status
    except exc.SQLAlchemyError:
        status = None

    allow_repeats = CONFIG.getboolean('Task Parameters', 'allow_repeats')
    if (status == STARTED or status == QUITEARLY) and not debug_mode:
        # Once participants have finished the instructions, we do not allow
        # them to start the task again.
        raise ExperimentError('already_started_exp_mturk')
    elif status == COMPLETED or (status == SUBMITTED and not already_in_db):
        # 'or status == SUBMITTED' because we suspect that sometimes the post
        # to mturk fails after we've set status to SUBMITTED, so really they
        # have not successfully submitted. This gives another chance for the
        # submit to work.

        # They've finished the experiment but haven't successfully submitted the HIT
        # yet.
        return render_template(
            'thanks-mturksubmit.html',
            using_sandbox=(mode == "sandbox"),
            hitid=hit_id,
            assignmentid=assignment_id,
            workerid=worker_id
        )
    elif already_in_db and not (debug_mode or allow_repeats):
        raise ExperimentError('already_did_exp_hit')
    elif status == ALLOCATED or not status or debug_mode:
        # Participant has not yet agreed to the consent. They might not
        # even have accepted the HIT.
        with open('templates/ad.html', 'r') as temp_file:
            ad_string = temp_file.read()
        ad_string = insert_mode(ad_string)
        return render_template_string(
            ad_string,
            mode=mode,
            hitid=hit_id,
            assignmentid=assignment_id,
            workerid=worker_id
        )
    else:
        raise ExperimentError('status_incorrectly_set')
```

## `psiturk/experiment.py` — `function:give_consent` (lines 395-417)

```python
def give_consent():
    """
    Serves up the consent in the popup window.
    """
    if not ('hitId' in request.args and 'assignmentId' in request.args and
            'workerId' in request.args):
        raise ExperimentError('hit_assign_worker_id_not_set_in_consent')
    hit_id = request.args['hitId']
    assignment_id = request.args['assignmentId']
    worker_id = request.args['workerId']
    mode = request.args['mode']
    with open('templates/consent.html', 'r') as temp_file:
        consent_string = temp_file.read()
    consent_string = insert_mode(consent_string)
    return render_template_string(
        consent_string,
        mode=mode,
        hitid=hit_id,
        assignmentid=assignment_id,
        workerid=worker_id
    )
```

## `psiturk/experiment.py` — `function:insert_mode` (lines 736-749)

```python
def insert_mode(page_html):
    """ Insert mode """
    page_html = page_html
    match_found = False
    matches = re.finditer('workerId={{ workerid }}', page_html)
    match = None
    for match in matches:
        match_found = True
    if match_found:
        new_html = page_html[:match.end()] + '&mode={{ mode }}' +\
            page_html[match.end():]
        return new_html
    else:
        raise ExperimentError("insert_mode_failed")
```

## `tests/test_psiturk.py` — `function:test_insert_mode` (lines 139-144)

```python
def test_insert_mode(psiturk_test_client):
    with open('templates/ad.html', 'r') as temp_file:
        ad_string = temp_file.read()

    from psiturk.experiment import insert_mode
    insert_mode(ad_string)
```
