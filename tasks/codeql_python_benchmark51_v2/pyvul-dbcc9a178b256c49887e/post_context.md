# Patch-overlapping Python context after the fix

## `web/pgadmin/misc/__init__.py` — `function:validate_binary_path` (lines 240-268)

```python
def validate_binary_path():
    """
    This function is used to validate the specified utilities path by
    running the utilities with their versions.
    """
    data = None
    if hasattr(request.data, 'decode'):
        data = request.data.decode('utf-8')

    if data != '':
        data = json.loads(data)

    version_str = ''
    if 'utility_path' in data and data['utility_path'] is not None:
        binary_versions = get_binary_path_versions(data['utility_path'])
        for utility, version in binary_versions.items():
            if version is None:
                version_str += "<b>" + utility + ":</b> " + \
                               "not found on the specified binary path.<br/>"
            else:
                version_str += "<b>" + utility + ":</b> " + version + "<br/>"
    else:
        return precondition_required(gettext('Invalid binary path.'))

    return make_json_response(data=gettext(version_str), status=200)
```

## `web/pgadmin/misc/__init__.py` — `module:<module>@16` (lines 16-17)

```python
from pgadmin.utils import PgAdminModule, replace_binary_path, \
    get_binary_path_versions
```

## `web/pgadmin/utils/__init__.py` — `function:get_binary_path_versions` (lines 354-385)

```python
def get_binary_path_versions(binary_path: str) -> dict:
    ret = {}
    binary_path = os.path.abspath(
        replace_binary_path(binary_path)
    )

    for utility in UTILITIES_ARRAY:
        ret[utility] = None
        full_path = os.path.join(binary_path,
                                 (utility if os.name != 'nt' else
                                  (utility + '.exe')))

        try:
            # if path doesn't exist raise exception
            if not os.path.isdir(binary_path):
                current_app.logger.warning('Invalid binary path.')
                raise Exception()
            # Get the output of the '--version' command
            cmd = subprocess.run(
                [full_path, '--version'],
                shell=False,
                capture_output=True,
                text=True
            )
            if cmd.returncode == 0:
                ret[utility] = cmd.stdout.split(") ", 1)[1].strip()
            else:
                raise Exception()
        except Exception as _:
            continue

    return ret
```

## `web/pgadmin/utils/__init__.py` — `function:set_binary_path` (lines 388-420)

```python
def set_binary_path(binary_path, bin_paths, server_type,
                    version_number=None, set_as_default=False):
    """
    This function is used to iterate through the utilities and set the
    default binary path.
    """
    path_with_dir = binary_path if "$DIR" in binary_path else None
    binary_versions = get_binary_path_versions(binary_path)

    for utility, version in binary_versions.items():
        version_number = version if version_number is None else version_number
        if version_number.find('.'):
            version_number = version_number.split('.', 1)[0]
        try:
            # Get the paths array based on server type
            if 'pg_bin_paths' in bin_paths or 'as_bin_paths' in bin_paths:
                paths_array = bin_paths['pg_bin_paths']
                if server_type == 'ppas':
                    paths_array = bin_paths['as_bin_paths']
            else:
                paths_array = bin_paths

            for path in paths_array:
                if path['version'].find(version_number) == 0 and \
                        path['binaryPath'] is None:
                    path['binaryPath'] = path_with_dir \
                        if path_with_dir is not None else binary_path
                    if set_as_default:
                        path['isDefault'] = True
                    break
            break
        except Exception:
            continue
```
