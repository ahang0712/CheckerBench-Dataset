# Patch-overlapping Python context before the fix

## `web/pgadmin/misc/__init__.py` — `function:validate_binary_path` (lines 239-291)

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
        # Check if "$DIR" present in binary path
        binary_path = replace_binary_path(data['utility_path'])

        for utility in UTILITIES_ARRAY:
            full_path = os.path.abspath(
                os.path.join(binary_path,
                             (utility if os.name != 'nt' else
                              (utility + '.exe'))))

            try:
                # if path doesn't exist raise exception
                if not os.path.exists(binary_path):
                    current_app.logger.warning('Invalid binary path.')
                    raise Exception()
                # escape double quotes to avoid command injection.
                # Get the output of the '--version' command
                version_string = \
                    subprocess.getoutput(r'"{0}" --version'.format(
                        full_path.replace('"', '""')))
                # Get the version number by splitting the result string
                version_string.split(") ", 1)[1].split('.', 1)[0]
            except Exception:
                version_str += "<b>" + utility + ":</b> " + \
                               "not found on the specified binary path.<br/>"
                continue

            # Replace the name of the utility from the result to avoid
            # duplicate name.
            result_str = version_string.replace(utility, '')

            version_str += "<b>" + utility + ":</b> " + result_str + "<br/>"
    else:
        return precondition_required(gettext('Invalid binary path.'))

    return make_json_response(data=gettext(version_str), status=200)
```

## `web/pgadmin/misc/__init__.py` — `module:<module>@16` (lines 16-16)

```python
from pgadmin.utils import PgAdminModule, replace_binary_path
```

## `web/pgadmin/misc/__init__.py` — `module:<module>@17` (lines 17-17)

```python
from pgadmin.utils.csrf import pgCSRFProtect
```

## `web/pgadmin/utils/__init__.py` — `function:set_binary_path` (lines 354-401)

```python
def set_binary_path(binary_path, bin_paths, server_type,
                    version_number=None, set_as_default=False):
    """
    This function is used to iterate through the utilities and set the
    default binary path.
    """
    path_with_dir = binary_path if "$DIR" in binary_path else None

    # Check if "$DIR" present in binary path
    binary_path = replace_binary_path(binary_path)

    for utility in UTILITIES_ARRAY:
        full_path = os.path.abspath(
            os.path.join(binary_path, (utility if os.name != 'nt' else
                                       (utility + '.exe'))))

        try:
            # if version_number is provided then no need to fetch it.
            if version_number is None:
                # Get the output of the '--version' command
                version_string = \
                    subprocess.getoutput('"{0}" --version'.format(full_path))

                # Get the version number by splitting the result string
                version_number = \
                    version_string.split(") ", 1)[1].split('.', 1)[0]
            elif version_number.find('.'):
                version_number = version_number.split('.', 1)[0]

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
