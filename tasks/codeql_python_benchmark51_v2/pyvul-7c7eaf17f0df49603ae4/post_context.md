# Patch-overlapping Python context after the fix

## `pyanyapi/interfaces.py` — `class:YAMLInterface` (lines 272-279)

```python
class YAMLInterface(DictInterface):
    _error_message = 'YAML data can not be parsed.'

    def perform_parsing(self):
        try:
            return yaml.safe_load(self.content)
        except yaml.error.YAMLError:
            raise ResponseParseError(self._error_message, self.content)
```

## `pyanyapi/interfaces.py` — `function:YAMLInterface.perform_parsing` (lines 275-279)

```python
    def perform_parsing(self):
        try:
            return yaml.safe_load(self.content)
        except yaml.error.YAMLError:
            raise ResponseParseError(self._error_message, self.content)
```

## `tests/test_parsers.py` — `function:test_yaml_parser_vulnerability` (lines 66-72)

```python
def test_yaml_parser_vulnerability():
    """
    In case of usage of yaml.load `test` value will be equal to 0.
    """
    parsed = YAMLParser({'test': 'container > test'}).parse('!!python/object/apply:os.system ["exit 0"]')
    with pytest.raises(ResponseParseError):
        parsed.test
```
