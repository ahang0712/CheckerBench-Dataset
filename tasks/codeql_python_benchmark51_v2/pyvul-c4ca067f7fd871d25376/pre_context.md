# Patch-overlapping Python context before the fix

## `nltk/tokenize/punkt.py` — `class:PunktLanguageVars` (lines 182-294)

```python
class PunktLanguageVars:
    """
    Stores variables, mostly regular expressions, which may be
    language-dependent for correct application of the algorithm.
    An extension of this class may modify its properties to suit
    a language other than English; an instance can then be passed
    as an argument to PunktSentenceTokenizer and PunktTrainer
    constructors.
    """

    __slots__ = ("_re_period_context", "_re_word_tokenizer")

    def __getstate__(self):
        # All modifications to the class are performed by inheritance.
        # Non-default parameters to be pickled must be defined in the inherited
        # class.
        return 1

    def __setstate__(self, state):
        return 1

    sent_end_chars = (".", "?", "!")
    """Characters which are candidates for sentence boundaries"""

    @property
    def _re_sent_end_chars(self):
        return "[%s]" % re.escape("".join(self.sent_end_chars))

    internal_punctuation = ",:;"  # might want to extend this..
    """sentence internal punctuation, which indicates an abbreviation if
    preceded by a period-final token."""

    re_boundary_realignment = re.compile(r'["\')\]}]+?(?:\s+|(?=--)|$)', re.MULTILINE)
    """Used to realign punctuation that should be included in a sentence
    although it follows the period (or ?, !)."""

    _re_word_start = r"[^\(\"\`{\[:;&\#\*@\)}\]\-,]"
    """Excludes some characters from starting word tokens"""

    @property
    def _re_non_word_chars(self):
        return r"(?:[)\";}\]\*:@\'\({\[%s])" % re.escape(
            "".join(set(self.sent_end_chars) - {"."})
        )

    """Characters that cannot appear within words"""

    _re_multi_char_punct = r"(?:\-{2,}|\.{2,}|(?:\.\s){2,}\.)"
    """Hyphen and ellipsis are multi-character punctuation"""

    _word_tokenize_fmt = r"""(
        %(MultiChar)s
        |
        (?=%(WordStart)s)\S+?  # Accept word characters until end is found
        (?= # Sequences marking a word's end
            \s|                                 # White-space
            $|                                  # End-of-string
            %(NonWord)s|%(MultiChar)s|          # Punctuation
            ,(?=$|\s|%(NonWord)s|%(MultiChar)s) # Comma if at end of word
        )
        |
        \S
    )"""
    """Format of a regular expression to split punctuation from words,
    excluding period."""

    def _word_tokenizer_re(self):
        """Compiles and returns a regular expression for word tokenization"""
        try:
            return self._re_word_tokenizer
        except AttributeError:
            self._re_word_tokenizer = re.compile(
                self._word_tokenize_fmt
                % {
                    "NonWord": self._re_non_word_chars,
                    "MultiChar": self._re_multi_char_punct,
                    "WordStart": self._re_word_start,
                },
                re.UNICODE | re.VERBOSE,
            )
            return self._re_word_tokenizer

    def word_tokenize(self, s):
        """Tokenize a string to split off punctuation other than periods"""
        return self._word_tokenizer_re().findall(s)

    _period_context_fmt = r"""
        \S*                          # some word material
        %(SentEndChars)s             # a potential sentence ending
        (?=(?P<after_tok>
            %(NonWord)s              # either other punctuation
            |
            \s+(?P<next_tok>\S+)     # or whitespace and some other token
        ))"""
    """Format of a regular expression to find contexts including possible
    sentence boundaries. Matches token which the possible sentence boundary
    ends, and matches the following token within a lookahead expression."""

    def period_context_re(self):
        """Compiles and returns a regular expression to find contexts
        including possible sentence boundaries."""
        try:
            return self._re_period_context
        except:
            self._re_period_context = re.compile(
                self._period_context_fmt
                % {
                    "NonWord": self._re_non_word_chars,
                    "SentEndChars": self._re_sent_end_chars,
                },
                re.UNICODE | re.VERBOSE,
            )
            return self._re_period_context
```

## `nltk/tokenize/punkt.py` — `class:PunktSentenceTokenizer` (lines 1236-1635)

```python
class PunktSentenceTokenizer(PunktBaseClass, TokenizerI):
    """
    A sentence tokenizer which uses an unsupervised algorithm to build
    a model for abbreviation words, collocations, and words that start
    sentences; and then uses that model to find sentence boundaries.
    This approach has been shown to work well for many European
    languages.
    """

    def __init__(
        self, train_text=None, verbose=False, lang_vars=None, token_cls=PunktToken
    ):
        """
        train_text can either be the sole training text for this sentence
        boundary detector, or can be a PunktParameters object.
        """
        PunktBaseClass.__init__(self, lang_vars=lang_vars, token_cls=token_cls)

        if train_text:
            self._params = self.train(train_text, verbose)

    def train(self, train_text, verbose=False):
        """
        Derives parameters from a given training text, or uses the parameters
        given. Repeated calls to this method destroy previous parameters. For
        incremental training, instantiate a separate PunktTrainer instance.
        """
        if not isinstance(train_text, str):
            return train_text
        return PunktTrainer(
            train_text, lang_vars=self._lang_vars, token_cls=self._Token
        ).get_params()

    # ////////////////////////////////////////////////////////////
    # { Tokenization
    # ////////////////////////////////////////////////////////////

    def tokenize(self, text, realign_boundaries=True):
        """
        Given a text, returns a list of the sentences in that text.
        """
        return list(self.sentences_from_text(text, realign_boundaries))

    def debug_decisions(self, text):
        """
        Classifies candidate periods as sentence breaks, yielding a dict for
        each that may be used to understand why the decision was made.

        See format_debug_decision() to help make this output readable.
        """

        for match in self._lang_vars.period_context_re().finditer(text):
            decision_text = match.group() + match.group("after_tok")
            tokens = self._tokenize_words(decision_text)
            tokens = list(self._annotate_first_pass(tokens))
            while tokens and not tokens[0].tok.endswith(self._lang_vars.sent_end_chars):
                tokens.pop(0)
            yield {
                "period_index": match.end() - 1,
                "text": decision_text,
                "type1": tokens[0].type,
                "type2": tokens[1].type,
                "type1_in_abbrs": bool(tokens[0].abbr),
                "type1_is_initial": bool(tokens[0].is_initial),
                "type2_is_sent_starter": tokens[1].type_no_sentperiod
                in self._params.sent_starters,
                "type2_ortho_heuristic": self._ortho_heuristic(tokens[1]),
                "type2_ortho_contexts": set(
                    self._params._debug_ortho_context(tokens[1].type_no_sentperiod)
                ),
                "collocation": (
                    tokens[0].type_no_sentperiod,
                    tokens[1].type_no_sentperiod,
                )
                in self._params.collocations,
                "reason": self._second_pass_annotation(tokens[0], tokens[1])
                or REASON_DEFAULT_DECISION,
                "break_decision": tokens[0].sentbreak,
            }

    def span_tokenize(self, text, realign_boundaries=True):
        """
        Given a text, generates (start, end) spans of sentences
        in the text.
        """
        slices = self._slices_from_text(text)
        if realign_boundaries:
            slices = self._realign_boundaries(text, slices)
        for sentence in slices:
            yield (sentence.start, sentence.stop)

    def sentences_from_text(self, text, realign_boundaries=True):
        """
        Given a text, generates the sentences in that text by only
        testing candidate sentence breaks. If realign_boundaries is
        True, includes in the sentence closing punctuation that
        follows the period.
        """
        return [text[s:e] for s, e in self.span_tokenize(text, realign_boundaries)]

    def _slices_from_text(self, text):
        last_break = 0
        for match in self._lang_vars.period_context_re().finditer(text):
            context = match.group() + match.group("after_tok")
            if self.text_contains_sentbreak(context):
                yield slice(last_break, match.end())
                if match.group("next_tok"):
                    # next sentence starts after whitespace
                    last_break = match.start("next_tok")
                else:
                    # next sentence starts at following punctuation
                    last_break = match.end()
        # The last sentence should not contain trailing whitespace.
        yield slice(last_break, len(text.rstrip()))

    def _realign_boundaries(self, text, slices):
        """
        Attempts to realign punctuation that falls after the period but
        should otherwise be included in the same sentence.

        For example: "(Sent1.) Sent2." will otherwise be split as::

            ["(Sent1.", ") Sent1."].

        This method will produce::

            ["(Sent1.)", "Sent2."].
        """
        realign = 0
        for sentence1, sentence2 in _pair_iter(slices):
            sentence1 = slice(sentence1.start + realign, sentence1.stop)
            if not sentence2:
                if text[sentence1]:
                    yield sentence1
                continue

            m = self._lang_vars.re_boundary_realignment.match(text[sentence2])
            if m:
                yield slice(sentence1.start, sentence2.start + len(m.group(0).rstrip()))
                realign = m.end()
            else:
                realign = 0
                if text[sentence1]:
                    yield sentence1

    def text_contains_sentbreak(self, text):
        """
        Returns True if the given text includes a sentence break.
        """
        found = False  # used to ignore last token
        for tok in self._annotate_tokens(self._tokenize_words(text)):
            if found:
                return True
            if tok.sentbreak:
                found = True
        return False

    def sentences_from_text_legacy(self, text):
        """
        Given a text, generates the sentences in that text. Annotates all
        tokens, rather than just those with possible sentence breaks. Should
        produce the same results as ``sentences_from_text``.
        """
        tokens = self._annotate_tokens(self._tokenize_words(text))
        return self._build_sentence_list(text, tokens)

    def sentences_from_tokens(self, tokens):
        """
        Given a sequence of tokens, generates lists of tokens, each list
        corresponding to a sentence.
        """
        tokens = iter(self._annotate_tokens(self._Token(t) for t in tokens))
        sentence = []
        for aug_tok in tokens:
            sentence.append(aug_tok.tok)
            if aug_tok.sentbreak:
                yield sentence
                sentence = []
        if sentence:
            yield sentence

    def _annotate_tokens(self, tokens):
        """
        Given a set of tokens augmented with markers for line-start and
        paragraph-start, returns an iterator through those tokens with full
        annotation including predicted sentence breaks.
        """
        # Make a preliminary pass through the document, marking likely
        # sentence breaks, abbreviations, and ellipsis tokens.
        tokens = self._annotate_first_pass(tokens)

        # Make a second pass through the document, using token context
        # information to change our preliminary decisions about where
        # sentence breaks, abbreviations, and ellipsis occurs.
        tokens = self._annotate_second_pass(tokens)

        ## [XX] TESTING
        # tokens = list(tokens)
        # self.dump(tokens)

        return tokens

    def _build_sentence_list(self, text, tokens):
        """
        Given the original text and the list of augmented word tokens,
        construct and return a tokenized list of sentence strings.
        """
        # Most of the work here is making sure that we put the right
        # pieces of whitespace back in all the right places.

        # Our position in the source text, used to keep track of which
        # whitespace to add:
        pos = 0

        # A regular expression that finds pieces of whitespace:
        white_space_regexp = re.compile(r"\s*")

        sentence = ""
        for aug_tok in tokens:
            tok = aug_tok.tok

            # Find the whitespace before this token, and update pos.
            white_space = white_space_regexp.match(text, pos).group()
            pos += len(white_space)

            # Some of the rules used by the punkt word tokenizer
            # strip whitespace out of the text, resulting in tokens
            # that contain whitespace in the source text.  If our
            # token doesn't match, see if adding whitespace helps.
            # If so, then use the version with whitespace.
            if text[pos : pos + len(tok)] != tok:
                pat = r"\s*".join(re.escape(c) for c in tok)
                m = re.compile(pat).match(text, pos)
                if m:
                    tok = m.group()

            # Move our position pointer to the end of the token.
            assert text[pos : pos + len(tok)] == tok
            pos += len(tok)

            # Add this token.  If it's not at the beginning of the
            # sentence, then include any whitespace that separated it
            # from the previous token.
            if sentence:
                sentence += white_space
            sentence += tok

            # If we're at a sentence break, then start a new sentence.
            if aug_tok.sentbreak:
                yield sentence
                sentence = ""

        # If the last sentence is empty, discard it.
        if sentence:
            yield sentence

    # [XX] TESTING
    def dump(self, tokens):
        print("writing to /tmp/punkt.new...")
        with open("/tmp/punkt.new", "w") as outfile:
            for aug_tok in tokens:
                if aug_tok.parastart:
                    outfile.write("\n\n")
                elif aug_tok.linestart:
                    outfile.write("\n")
                else:
                    outfile.write(" ")

                outfile.write(str(aug_tok))

    # ////////////////////////////////////////////////////////////
    # { Customization Variables
    # ////////////////////////////////////////////////////////////

    PUNCTUATION = tuple(";:,.!?")

    # ////////////////////////////////////////////////////////////
    # { Annotation Procedures
    # ////////////////////////////////////////////////////////////

    def _annotate_second_pass(self, tokens):
        """
        Performs a token-based classification (section 4) over the given
        tokens, making use of the orthographic heuristic (4.1.1), collocation
        heuristic (4.1.2) and frequent sentence starter heuristic (4.1.3).
        """
        for token1, token2 in _pair_iter(tokens):
            self._second_pass_annotation(token1, token2)
            yield token1

    def _second_pass_annotation(self, aug_tok1, aug_tok2):
        """
        Performs token-based classification over a pair of contiguous tokens
        updating the first.
        """
        # Is it the last token? We can't do anything then.
        if not aug_tok2:
            return

        if not aug_tok1.period_final:
            # We only care about words ending in periods.
            return
        typ = aug_tok1.type_no_period
        next_typ = aug_tok2.type_no_sentperiod
        tok_is_initial = aug_tok1.is_initial

        # [4.1.2. Collocation Heuristic] If there's a
        # collocation between the word before and after the
        # period, then label tok as an abbreviation and NOT
        # a sentence break. Note that collocations with
        # frequent sentence starters as their second word are
        # excluded in training.
        if (typ, next_typ) in self._params.collocations:
            aug_tok1.sentbreak = False
            aug_tok1.abbr = True
            return REASON_KNOWN_COLLOCATION

        # [4.2. Token-Based Reclassification of Abbreviations] If
        # the token is an abbreviation or an ellipsis, then decide
        # whether we should *also* classify it as a sentbreak.
        if (aug_tok1.abbr or aug_tok1.ellipsis) and (not tok_is_initial):
            # [4.1.1. Orthographic Heuristic] Check if there's
            # orthogrpahic evidence about whether the next word
            # starts a sentence or not.
            is_sent_starter = self._ortho_heuristic(aug_tok2)
            if is_sent_starter == True:
                aug_tok1.sentbreak = True
                return REASON_ABBR_WITH_ORTHOGRAPHIC_HEURISTIC

            # [4.1.3. Frequent Sentence Starter Heruistic] If the
            # next word is capitalized, and is a member of the
            # frequent-sentence-starters list, then label tok as a
            # sentence break.
            if aug_tok2.first_upper and next_typ in self._params.sent_starters:
                aug_tok1.sentbreak = True
                return REASON_ABBR_WITH_SENTENCE_STARTER

        # [4.3. Token-Based Detection of Initials and Ordinals]
        # Check if any initials or ordinals tokens that are marked
        # as sentbreaks should be reclassified as abbreviations.
        if tok_is_initial or typ == "##number##":

            # [4.1.1. Orthographic Heuristic] Check if there's
            # orthogrpahic evidence about whether the next word
            # starts a sentence or not.
            is_sent_starter = self._ortho_heuristic(aug_tok2)

            if is_sent_starter == False:
                aug_tok1.sentbreak = False
                aug_tok1.abbr = True
                if tok_is_initial:
                    return REASON_INITIAL_WITH_ORTHOGRAPHIC_HEURISTIC
                return REASON_NUMBER_WITH_ORTHOGRAPHIC_HEURISTIC

            # Special heuristic for initials: if orthogrpahic
            # heuristic is unknown, and next word is always
            # capitalized, then mark as abbrev (eg: J. Bach).
            if (
                is_sent_starter == "unknown"
                and tok_is_initial
                and aug_tok2.first_upper
                and not (self._params.ortho_context[next_typ] & _ORTHO_LC)
            ):
                aug_tok1.sentbreak = False
                aug_tok1.abbr = True
                return REASON_INITIAL_WITH_SPECIAL_ORTHOGRAPHIC_HEURISTIC

        return

    def _ortho_heuristic(self, aug_tok):
        """
        Decide whether the given token is the first token in a sentence.
        """
        # Sentences don't start with punctuation marks:
        if aug_tok.tok in self.PUNCTUATION:
            return False

        ortho_context = self._params.ortho_context[aug_tok.type_no_sentperiod]

        # If the word is capitalized, occurs at least once with a
        # lower case first letter, and never occurs with an upper case
        # first letter sentence-internally, then it's a sentence starter.
        if (
            aug_tok.first_upper
            and (ortho_context & _ORTHO_LC)
            and not (ortho_context & _ORTHO_MID_UC)
        ):
            return True

        # If the word is lower case, and either (a) we've seen it used
        # with upper case, or (b) we've never seen it used
        # sentence-initially with lower case, then it's not a sentence
        # starter.
        if aug_tok.first_lower and (
            (ortho_context & _ORTHO_UC) or not (ortho_context & _ORTHO_BEG_LC)
        ):
            return False

        # Otherwise, we're not sure.
        return "unknown"
```

## `nltk/tokenize/punkt.py` — `function:PunktSentenceTokenizer.debug_decisions` (lines 1279-1314)

```python
    def debug_decisions(self, text):
        """
        Classifies candidate periods as sentence breaks, yielding a dict for
        each that may be used to understand why the decision was made.

        See format_debug_decision() to help make this output readable.
        """

        for match in self._lang_vars.period_context_re().finditer(text):
            decision_text = match.group() + match.group("after_tok")
            tokens = self._tokenize_words(decision_text)
            tokens = list(self._annotate_first_pass(tokens))
            while tokens and not tokens[0].tok.endswith(self._lang_vars.sent_end_chars):
                tokens.pop(0)
            yield {
                "period_index": match.end() - 1,
                "text": decision_text,
                "type1": tokens[0].type,
                "type2": tokens[1].type,
                "type1_in_abbrs": bool(tokens[0].abbr),
                "type1_is_initial": bool(tokens[0].is_initial),
                "type2_is_sent_starter": tokens[1].type_no_sentperiod
                in self._params.sent_starters,
                "type2_ortho_heuristic": self._ortho_heuristic(tokens[1]),
                "type2_ortho_contexts": set(
                    self._params._debug_ortho_context(tokens[1].type_no_sentperiod)
                ),
                "collocation": (
                    tokens[0].type_no_sentperiod,
                    tokens[1].type_no_sentperiod,
                )
                in self._params.collocations,
                "reason": self._second_pass_annotation(tokens[0], tokens[1])
                or REASON_DEFAULT_DECISION,
                "break_decision": tokens[0].sentbreak,
            }
```

## `nltk/tokenize/punkt.py` — `function:PunktSentenceTokenizer._slices_from_text` (lines 1336-1349)

```python
    def _slices_from_text(self, text):
        last_break = 0
        for match in self._lang_vars.period_context_re().finditer(text):
            context = match.group() + match.group("after_tok")
            if self.text_contains_sentbreak(context):
                yield slice(last_break, match.end())
                if match.group("next_tok"):
                    # next sentence starts after whitespace
                    last_break = match.start("next_tok")
                else:
                    # next sentence starts at following punctuation
                    last_break = match.end()
        # The last sentence should not contain trailing whitespace.
        yield slice(last_break, len(text.rstrip()))
```
