# Patch-overlapping Python context before the fix

## `pyanyapi/interfaces.py` — `class:YAMLInterface` (lines 272-279)

```python
class YAMLInterface(DictInterface):
    _error_message = 'YAML data can not be parsed.'

    def perform_parsing(self):
        try:
            return yaml.load(self.content)
        except yaml.error.YAMLError:
            raise ResponseParseError(self._error_message, self.content)
```

## `pyanyapi/interfaces.py` — `function:YAMLInterface.perform_parsing` (lines 275-279)

```python
    def perform_parsing(self):
        try:
            return yaml.load(self.content)
        except yaml.error.YAMLError:
            raise ResponseParseError(self._error_message, self.content)
```

## `tests/test_parsers.py` — `function:test_xml_parsed` (lines 66-76)

```python
def test_xml_parsed(settings):
    parsed = XMLParser(settings).parse(XML_CONTENT)
    assert parsed.success == ['1']
    assert parsed.parse('string(//id/text())') == '32e9a4a2'
```
