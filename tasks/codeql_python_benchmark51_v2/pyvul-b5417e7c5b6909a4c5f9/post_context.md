# Patch-overlapping Python context after the fix

## `rply/parsergenerator.py` — `class:ParserGenerator` (lines 22-220)

```python
class ParserGenerator(object):
    """
    A ParserGenerator represents a set of production rules, that define a
    sequence of terminals and non-terminals to be replaced with a non-terminal,
    which can be turned into a parser.

    :param tokens: A list of token (non-terminal) names.
    :param precedence: A list of tuples defining the order of operation for
                       avoiding ambiguity, consisting of a string defining
                       associativity (left, right or nonassoc) and a list of
                       token names with the same associativity and level of
                       precedence.
    :param cache_id: A string specifying an ID for caching.
    """
    VERSION = 1

    def __init__(self, tokens, precedence=[], cache_id=None):
        self.tokens = tokens
        self.productions = []
        self.precedence = precedence
        if cache_id is None:
            # This ensures that we always go through the caching code.
            cache_id = "".join(random.choice(string.ascii_letters) for _ in range(6))
        self.cache_id = cache_id
        self.error_handler = None

    def production(self, rule, precedence=None):
        """
        A decorator that defines a production rule and registers the decorated
        function to be called with the terminals and non-terminals matched by
        that rule.

        A `rule` should consist of a name defining the non-terminal returned
        by the decorated function and a sequence of non-terminals and terminals
        that are supposed to be replaced::

            replacing_non_terminal : ATERMINAL non_terminal

        The name of the non-terminal replacing the sequence is on the left,
        separated from the sequence by a colon. The whitespace around the colon
        is required.

        Knowing this we can define productions::

            pg = ParserGenerator(['NUMBER', 'ADD'])

            @pg.production('number : NUMBER')
            def expr_number(p):
                return BoxInt(int(p[0].getstr()))

            @pg.production('expr : number ADD number')
            def expr_add(p):
                return BoxInt(p[0].getint() + p[2].getint())

        If a state was passed to the parser, the decorated function is
        additionally called with that state as first argument.
        """
        parts = rule.split()
        production_name = parts[0]
        if parts[1] != ":":
            raise ParserGeneratorError("Expecting :")
        syms = parts[2:]

        def inner(func):
            self.productions.append((production_name, syms, func, precedence))
            return func
        return inner

    def error(self, func):
        """
        Sets the error handler that is called with the state (if passed to the
        parser) and the token the parser errored on.

        Currently error handlers must raise an exception. If an error handler
        is not defined, a :exc:`rply.ParsingError` will be raised.
        """
        self.error_handler = func
        return func

    def compute_grammar_hash(self, g):
        hasher = hashlib.sha1()
        hasher.update(g.start.encode())
        hasher.update(json.dumps(sorted(g.terminals)).encode())
        for term, (assoc, level) in sorted(iteritems(g.precedence)):
            hasher.update(term.encode())
            hasher.update(assoc.encode())
            hasher.update(bytes(level))
        for p in g.productions:
            hasher.update(p.name.encode())
            hasher.update(json.dumps(p.prec).encode())
            hasher.update(json.dumps(p.prod).encode())
        return hasher.hexdigest()

    def serialize_table(self, table):
        return {
            "lr_action": table.lr_action,
            "lr_goto": table.lr_goto,
            "sr_conflicts": table.sr_conflicts,
            "rr_conflicts": table.rr_conflicts,
            "default_reductions": table.default_reductions,
            "start": table.grammar.start,
            "terminals": sorted(table.grammar.terminals),
            "precedence": table.grammar.precedence,
            "productions": [
                (p.name, p.prod, p.prec) for p in table.grammar.productions
            ],
        }

    def data_is_valid(self, g, data):
        if g.start != data["start"]:
            return False
        if sorted(g.terminals) != data["terminals"]:
            return False
        if sorted(g.precedence) != sorted(data["precedence"]):
            return False
        for key, (assoc, level) in iteritems(g.precedence):
            if data["precedence"][key] != [assoc, level]:
                return False
        if len(g.productions) != len(data["productions"]):
            return False
        for p, (name, prod, (assoc, level)) in zip(g.productions, data["productions"]):
            if p.name != name:
                return False
            if p.prod != prod:
                return False
            if p.prec != (assoc, level):
                return False
        return True

    def build(self):
        g = Grammar(self.tokens)

        for level, (assoc, terms) in enumerate(self.precedence, 1):
            for term in terms:
                g.set_precedence(term, assoc, level)

        for prod_name, syms, func, precedence in self.productions:
            g.add_production(prod_name, syms, func, precedence)

        g.set_start()

        for unused_term in g.unused_terminals():
            warnings.warn(
                "Token %r is unused" % unused_term,
                ParserGeneratorWarning,
                stacklevel=2
            )
        for unused_prod in g.unused_productions():
            warnings.warn(
                "Production %r is not reachable" % unused_prod,
                ParserGeneratorWarning,
                stacklevel=2
            )

        g.build_lritems()
        g.compute_first()
        g.compute_follow()

        cache_dir = AppDirs("rply").user_cache_dir
        cache_file = os.path.join(
            cache_dir,
            "rply-%s-%s-%s.json" % (
                self.VERSION, self.cache_id, self.compute_grammar_hash(g)
            )
        )

        table = None
        if os.path.exists(cache_file):
            with open(cache_file) as f:
                data = json.load(f)
            if self.data_is_valid(g, data):
                table = LRTable.from_cache(g, data)
        if table is None:
            table = LRTable.from_grammar(g)
            if not os.path.exists(cache_dir):
                os.makedirs(cache_dir)

            with open(cache_file, "w") as f:
                json.dump(self.serialize_table(table), f)

        if table.sr_conflicts:
            warnings.warn(
                "%d shift/reduce conflict%s" % (
                    len(table.sr_conflicts),
                    "s" if len(table.sr_conflicts) > 1 else ""
                ),
                ParserGeneratorWarning,
                stacklevel=2,
            )
        if table.rr_conflicts:
            warnings.warn(
                "%d reduce/reduce conflict%s" % (
                    len(table.rr_conflicts),
                    "s" if len(table.rr_conflicts) > 1 else ""
                ),
                ParserGeneratorWarning,
                stacklevel=2,
            )
        return LRParser(table, self.error_handler)
```

## `rply/parsergenerator.py` — `function:ParserGenerator.build` (lines 151-220)

```python
    def build(self):
        g = Grammar(self.tokens)

        for level, (assoc, terms) in enumerate(self.precedence, 1):
            for term in terms:
                g.set_precedence(term, assoc, level)

        for prod_name, syms, func, precedence in self.productions:
            g.add_production(prod_name, syms, func, precedence)

        g.set_start()

        for unused_term in g.unused_terminals():
            warnings.warn(
                "Token %r is unused" % unused_term,
                ParserGeneratorWarning,
                stacklevel=2
            )
        for unused_prod in g.unused_productions():
            warnings.warn(
                "Production %r is not reachable" % unused_prod,
                ParserGeneratorWarning,
                stacklevel=2
            )

        g.build_lritems()
        g.compute_first()
        g.compute_follow()

        cache_dir = AppDirs("rply").user_cache_dir
        cache_file = os.path.join(
            cache_dir,
            "rply-%s-%s-%s.json" % (
                self.VERSION, self.cache_id, self.compute_grammar_hash(g)
            )
        )

        table = None
        if os.path.exists(cache_file):
            with open(cache_file) as f:
                data = json.load(f)
            if self.data_is_valid(g, data):
                table = LRTable.from_cache(g, data)
        if table is None:
            table = LRTable.from_grammar(g)
            if not os.path.exists(cache_dir):
                os.makedirs(cache_dir)

            with open(cache_file, "w") as f:
                json.dump(self.serialize_table(table), f)

        if table.sr_conflicts:
            warnings.warn(
                "%d shift/reduce conflict%s" % (
                    len(table.sr_conflicts),
                    "s" if len(table.sr_conflicts) > 1 else ""
                ),
                ParserGeneratorWarning,
                stacklevel=2,
            )
        if table.rr_conflicts:
            warnings.warn(
                "%d reduce/reduce conflict%s" % (
                    len(table.rr_conflicts),
                    "s" if len(table.rr_conflicts) > 1 else ""
                ),
                ParserGeneratorWarning,
                stacklevel=2,
            )
        return LRParser(table, self.error_handler)
```

## `rply/parsergenerator.py` — `module:<module>@11` (lines 11-11)

```python
from appdirs import AppDirs
```

## `setup.py` — `module:<module>@7` (lines 7-17)

```python
setup(
    name="rply",
    description="A pure Python Lex/Yacc that works with RPython",
    long_description=readme,
    # duplicated in docs/conf.py
    version="0.7.3",
    author="Alex Gaynor",
    author_email="alex.gaynor@gmail.com",
    packages=["rply"],
    install_requires=["appdirs"],
)
```
