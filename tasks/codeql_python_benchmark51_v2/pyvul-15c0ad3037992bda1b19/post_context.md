# Patch-overlapping Python context after the fix

## `svglib/svglib.py` — `function:svg2rlg` (lines 1406-1429)

```python
def svg2rlg(path, resolve_entities=False, **kwargs):
    "Convert an SVG file to an RLG Drawing object."

    # unzip .svgz file into .svg
    unzipped = False
    if isinstance(path, str) and os.path.splitext(path)[1].lower() == ".svgz":
        with gzip.open(path, 'rb') as f_in, open(path[:-1], 'wb') as f_out:
            shutil.copyfileobj(f_in, f_out)
        path = path[:-1]
        unzipped = True

    svg_root = load_svg_file(path, resolve_entities=resolve_entities)
    if svg_root is None:
        return

    # convert to a RLG drawing
    svgRenderer = SvgRenderer(path, **kwargs)
    drawing = svgRenderer.render(svg_root)

    # remove unzipped .svgz file (.svg)
    if unzipped:
        os.remove(path)

    return drawing
```

## `svglib/svglib.py` — `function:load_svg_file` (lines 1432-1442)

```python
def load_svg_file(path, resolve_entities=False):
    parser = etree.XMLParser(
        remove_comments=True, recover=True, resolve_entities=resolve_entities
    )
    try:
        doc = etree.parse(path, parser=parser)
        svg_root = doc.getroot()
    except Exception as exc:
        logger.error("Failed to load input file! (%s)" % exc)
    else:
        return svg_root
```
