# Patch-overlapping Python context before the fix

## `grappelli/views/switch.py` — `function:switch_user` (lines 22-69)

```python
def switch_user(request, object_id):

    # current/session user
    current_user = request.user
    session_user = request.session.get("original_user", {"id": current_user.id, "username": current_user.get_username()})

    # check redirect
    redirect_url = request.GET.get("redirect", None)
    if redirect_url is None or not redirect_url.startswith("/"):
        raise Http404()

    # check original_user
    try:
        original_user = User.objects.get(pk=session_user["id"], is_staff=True)
        if not SWITCH_USER_ORIGINAL(original_user):
            messages.add_message(request, messages.ERROR, _("Permission denied."))
            return redirect(request.GET.get("redirect"))
    except ObjectDoesNotExist:
        msg = _('%(name)s object with primary key %(key)r does not exist.') % {'name': "User", 'key': escape(session_user["id"])}
        messages.add_message(request, messages.ERROR, msg)
        return redirect(request.GET.get("redirect"))

    # check new user
    try:
        target_user = User.objects.get(pk=object_id, is_staff=True)
        if target_user != original_user and not SWITCH_USER_TARGET(original_user, target_user):
            messages.add_message(request, messages.ERROR, _("Permission denied."))
            return redirect(request.GET.get("redirect"))
    except ObjectDoesNotExist:
        msg = _('%(name)s object with primary key %(key)r does not exist.') % {'name': "User", 'key': escape(object_id)}
        messages.add_message(request, messages.ERROR, msg)
        return redirect(request.GET.get("redirect"))

    # find backend
    if not hasattr(target_user, 'backend'):
        for backend in settings.AUTHENTICATION_BACKENDS:
            if target_user == load_backend(backend).get_user(target_user.pk):
                target_user.backend = backend
                break

    # target user login, set original as session
    if hasattr(target_user, 'backend'):
        login(request, target_user)
        if original_user.id != target_user.id:
            request.session["original_user"] = {"id": original_user.id, "username": original_user.get_username()}

    return redirect(request.GET.get("redirect"))
```

## `grappelli/views/switch.py` — `module:<module>@11` (lines 11-11)

```python
from django.utils.translation import gettext_lazy as _
```
