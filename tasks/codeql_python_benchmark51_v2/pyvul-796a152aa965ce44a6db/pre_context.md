# Patch-overlapping Python context before the fix

## `flask_unchained/bundles/controller/utils.py` — `function:redirect` (lines 187-239)

```python
def redirect(where: Optional[str] = None,
             default: Optional[str] = None,
             override: Optional[str] = None,
             _anchor: Optional[str] = None,
             _cls: Optional[Union[object, type]] = None,
             _external: Optional[bool] = False,
             _external_host: Optional[str] = None,
             _method: Optional[str] = None,
             _scheme: Optional[str] = None,
             **values,
             ) -> Response:
    """
    An improved version of flask's redirect function

    :param where: A URL, endpoint, or config key name to redirect to
    :param default: A URL, endpoint, or config key name to redirect to if
      ``where`` is invalid
    :param override: explicitly redirect to a URL, endpoint, or config key name
      (takes precedence over the ``next`` value in query strings or forms)
    :param values: the variable arguments of the URL rule
    :param _anchor: if provided this is added as anchor to the URL.
    :param _cls: if specified, allows a method name to be passed to where,
      default, and/or override
    :param _external: if set to ``True``, an absolute URL is generated. Server
      address can be changed via ``SERVER_NAME`` configuration variable which
      defaults to `localhost`.
    :param _external_host: if specified, the host of an external server to
      generate urls for (eg https://example.com or localhost:8888)
    :param _method: if provided this explicitly specifies an HTTP method.
    :param _scheme: a string specifying the desired URL scheme. The `_external`
      parameter must be set to ``True`` or a :exc:`ValueError` is raised. The
      default behavior uses the same scheme as the current request, or
      ``PREFERRED_URL_SCHEME`` from the :ref:`app configuration <config>` if no
      request context is available. As of Werkzeug 0.10, this also can be set
      to an empty string to build protocol-relative URLs.
    """
    flask_url_for_kwargs = dict(_anchor=_anchor, _external=_external,
                                _external_host=_external_host, _method=_method,
                                _scheme=_scheme, **values)

    urls = [url_for(request.args.get('next'), **flask_url_for_kwargs),
            url_for(request.form.get('next'), **flask_url_for_kwargs)]
    if where:
        urls.append(url_for(where, _cls=_cls, **flask_url_for_kwargs))
    if default:
        urls.append(url_for(default, _cls=_cls, **flask_url_for_kwargs))
    if override:
        urls.insert(0, url_for(override, _cls=_cls, **flask_url_for_kwargs))

    for url in urls:
        if _validate_redirect_url(url, _external_host):
            return flask_redirect(url)
    return flask_redirect('/')
```

## `flask_unchained/bundles/controller/utils.py` — `function:_validate_redirect_url` (lines 291-301)

```python
def _validate_redirect_url(url, _external_host=None):
    if url is None or url.strip() == '':
        return False
    url_next = urlsplit(url)
    url_base = urlsplit(request.host_url)
    external_host = _external_host or current_app.config.get('EXTERNAL_SERVER_NAME', '')
    if ((url_next.netloc or url_next.scheme)
            and url_next.netloc != url_base.netloc
            and url_next.netloc not in external_host):
        return False
    return True
```

## `flask_unchained/bundles/controller/utils.py` — `module:<module>@9` (lines 9-9)

```python
from urllib.parse import urlsplit
```

## `flask_unchained/bundles/controller/utils.py` — `module:<module>@10` (lines 10-10)

```python
from werkzeug.routing import BuildError, UnicodeConverter
```
