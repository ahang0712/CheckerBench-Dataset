# Patch-overlapping Python context after the fix

## `simiki/config.py` — `function:parse_config` (lines 57-69)

```python
def parse_config(config_file):
    if not os.path.exists(config_file):
        raise ConfigFileNotFound("{0} not exists".format(config_file))

    default_config = _set_default_config()

    with io.open(config_file, "rt", encoding="utf-8") as fd:
        config = yaml.load(fd, Loader=yaml.FullLoader)

    default_config.update(config)
    config = _post_process(default_config)

    return config
```

## `simiki/generators.py` — `class:PageGenerator` (lines 92-284)

```python
class PageGenerator(BaseGenerator):

    def __init__(self, site_config, base_path, tags=None):
        super(PageGenerator, self).__init__(site_config, base_path)
        self._tags = tags
        self._reset()

    def _reset(self):
        """Reset the global self variables"""
        self._src_file = None  # source file path relative to base_path
        self.meta = None
        self.content = None

    def to_html(self, src_file, include_draft=False):
        """Load template, and generate html

        :src_file: the filename of the source file. This can either be an
                   absolute filename or a filename relative to the base path.
        :include_draft: True/False, include draft pages or not to generate
        """
        self._reset()
        self._src_file = os.path.relpath(src_file, self.base_path)
        self.meta, self.content = self.get_meta_and_content()
        # Page set `draft: True' mark current page as draft, and will
        # be ignored if not forced generate include draft pages
        if not include_draft and self.meta.get('draft', False):
            return None
        layout = self.get_layout(self.meta)
        template_vars = self.get_template_vars(self.meta, self.content)
        template = self.get_template(layout)
        html = template.render(template_vars)

        return html

    @property
    def src_file(self):
        return self._src_file

    @src_file.setter
    def src_file(self, filename):
        self._src_file = os.path.relpath(filename, self.base_path)

    def get_meta_and_content(self, do_render=True):
        meta_str, content_str = self.extract_page(self._src_file)
        meta = self.parse_meta(meta_str)
        # This is the most time consuming part
        if do_render and meta.get('render', True):
            content = self._parse_markup(content_str)
        else:
            content = content_str

        return meta, content

    def get_layout(self, meta):
        """Get layout config in meta, default is `page'"""
        if "layout" in meta:
            # Compatible with previous version, which default layout is "post"
            # XXX Will remove this checker in v2.0
            if meta["layout"] == "post":
                warn_msg = "{0}: layout `post' is deprecated, use `page'" \
                           .format(self._src_file)
                if is_py2:
                    # XXX: warnings message require str, no matter whether
                    # py2 or py3; but in py3, bytes message is ok in simple
                    # test, but failed in unittest with py3.3, ok with py3.4?
                    warn_msg = warn_msg.encode('utf-8')
                warnings.warn(warn_msg, DeprecationWarning)
                layout = "page"
            else:
                layout = meta["layout"]
        else:
            layout = "page"

        return layout

    def get_template_vars(self, meta, content):
        """Get template variables, include site config and page config"""
        template_vars = copy.deepcopy(self._template_vars)
        page = {"content": content}
        page.update(meta)
        page.update({'relation': self.get_relation()})

        template_vars.update({'page': page})

        return template_vars

    def get_category_and_file(self):
        """Get the name of category and file(with extension)"""
        src_file_relpath_to_source = \
            os.path.relpath(self._src_file, self.site_config['source'])
        category, filename = os.path.split(src_file_relpath_to_source)
        return (category, filename)

    @staticmethod
    def extract_page(filename):
        """Split the page file texts by triple-dashed lines, return the mata
        and content.

        :param filename: the filename of markup page

        returns:
          meta_str (str): page's meta string
          content_str (str): html parsed from markdown or other markup text.
        """
        regex = re.compile('(?sm)^---(?P<meta>.*?)^---(?P<body>.*)')
        with io.open(filename, "rt", encoding="utf-8") as fd:
            match_obj = re.match(regex, fd.read())
            if match_obj:
                meta_str = match_obj.group('meta')
                content_str = match_obj.group('body')
            else:
                raise Exception('extracting page with format error, '
                                'see <http://simiki.org/docs/metadata.html>')

        return meta_str, content_str

    def parse_meta(self, yaml_str):
        """Parse meta from yaml string, and validate yaml filed, return dict"""
        try:
            meta = yaml.load(yaml_str, Loader=yaml.FullLoader)
        except yaml.YAMLError as e:
            e.extra_msg = 'yaml format error'
            raise

        category, src_fname = self.get_category_and_file()
        dst_fname = src_fname.replace(
            ".{0}".format(self.site_config['default_ext']), '.html')
        meta.update({'category': category, 'filename': dst_fname})

        if 'tag' in meta:
            if isinstance(meta['tag'], basestring):
                _tags = [t.strip() for t in meta['tag'].split(',')]
                meta.update({'tag': _tags})

        if "title" not in meta:
            raise Exception("no 'title' in meta")

        return meta

    def _parse_markup(self, markup_text):
        """Parse markup text to html

        Only support Markdown for now.
        """
        markdown_extensions = self._set_markdown_extensions()

        html_content = markdown.markdown(
            markup_text,
            extensions=markdown_extensions,
        )

        return html_content

    def _set_markdown_extensions(self):
        """Set the extensions for markdown parser"""
        # Default enabled extensions
        markdown_extensions_config = {
            "fenced_code": {},
            "nl2br": {},
            "toc": {"title": "Table of Contents"},
            "extra": {},
        }
        # Handle pygments
        if self.site_config["pygments"]:
            markdown_extensions_config.update({
                "codehilite": {"css_class": "hlcode"}
            })
        # Handle markdown_ext
        # Ref: https://pythonhosted.org/Markdown/extensions/index.html#officially-supported-extensions  # noqa
        if "markdown_ext" in self.site_config:
            markdown_extensions_config.update(self.site_config["markdown_ext"])

        markdown_extensions = []
        for k, v in markdown_extensions_config.items():
            ext = import_string("markdown.extensions." + k).makeExtension()
            if v:
                for i, j in v.items():
                    ext.setConfig(i, j)
            markdown_extensions.append(ext)

        return markdown_extensions

    def get_relation(self):
        rn = []
        if self._tags and 'tag' in self.meta:
            for t in self.meta['tag']:
                rn.extend(self._tags[t])
        # remove itself
        rn = [r for r in rn if self.meta['title'] != r['title']]
        # remove the duplicate items
        # note this will change the items order
        rn = [r for n, r in enumerate(rn) if r not in rn[n+1:]]  # noqa: E226
        return rn
```

## `simiki/generators.py` — `function:PageGenerator.parse_meta` (lines 208-229)

```python
    def parse_meta(self, yaml_str):
        """Parse meta from yaml string, and validate yaml filed, return dict"""
        try:
            meta = yaml.load(yaml_str, Loader=yaml.FullLoader)
        except yaml.YAMLError as e:
            e.extra_msg = 'yaml format error'
            raise

        category, src_fname = self.get_category_and_file()
        dst_fname = src_fname.replace(
            ".{0}".format(self.site_config['default_ext']), '.html')
        meta.update({'category': category, 'filename': dst_fname})

        if 'tag' in meta:
            if isinstance(meta['tag'], basestring):
                _tags = [t.strip() for t in meta['tag'].split(',')]
                meta.update({'tag': _tags})

        if "title" not in meta:
            raise Exception("no 'title' in meta")

        return meta
```
