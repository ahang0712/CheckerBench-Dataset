# Patch-overlapping Python context before the fix

## `src/fides/api/service/privacy_request/request_runner_service.py` — `function:generate_id_verification_code` (lines 626-630)

```python
def generate_id_verification_code() -> str:
    """
    Generate one-time identity verification code
    """
    return str(random.choice(range(100000, 999999)))
```

## `src/fides/api/service/privacy_request/request_runner_service.py` — `module:<module>@1` (lines 1-1)

```python
import random
```

## `src/fides/api/service/privacy_request/request_runner_service.py` — `module:<module>@2` (lines 2-2)

```python
from datetime import datetime, timedelta
```
