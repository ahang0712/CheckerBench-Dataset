# Patch-overlapping Python context before the fix

## `xhu.py` — `function:sanitized_join` (lines 42-46)

```python
def sanitized_join(path: str, root: pathlib.Path) -> pathlib.Path:
    result = (root / path).absolute()
    if not str(result).startswith(str(root) + "/"):
        raise ValueError("resulting path is outside root")
    return result
```

## `xhu.py` — `function:get_paths` (lines 49-53)

```python
def get_paths(base_path: pathlib.Path):
    data_file = pathlib.Path(str(base_path) + ".data")
    metadata_file = pathlib.Path(str(base_path) + ".meta")

    return data_file, metadata_file
```

## `xhu.py` — `function:get_info` (lines 61-71)

```python
def get_info(path: str, root: pathlib.Path) -> typing.Tuple[
        pathlib.Path,
        dict]:
    dest_path = sanitized_join(
        path,
        pathlib.Path(app.config["DATA_ROOT"]),
    )

    data_file, metadata_file = get_paths(dest_path)

    return data_file, load_metadata(metadata_file)
```

## `xhu.py` — `function:put_file` (lines 104-170)

```python
def put_file(path):
    try:
        dest_path = sanitized_join(
            path,
            pathlib.Path(app.config["DATA_ROOT"]),
        )
    except ValueError:
        return flask.Response(
            "Not Found",
            404,
            mimetype="text/plain",
        )

    verification_key = flask.request.args.get("v", "")
    length = int(flask.request.headers.get("Content-Length", 0))
    hmac_input = "{} {}".format(path, length).encode("utf-8")
    key = app.config["SECRET_KEY"]
    mac = hmac.new(key, hmac_input, hashlib.sha256)
    digest = mac.hexdigest()

    if not hmac.compare_digest(digest, verification_key):
        return flask.Response(
            "Invalid verification key",
            403,
            mimetype="text/plain",
        )

    content_type = flask.request.headers.get(
        "Content-Type",
        "application/octet-stream",
    )

    dest_path.parent.mkdir(parents=True, exist_ok=True, mode=0o770)
    data_file, metadata_file = get_paths(dest_path)

    try:
        with write_file(data_file) as fout:
            stream_file(flask.request.stream, fout, length)

            with metadata_file.open("x") as f:
                json.dump(
                    {
                        "headers": {"Content-Type": content_type},
                    },
                    f,
                )
    except EOFError:
        return flask.Response(
            "Bad Request",
            400,
            mimetype="text/plain",
        )
    except OSError as exc:
        if exc.errno == errno.EEXIST:
            return flask.Response(
                "Conflict",
                409,
                mimetype="text/plain",
            )
        raise

    return flask.Response(
        "Created",
        201,
        mimetype="text/plain",
    )
```

## `xhu.py` — `function:head_file` (lines 189-211)

```python
def head_file(path):
    try:
        data_file, metadata = get_info(
            path,
            pathlib.Path(app.config["DATA_ROOT"])
        )

        stat = data_file.stat()
    except (OSError, ValueError):
        return flask.Response(
            "Not Found",
            404,
            mimetype="text/plain",
        )

    response = flask.Response()
    response.headers["Content-Length"] = str(stat.st_size)
    generate_headers(
        response.headers,
        metadata["headers"],
    )
    return response
```

## `xhu.py` — `function:get_file` (lines 214-235)

```python
def get_file(path):
    try:
        data_file, metadata = get_info(
            path,
            pathlib.Path(app.config["DATA_ROOT"])
        )
    except (OSError, ValueError):
        return flask.Response(
            "Not Found",
            404,
            mimetype="text/plain",
        )

    response = flask.make_response(flask.send_file(
        str(data_file),
    ))
    generate_headers(
        response.headers,
        metadata["headers"],
    )
    return response
```
