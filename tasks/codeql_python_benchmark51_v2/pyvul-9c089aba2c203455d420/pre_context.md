# Patch-overlapping Python context before the fix

## `dpaste/views.py` — `class:APIView` (lines 239-333)

```python
class APIView(View):
    """
    API View
    """

    def _format_default(self, s):
        """
        The default response is the snippet URL wrapped in quotes.
        """
        base_url = config.get_base_url(request=self.request)
        return f'"{base_url}{s.get_absolute_url()}"'

    def _format_url(self, s):
        """
        The `url` format returns the snippet URL,
        no quotes, but a linebreak at the end.
        """
        base_url = config.get_base_url(request=self.request)
        return f"{base_url}{s.get_absolute_url()}\n"

    def _format_json(self, s):
        """
        The `json` format export.
        """
        base_url = config.get_base_url(request=self.request)
        return json.dumps(
            {
                "url": f"{base_url}{s.get_absolute_url()}",
                "content": s.content,
                "lexer": s.lexer,
            }
        )

    def post(self, request, *args, **kwargs):
        content = request.POST.get("content", "")
        lexer = request.POST.get("lexer", highlight.LEXER_DEFAULT).strip()
        filename = request.POST.get("filename", "").strip()
        expires = request.POST.get("expires", "").strip()
        response_format = request.POST.get("format", "default").strip()

        if not content.strip():
            return HttpResponseBadRequest("No content given")

        # We need at least a lexer or a filename
        if not lexer and not filename:
            return HttpResponseBadRequest(
                "No lexer or filename given. Unable to "
                "determine a highlight. Valid lexers are: %s"
                % ", ".join(highlight.LEXER_KEYS)
            )

        # A lexer is given, check if its valid at all
        if lexer and lexer not in highlight.LEXER_KEYS:
            return HttpResponseBadRequest(
                'Invalid lexer "%s" given. Valid lexers are: %s'
                % (lexer, ", ".join(highlight.LEXER_KEYS))
            )

        # No lexer is given, but we have a filename, try to get the lexer
        #  out of it. In case Pygments cannot determine the lexer of the
        # filename, we fallback to 'plain' code.
        if not lexer and filename:
            try:
                lexer_cls = get_lexer_for_filename(filename)
                lexer = lexer_cls.aliases[0]
            except (ClassNotFound, IndexError):
                lexer = config.PLAIN_CODE_SYMBOL

        if expires:
            expire_options = [str(i) for i in dict(config.EXPIRE_CHOICES)]
            if expires not in expire_options:
                return HttpResponseBadRequest(
                    'Invalid expire choice "{}" given. Valid values are: {}'.format(
                        expires, ", ".join(expire_options)
                    )
                )
            expires, expire_type = get_expire_values(expires)
        else:
            expires = datetime.datetime.now() + datetime.timedelta(seconds=60 * 60 * 24)
            expire_type = Snippet.EXPIRE_TIME

        snippet = Snippet.objects.create(
            content=content,
            lexer=lexer,
            expires=expires,
            expire_type=expire_type,
        )

        # Custom formatter for the API response
        formatter = getattr(self, f"_format_{response_format}", None)
        if callable(formatter):
            return HttpResponse(formatter(snippet))

        # Otherwise use the default one.
        return HttpResponse(self._format_default(snippet))
```

## `dpaste/views.py` — `function:APIView.post` (lines 272-333)

```python
    def post(self, request, *args, **kwargs):
        content = request.POST.get("content", "")
        lexer = request.POST.get("lexer", highlight.LEXER_DEFAULT).strip()
        filename = request.POST.get("filename", "").strip()
        expires = request.POST.get("expires", "").strip()
        response_format = request.POST.get("format", "default").strip()

        if not content.strip():
            return HttpResponseBadRequest("No content given")

        # We need at least a lexer or a filename
        if not lexer and not filename:
            return HttpResponseBadRequest(
                "No lexer or filename given. Unable to "
                "determine a highlight. Valid lexers are: %s"
                % ", ".join(highlight.LEXER_KEYS)
            )

        # A lexer is given, check if its valid at all
        if lexer and lexer not in highlight.LEXER_KEYS:
            return HttpResponseBadRequest(
                'Invalid lexer "%s" given. Valid lexers are: %s'
                % (lexer, ", ".join(highlight.LEXER_KEYS))
            )

        # No lexer is given, but we have a filename, try to get the lexer
        #  out of it. In case Pygments cannot determine the lexer of the
        # filename, we fallback to 'plain' code.
        if not lexer and filename:
            try:
                lexer_cls = get_lexer_for_filename(filename)
                lexer = lexer_cls.aliases[0]
            except (ClassNotFound, IndexError):
                lexer = config.PLAIN_CODE_SYMBOL

        if expires:
            expire_options = [str(i) for i in dict(config.EXPIRE_CHOICES)]
            if expires not in expire_options:
                return HttpResponseBadRequest(
                    'Invalid expire choice "{}" given. Valid values are: {}'.format(
                        expires, ", ".join(expire_options)
                    )
                )
            expires, expire_type = get_expire_values(expires)
        else:
            expires = datetime.datetime.now() + datetime.timedelta(seconds=60 * 60 * 24)
            expire_type = Snippet.EXPIRE_TIME

        snippet = Snippet.objects.create(
            content=content,
            lexer=lexer,
            expires=expires,
            expire_type=expire_type,
        )

        # Custom formatter for the API response
        formatter = getattr(self, f"_format_{response_format}", None)
        if callable(formatter):
            return HttpResponse(formatter(snippet))

        # Otherwise use the default one.
        return HttpResponse(self._format_default(snippet))
```

## `dpaste/views.py` — `function:handler500` (lines 350-355)

```python
def handler500(request, template_name="dpaste/500.html"):
    context = {}
    context.update(config.extra_template_context)
    response = render(request, template_name, context, status=500)
    add_never_cache_headers(response)
    return response
```

## `dpaste/views.py` — `module:<module>@16` (lines 16-16)

```python
from django.utils.cache import add_never_cache_headers, patch_cache_control
```
