# Patch-overlapping Python context after the fix

## `sqlparse/filters/others.py` — `class:StripCommentsFilter` (lines 14-56)

```python
class StripCommentsFilter:

    @staticmethod
    def _process(tlist):
        def get_next_comment():
            # TODO(andi) Comment types should be unified, see related issue38
            return tlist.token_next_by(i=sql.Comment, t=T.Comment)

        def _get_insert_token(token):
            """Returns either a whitespace or the line breaks from token."""
            # See issue484 why line breaks should be preserved.
            # Note: The actual value for a line break is replaced by \n
            # in SerializerUnicode which will be executed in the
            # postprocessing state.
            m = re.search(r'((\r|\n)+) *$', token.value)
            if m is not None:
                return sql.Token(T.Whitespace.Newline, m.groups()[0])
            else:
                return sql.Token(T.Whitespace, ' ')

        tidx, token = get_next_comment()
        while token:
            pidx, prev_ = tlist.token_prev(tidx, skip_ws=False)
            nidx, next_ = tlist.token_next(tidx, skip_ws=False)
            # Replace by whitespace if prev and next exist and if they're not
            # whitespaces. This doesn't apply if prev or next is a parenthesis.
            if (prev_ is None or next_ is None
                    or prev_.is_whitespace or prev_.match(T.Punctuation, '(')
                    or next_.is_whitespace or next_.match(T.Punctuation, ')')):
                # Insert a whitespace to ensure the following SQL produces
                # a valid SQL (see #425).
                if prev_ is not None and not prev_.match(T.Punctuation, '('):
                    tlist.tokens.insert(tidx, _get_insert_token(token))
                tlist.tokens.remove(token)
            else:
                tlist.tokens[tidx] = _get_insert_token(token)

            tidx, token = get_next_comment()

    def process(self, stmt):
        [self.process(sgroup) for sgroup in stmt.get_sublists()]
        StripCommentsFilter._process(stmt)
        return stmt
```

## `sqlparse/filters/others.py` — `function:StripCommentsFilter._process` (lines 16-51)

```python
    def _process(tlist):
        def get_next_comment():
            # TODO(andi) Comment types should be unified, see related issue38
            return tlist.token_next_by(i=sql.Comment, t=T.Comment)

        def _get_insert_token(token):
            """Returns either a whitespace or the line breaks from token."""
            # See issue484 why line breaks should be preserved.
            # Note: The actual value for a line break is replaced by \n
            # in SerializerUnicode which will be executed in the
            # postprocessing state.
            m = re.search(r'((\r|\n)+) *$', token.value)
            if m is not None:
                return sql.Token(T.Whitespace.Newline, m.groups()[0])
            else:
                return sql.Token(T.Whitespace, ' ')

        tidx, token = get_next_comment()
        while token:
            pidx, prev_ = tlist.token_prev(tidx, skip_ws=False)
            nidx, next_ = tlist.token_next(tidx, skip_ws=False)
            # Replace by whitespace if prev and next exist and if they're not
            # whitespaces. This doesn't apply if prev or next is a parenthesis.
            if (prev_ is None or next_ is None
                    or prev_.is_whitespace or prev_.match(T.Punctuation, '(')
                    or next_.is_whitespace or next_.match(T.Punctuation, ')')):
                # Insert a whitespace to ensure the following SQL produces
                # a valid SQL (see #425).
                if prev_ is not None and not prev_.match(T.Punctuation, '('):
                    tlist.tokens.insert(tidx, _get_insert_token(token))
                tlist.tokens.remove(token)
            else:
                tlist.tokens[tidx] = _get_insert_token(token)

            tidx, token = get_next_comment()
```

## `sqlparse/filters/others.py` — `function:StripCommentsFilter._process._get_insert_token` (lines 22-32)

```python
        def _get_insert_token(token):
            """Returns either a whitespace or the line breaks from token."""
            # See issue484 why line breaks should be preserved.
            # Note: The actual value for a line break is replaced by \n
            # in SerializerUnicode which will be executed in the
            # postprocessing state.
            m = re.search(r'((\r|\n)+) *$', token.value)
            if m is not None:
                return sql.Token(T.Whitespace.Newline, m.groups()[0])
            else:
                return sql.Token(T.Whitespace, ' ')
```

## `tests/test_format.py` — `class:TestFormat` (lines 7-138)

```python
class TestFormat:
    def test_keywordcase(self):
        sql = 'select * from bar; -- select foo\n'
        res = sqlparse.format(sql, keyword_case='upper')
        assert res == 'SELECT * FROM bar; -- select foo\n'
        res = sqlparse.format(sql, keyword_case='capitalize')
        assert res == 'Select * From bar; -- select foo\n'
        res = sqlparse.format(sql.upper(), keyword_case='lower')
        assert res == 'select * from BAR; -- SELECT FOO\n'

    def test_keywordcase_invalid_option(self):
        sql = 'select * from bar; -- select foo\n'
        with pytest.raises(SQLParseError):
            sqlparse.format(sql, keyword_case='foo')

    def test_identifiercase(self):
        sql = 'select * from bar; -- select foo\n'
        res = sqlparse.format(sql, identifier_case='upper')
        assert res == 'select * from BAR; -- select foo\n'
        res = sqlparse.format(sql, identifier_case='capitalize')
        assert res == 'select * from Bar; -- select foo\n'
        res = sqlparse.format(sql.upper(), identifier_case='lower')
        assert res == 'SELECT * FROM bar; -- SELECT FOO\n'

    def test_identifiercase_invalid_option(self):
        sql = 'select * from bar; -- select foo\n'
        with pytest.raises(SQLParseError):
            sqlparse.format(sql, identifier_case='foo')

    def test_identifiercase_quotes(self):
        sql = 'select * from "foo"."bar"'
        res = sqlparse.format(sql, identifier_case="upper")
        assert res == 'select * from "foo"."bar"'

    def test_strip_comments_single(self):
        sql = 'select *-- statement starts here\nfrom foo'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select *\nfrom foo'
        sql = 'select * -- statement starts here\nfrom foo'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select *\nfrom foo'
        sql = 'select-- foo\nfrom -- bar\nwhere'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select\nfrom\nwhere'
        sql = 'select *-- statement starts here\n\nfrom foo'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select *\n\nfrom foo'
        sql = 'select * from foo-- statement starts here\nwhere'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select * from foo\nwhere'
        sql = 'select a-- statement starts here\nfrom foo'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select a\nfrom foo'
        sql = '--comment\nselect a-- statement starts here\n' \
              'from foo--comment\nf'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select a\nfrom foo\nf'

    def test_strip_comments_invalid_option(self):
        sql = 'select-- foo\nfrom -- bar\nwhere'
        with pytest.raises(SQLParseError):
            sqlparse.format(sql, strip_comments=None)

    def test_strip_comments_multi(self):
        sql = '/* sql starts here */\nselect'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select'
        sql = '/* sql starts here */ select'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select'
        sql = '/*\n * sql starts here\n */\nselect'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select'
        sql = 'select (/* sql starts here */ select 2)'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select (select 2)'
        sql = 'select (/* sql /* starts here */ select 2)'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select (select 2)'

    def test_strip_comments_preserves_linebreak(self):
        sql = 'select * -- a comment\r\nfrom foo'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select *\nfrom foo'
        sql = 'select * -- a comment\nfrom foo'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select *\nfrom foo'
        sql = 'select * -- a comment\rfrom foo'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select *\nfrom foo'
        sql = 'select * -- a comment\r\n\r\nfrom foo'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select *\n\nfrom foo'
        sql = 'select * -- a comment\n\nfrom foo'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select *\n\nfrom foo'

    def test_strip_ws(self):
        f = lambda sql: sqlparse.format(sql, strip_whitespace=True)
        s = 'select\n* from      foo\n\twhere  ( 1 = 2 )\n'
        assert f(s) == 'select * from foo where (1 = 2)'
        s = 'select -- foo\nfrom    bar\n'
        assert f(s) == 'select -- foo\nfrom bar'

    def test_strip_ws_invalid_option(self):
        s = 'select -- foo\nfrom    bar\n'
        with pytest.raises(SQLParseError):
            sqlparse.format(s, strip_whitespace=None)

    def test_preserve_ws(self):
        # preserve at least one whitespace after subgroups
        f = lambda sql: sqlparse.format(sql, strip_whitespace=True)
        s = 'select\n* /* foo */  from bar '
        assert f(s) == 'select * /* foo */ from bar'

    def test_notransform_of_quoted_crlf(self):
        # Make sure that CR/CR+LF characters inside string literals don't get
        # affected by the formatter.

        s1 = "SELECT some_column LIKE 'value\r'"
        s2 = "SELECT some_column LIKE 'value\r'\r\nWHERE id = 1\n"
        s3 = "SELECT some_column LIKE 'value\\'\r' WHERE id = 1\r"
        s4 = "SELECT some_column LIKE 'value\\\\\\'\r' WHERE id = 1\r\n"

        f = lambda x: sqlparse.format(x)

        # Because of the use of
        assert f(s1) == "SELECT some_column LIKE 'value\r'"
        assert f(s2) == "SELECT some_column LIKE 'value\r'\nWHERE id = 1\n"
        assert f(s3) == "SELECT some_column LIKE 'value\\'\r' WHERE id = 1\n"
        assert (f(s4)
                == "SELECT some_column LIKE 'value\\\\\\'\r' WHERE id = 1\n")
```

## `tests/test_format.py` — `function:TestFormat.test_strip_comments_preserves_linebreak` (lines 87-102)

```python
    def test_strip_comments_preserves_linebreak(self):
        sql = 'select * -- a comment\r\nfrom foo'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select *\nfrom foo'
        sql = 'select * -- a comment\nfrom foo'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select *\nfrom foo'
        sql = 'select * -- a comment\rfrom foo'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select *\nfrom foo'
        sql = 'select * -- a comment\r\n\r\nfrom foo'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select *\n\nfrom foo'
        sql = 'select * -- a comment\n\nfrom foo'
        res = sqlparse.format(sql, strip_comments=True)
        assert res == 'select *\n\nfrom foo'
```
