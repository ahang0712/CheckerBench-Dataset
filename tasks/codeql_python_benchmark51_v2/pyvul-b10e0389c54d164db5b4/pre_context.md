# Patch-overlapping Python context before the fix

## `thefuck/rules/dirty_untar.py` — `function:side_effect` (lines 41-49)

```python
def side_effect(old_cmd, command):
    with tarfile.TarFile(_tar_file(old_cmd.script_parts)[0]) as archive:
        for file in archive.getnames():
            try:
                os.remove(file)
            except OSError:
                # does not try to remove directories as we cannot know if they
                # already existed before
                pass
```

## `thefuck/rules/dirty_unzip.py` — `function:side_effect` (lines 45-53)

```python
def side_effect(old_cmd, command):
    with zipfile.ZipFile(_zip_file(old_cmd), 'r') as archive:
        for file in archive.namelist():
            try:
                os.remove(file)
            except OSError:
                # does not try to remove directories as we cannot know if they
                # already existed before
                pass
```
