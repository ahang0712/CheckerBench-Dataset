# Patch-overlapping Python context after the fix

## `mlflow/utils/uri.py` — `function:is_local_uri` (lines 27-61)

```python
def is_local_uri(uri, is_tracking_or_registry_uri=True):
    """
    Returns true if the specified URI is a local file path (/foo or file:/foo).

    :param uri: The URI.
    :param is_tracking_uri: Whether or not the specified URI is an MLflow Tracking or MLflow
                            Model Registry URI. Examples of other URIs are MLflow artifact URIs,
                            filesystem paths, etc.
    """
    if uri == "databricks" and is_tracking_or_registry_uri:
        return False

    if is_windows() and uri.startswith("\\\\"):
        # windows network drive path looks like: "\\<server name>\path\..."
        return False

    parsed_uri = urllib.parse.urlparse(uri)
    scheme = parsed_uri.scheme
    if scheme == "":
        return True

    if parsed_uri.hostname and not (
        parsed_uri.hostname == "."
        or parsed_uri.hostname.startswith("localhost")
        or parsed_uri.hostname.startswith("127.0.0.1")
    ):
        return False

    if scheme == "file":
        return True

    if is_windows() and len(scheme) == 1 and scheme.lower() == pathlib.Path(uri).drive.lower()[0]:
        return True

    return False
```

## `tests/utils/test_uri.py` — `function:test_is_local_uri` (lines 92-110)

```python
def test_is_local_uri():
    assert is_local_uri("mlruns")
    assert is_local_uri("./mlruns")
    assert is_local_uri("file:///foo/mlruns")
    assert is_local_uri("file:foo/mlruns")
    assert is_local_uri("file://./mlruns")
    assert is_local_uri("file://localhost/mlruns")
    assert is_local_uri("file://localhost:5000/mlruns")
    assert is_local_uri("file://127.0.0.1/mlruns")
    assert is_local_uri("file://127.0.0.1:5000/mlruns")
    assert is_local_uri("//proc/self/root")
    assert is_local_uri("/proc/self/root")

    assert not is_local_uri("file://myhostname/path/to/file")
    assert not is_local_uri("https://whatever")
    assert not is_local_uri("http://whatever")
    assert not is_local_uri("databricks")
    assert not is_local_uri("databricks:whatever")
    assert not is_local_uri("databricks://whatever")
```
