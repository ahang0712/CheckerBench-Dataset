# Patch-overlapping Python context after the fix

## `xhu.py` — `function:get_paths` (lines 43-49)

```python
def get_paths(root: str, sub_path: str) \
        -> typing.Tuple[pathlib.Path, pathlib.Path]:
    base_path = flask.safe_join(root, sub_path)
    data_file = pathlib.Path(base_path + ".data")
    metadata_file = pathlib.Path(base_path + ".meta")

    return data_file, metadata_file
```

## `xhu.py` — `function:get_info` (lines 57-62)

```python
def get_info(path: str) -> typing.Tuple[
        pathlib.Path,
        dict]:
    data_file, metadata_file = get_paths(app.config["DATA_ROOT"], path)

    return data_file, load_metadata(metadata_file)
```

## `xhu.py` — `function:put_file` (lines 95-157)

```python
def put_file(path):
    try:
        data_file, metadata_file = get_paths(app.config["DATA_ROOT"], path)
    except werkzeug.exceptions.NotFound:
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

    data_file.parent.mkdir(parents=True, exist_ok=True, mode=0o770)

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

## `xhu.py` — `function:head_file` (lines 176-195)

```python
def head_file(path):
    try:
        data_file, metadata = get_info(path)

        stat = data_file.stat()
    except (OSError, werkzeug.exceptions.NotFound):
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

## `xhu.py` — `function:get_file` (lines 198-216)

```python
def get_file(path):
    try:
        data_file, metadata = get_info(path)
    except (OSError, werkzeug.exceptions.NotFound):
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

## `xhu.py` — `module:<module>@32` (lines 32-32)

```python
import werkzeug.exceptions
```
