# Patch-overlapping Python context before the fix

## `torbot/modules/validators.py` — `function:validate_email` (lines 4-7)

```python
def validate_email(email):
    if not isinstance(email, str):
        return False
    return validators.email(email)
```

## `torbot/modules/validators.py` — `function:validate_link` (lines 10-13)

```python
def validate_link(link):
    if not isinstance(link, str):
        return False
    return validators.url(link)
```

## `torbot/modules/validators.py` — `module:<module>@1` (lines 1-1)

```python
import validators
```
