# Patch-overlapping Python context after the fix

## `flask_unchained/bundles/controller/utils.py` — `function:encode_non_url_reserved_characters` (lines 186-188)

```python
def encode_non_url_reserved_characters(url):
    # safe url reserved characters: https://datatracker.ietf.org/doc/html/rfc3986#section-2.2
    return urlquote(url, safe=":/?#[]@!$&'()*+,;=")
```

## `flask_unchained/bundles/controller/utils.py` — `function:redirect` (lines 192-244)

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
            return flask_redirect(encode_non_url_reserved_characters(url))
    return flask_redirect('/')
```

## `flask_unchained/bundles/controller/utils.py` — `function:_validate_redirect_url` (lines 296-320)

```python
def _validate_redirect_url(url, _external_host=None):
    url = (url or '').strip().replace('\\', '/')

    # reject empty urls and urls starting with 3+ slashes or a control character
    if not url or url.startswith('///') or ord(url[0]) <= 32:
        return False

    url_next = urlsplit(url)
    url_base = urlsplit(request.host_url)
    if url_next.netloc or url_next.scheme:
        # require both netloc and scheme
        if not url_next.netloc or not url_next.scheme:
            return False

        # if external host, require same netloc and scheme
        external_host = _external_host or current_app.config.get('EXTERNAL_SERVER_NAME', '')
        if external_host:
            url_external = urlsplit(external_host)
            if url_next.netloc == url_external.netloc and url_next.scheme == url_external.scheme:
                return True

        # require same netloc and scheme
        if url_next.netloc != url_base.netloc or url_next.scheme != url_base.scheme:
            return False
    return True
```

## `flask_unchained/bundles/controller/utils.py` — `module:<module>@9` (lines 9-9)

```python
from urllib.parse import urlsplit, quote as urlquote
```
