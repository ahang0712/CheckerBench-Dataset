# Patch-overlapping Python context after the fix

## `paramiko/pkey.py` — `class:PKey` (lines 62-663)

```python
class PKey(object):
    """
    Base class for public keys.
    """

    # known encryption types for private key files:
    _CIPHER_TABLE = {
        "AES-128-CBC": {
            "cipher": algorithms.AES,
            "keysize": 16,
            "blocksize": 16,
            "mode": modes.CBC,
        },
        "AES-256-CBC": {
            "cipher": algorithms.AES,
            "keysize": 32,
            "blocksize": 16,
            "mode": modes.CBC,
        },
        "DES-EDE3-CBC": {
            "cipher": algorithms.TripleDES,
            "keysize": 24,
            "blocksize": 8,
            "mode": modes.CBC,
        },
    }
    _PRIVATE_KEY_FORMAT_ORIGINAL = 1
    _PRIVATE_KEY_FORMAT_OPENSSH = 2
    BEGIN_TAG = re.compile(
        r"^-{5}BEGIN (RSA|DSA|EC|OPENSSH) PRIVATE KEY-{5}\s*$"
    )
    END_TAG = re.compile(r"^-{5}END (RSA|DSA|EC|OPENSSH) PRIVATE KEY-{5}\s*$")

    def __init__(self, msg=None, data=None):
        """
        Create a new instance of this public key type.  If ``msg`` is given,
        the key's public part(s) will be filled in from the message.  If
        ``data`` is given, the key's public part(s) will be filled in from
        the string.

        :param .Message msg:
            an optional SSH `.Message` containing a public key of this type.
        :param str data: an optional string containing a public key
            of this type

        :raises: `.SSHException` --
            if a key cannot be created from the ``data`` or ``msg`` given, or
            no key was passed in.
        """
        pass

    def asbytes(self):
        """
        Return a string of an SSH `.Message` made up of the public part(s) of
        this key.  This string is suitable for passing to `__init__` to
        re-create the key object later.
        """
        return bytes()

    def __str__(self):
        return self.asbytes()

    # noinspection PyUnresolvedReferences
    # TODO: The comparison functions should be removed as per:
    # https://docs.python.org/3.0/whatsnew/3.0.html#ordering-comparisons
    def __cmp__(self, other):
        """
        Compare this key to another.  Returns 0 if this key is equivalent to
        the given key, or non-0 if they are different.  Only the public parts
        of the key are compared, so a public key will compare equal to its
        corresponding private key.

        :param .PKey other: key to compare to.
        """
        hs = hash(self)
        ho = hash(other)
        if hs != ho:
            return cmp(hs, ho)  # noqa
        return cmp(self.asbytes(), other.asbytes())  # noqa

    def __eq__(self, other):
        return self._fields == other._fields

    def __hash__(self):
        return hash(self._fields)

    @property
    def _fields(self):
        raise NotImplementedError

    def get_name(self):
        """
        Return the name of this private key implementation.

        :return:
            name of this private key type, in SSH terminology, as a `str` (for
            example, ``"ssh-rsa"``).
        """
        return ""

    def get_bits(self):
        """
        Return the number of significant bits in this key.  This is useful
        for judging the relative security of a key.

        :return: bits in the key (as an `int`)
        """
        return 0

    def can_sign(self):
        """
        Return ``True`` if this key has the private part necessary for signing
        data.
        """
        return False

    def get_fingerprint(self):
        """
        Return an MD5 fingerprint of the public part of this key.  Nothing
        secret is revealed.

        :return:
            a 16-byte `string <str>` (binary) of the MD5 fingerprint, in SSH
            format.
        """
        return md5(self.asbytes()).digest()

    def get_base64(self):
        """
        Return a base64 string containing the public part of this key.  Nothing
        secret is revealed.  This format is compatible with that used to store
        public key files or recognized host keys.

        :return: a base64 `string <str>` containing the public part of the key.
        """
        return u(encodebytes(self.asbytes())).replace("\n", "")

    def sign_ssh_data(self, data, algorithm=None):
        """
        Sign a blob of data with this private key, and return a `.Message`
        representing an SSH signature message.

        :param str data:
            the data to sign.
        :param str algorithm:
            the signature algorithm to use, if different from the key's
            internal name. Default: ``None``.
        :return: an SSH signature `message <.Message>`.

        .. versionchanged:: 2.9
            Added the ``algorithm`` kwarg.
        """
        return bytes()

    def verify_ssh_sig(self, data, msg):
        """
        Given a blob of data, and an SSH message representing a signature of
        that data, verify that it was signed with this key.

        :param str data: the data that was signed.
        :param .Message msg: an SSH signature message
        :return:
            ``True`` if the signature verifies correctly; ``False`` otherwise.
        """
        return False

    @classmethod
    def from_private_key_file(cls, filename, password=None):
        """
        Create a key object by reading a private key file.  If the private
        key is encrypted and ``password`` is not ``None``, the given password
        will be used to decrypt the key (otherwise `.PasswordRequiredException`
        is thrown).  Through the magic of Python, this factory method will
        exist in all subclasses of PKey (such as `.RSAKey` or `.DSSKey`), but
        is useless on the abstract PKey class.

        :param str filename: name of the file to read
        :param str password:
            an optional password to use to decrypt the key file, if it's
            encrypted
        :return: a new `.PKey` based on the given private key

        :raises: ``IOError`` -- if there was an error reading the file
        :raises: `.PasswordRequiredException` -- if the private key file is
            encrypted, and ``password`` is ``None``
        :raises: `.SSHException` -- if the key file is invalid
        """
        key = cls(filename=filename, password=password)
        return key

    @classmethod
    def from_private_key(cls, file_obj, password=None):
        """
        Create a key object by reading a private key from a file (or file-like)
        object.  If the private key is encrypted and ``password`` is not
        ``None``, the given password will be used to decrypt the key (otherwise
        `.PasswordRequiredException` is thrown).

        :param file_obj: the file-like object to read from
        :param str password:
            an optional password to use to decrypt the key, if it's encrypted
        :return: a new `.PKey` based on the given private key

        :raises: ``IOError`` -- if there was an error reading the key
        :raises: `.PasswordRequiredException` --
            if the private key file is encrypted, and ``password`` is ``None``
        :raises: `.SSHException` -- if the key file is invalid
        """
        key = cls(file_obj=file_obj, password=password)
        return key

    def write_private_key_file(self, filename, password=None):
        """
        Write private key contents into a file.  If the password is not
        ``None``, the key is encrypted before writing.

        :param str filename: name of the file to write
        :param str password:
            an optional password to use to encrypt the key file

        :raises: ``IOError`` -- if there was an error writing the file
        :raises: `.SSHException` -- if the key is invalid
        """
        raise Exception("Not implemented in PKey")

    def write_private_key(self, file_obj, password=None):
        """
        Write private key contents into a file (or file-like) object.  If the
        password is not ``None``, the key is encrypted before writing.

        :param file_obj: the file-like object to write into
        :param str password: an optional password to use to encrypt the key

        :raises: ``IOError`` -- if there was an error writing to the file
        :raises: `.SSHException` -- if the key is invalid
        """
        raise Exception("Not implemented in PKey")

    def _read_private_key_file(self, tag, filename, password=None):
        """
        Read an SSH2-format private key file, looking for a string of the type
        ``"BEGIN xxx PRIVATE KEY"`` for some ``xxx``, base64-decode the text we
        find, and return it as a string.  If the private key is encrypted and
        ``password`` is not ``None``, the given password will be used to
        decrypt the key (otherwise `.PasswordRequiredException` is thrown).

        :param str tag: ``"RSA"`` or ``"DSA"``, the tag used to mark the
            data block.
        :param str filename: name of the file to read.
        :param str password:
            an optional password to use to decrypt the key file, if it's
            encrypted.
        :return: data blob (`str`) that makes up the private key.

        :raises: ``IOError`` -- if there was an error reading the file.
        :raises: `.PasswordRequiredException` -- if the private key file is
            encrypted, and ``password`` is ``None``.
        :raises: `.SSHException` -- if the key file is invalid.
        """
        with open(filename, "r") as f:
            data = self._read_private_key(tag, f, password)
        return data

    def _read_private_key(self, tag, f, password=None):
        lines = f.readlines()

        # find the BEGIN tag
        start = 0
        m = self.BEGIN_TAG.match(lines[start])
        line_range = len(lines) - 1
        while start < line_range and not m:
            start += 1
            m = self.BEGIN_TAG.match(lines[start])
        start += 1
        keytype = m.group(1) if m else None
        if start >= len(lines) or keytype is None:
            raise SSHException("not a valid {} private key file".format(tag))

        # find the END tag
        end = start
        m = self.END_TAG.match(lines[end])
        while end < line_range and not m:
            end += 1
            m = self.END_TAG.match(lines[end])

        if keytype == tag:
            data = self._read_private_key_pem(lines, end, password)
            pkformat = self._PRIVATE_KEY_FORMAT_ORIGINAL
        elif keytype == "OPENSSH":
            data = self._read_private_key_openssh(lines[start:end], password)
            pkformat = self._PRIVATE_KEY_FORMAT_OPENSSH
        else:
            raise SSHException(
                "encountered {} key, expected {} key".format(keytype, tag)
            )

        return pkformat, data

    def _got_bad_key_format_id(self, id_):
        err = "{}._read_private_key() spat out an unknown key format id '{}'"
        raise SSHException(err.format(self.__class__.__name__, id_))

    def _read_private_key_pem(self, lines, end, password):
        start = 0
        # parse any headers first
        headers = {}
        start += 1
        while start < len(lines):
            line = lines[start].split(": ")
            if len(line) == 1:
                break
            headers[line[0].lower()] = line[1].strip()
            start += 1
        # if we trudged to the end of the file, just try to cope.
        try:
            data = decodebytes(b("".join(lines[start:end])))
        except base64.binascii.Error as e:
            raise SSHException("base64 decoding error: {}".format(e))
        if "proc-type" not in headers:
            # unencryped: done
            return data
        # encrypted keyfile: will need a password
        proc_type = headers["proc-type"]
        if proc_type != "4,ENCRYPTED":
            raise SSHException(
                'Unknown private key structure "{}"'.format(proc_type)
            )
        try:
            encryption_type, saltstr = headers["dek-info"].split(",")
        except:
            raise SSHException("Can't parse DEK-info in private key file")
        if encryption_type not in self._CIPHER_TABLE:
            raise SSHException(
                'Unknown private key cipher "{}"'.format(encryption_type)
            )
        # if no password was passed in,
        # raise an exception pointing out that we need one
        if password is None:
            raise PasswordRequiredException("Private key file is encrypted")
        cipher = self._CIPHER_TABLE[encryption_type]["cipher"]
        keysize = self._CIPHER_TABLE[encryption_type]["keysize"]
        mode = self._CIPHER_TABLE[encryption_type]["mode"]
        salt = unhexlify(b(saltstr))
        key = util.generate_key_bytes(md5, salt, password, keysize)
        decryptor = Cipher(
            cipher(key), mode(salt), backend=default_backend()
        ).decryptor()
        return decryptor.update(data) + decryptor.finalize()

    def _read_private_key_openssh(self, lines, password):
        """
        Read the new OpenSSH SSH2 private key format available
        since OpenSSH version 6.5
        Reference:
        https://github.com/openssh/openssh-portable/blob/master/PROTOCOL.key
        """
        try:
            data = decodebytes(b("".join(lines)))
        except base64.binascii.Error as e:
            raise SSHException("base64 decoding error: {}".format(e))

        # read data struct
        auth_magic = data[:15]
        if auth_magic != OPENSSH_AUTH_MAGIC:
            raise SSHException("unexpected OpenSSH key header encountered")

        cstruct = self._uint32_cstruct_unpack(data[15:], "sssur")
        cipher, kdfname, kdf_options, num_pubkeys, remainder = cstruct
        # For now, just support 1 key.
        if num_pubkeys > 1:
            raise SSHException(
                "unsupported: private keyfile has multiple keys"
            )
        pubkey, privkey_blob = self._uint32_cstruct_unpack(remainder, "ss")

        if kdfname == b("bcrypt"):
            if cipher == b("aes256-cbc"):
                mode = modes.CBC
            elif cipher == b("aes256-ctr"):
                mode = modes.CTR
            else:
                raise SSHException(
                    "unknown cipher `{}` used in private key file".format(
                        cipher.decode("utf-8")
                    )
                )
            # Encrypted private key.
            # If no password was passed in, raise an exception pointing
            # out that we need one
            if password is None:
                raise PasswordRequiredException(
                    "private key file is encrypted"
                )

            # Unpack salt and rounds from kdfoptions
            salt, rounds = self._uint32_cstruct_unpack(kdf_options, "su")

            # run bcrypt kdf to derive key and iv/nonce (32 + 16 bytes)
            key_iv = bcrypt.kdf(
                b(password),
                b(salt),
                48,
                rounds,
                # We can't control how many rounds are on disk, so no sense
                # warning about it.
                ignore_few_rounds=True,
            )
            key = key_iv[:32]
            iv = key_iv[32:]

            # decrypt private key blob
            decryptor = Cipher(
                algorithms.AES(key), mode(iv), default_backend()
            ).decryptor()
            decrypted_privkey = decryptor.update(privkey_blob)
            decrypted_privkey += decryptor.finalize()
        elif cipher == b("none") and kdfname == b("none"):
            # Unencrypted private key
            decrypted_privkey = privkey_blob
        else:
            raise SSHException(
                "unknown cipher or kdf used in private key file"
            )

        # Unpack private key and verify checkints
        cstruct = self._uint32_cstruct_unpack(decrypted_privkey, "uusr")
        checkint1, checkint2, keytype, keydata = cstruct

        if checkint1 != checkint2:
            raise SSHException(
                "OpenSSH private key file checkints do not match"
            )

        return _unpad_openssh(keydata)

    def _uint32_cstruct_unpack(self, data, strformat):
        """
        Used to read new OpenSSH private key format.
        Unpacks a c data structure containing a mix of 32-bit uints and
        variable length strings prefixed by 32-bit uint size field,
        according to the specified format. Returns the unpacked vars
        in a tuple.
        Format strings:
          s - denotes a string
          i - denotes a long integer, encoded as a byte string
          u - denotes a 32-bit unsigned integer
          r - the remainder of the input string, returned as a string
        """
        arr = []
        idx = 0
        try:
            for f in strformat:
                if f == "s":
                    # string
                    s_size = struct.unpack(">L", data[idx : idx + 4])[0]
                    idx += 4
                    s = data[idx : idx + s_size]
                    idx += s_size
                    arr.append(s)
                if f == "i":
                    # long integer
                    s_size = struct.unpack(">L", data[idx : idx + 4])[0]
                    idx += 4
                    s = data[idx : idx + s_size]
                    idx += s_size
                    i = util.inflate_long(s, True)
                    arr.append(i)
                elif f == "u":
                    # 32-bit unsigned int
                    u = struct.unpack(">L", data[idx : idx + 4])[0]
                    idx += 4
                    arr.append(u)
                elif f == "r":
                    # remainder as string
                    s = data[idx:]
                    arr.append(s)
                    break
        except Exception as e:
            # PKey-consuming code frequently wants to save-and-skip-over issues
            # with loading keys, and uses SSHException as the (really friggin
            # awful) signal for this. So for now...we do this.
            raise SSHException(str(e))
        return tuple(arr)

    def _write_private_key_file(self, filename, key, format, password=None):
        """
        Write an SSH2-format private key file in a form that can be read by
        paramiko or openssh.  If no password is given, the key is written in
        a trivially-encoded format (base64) which is completely insecure.  If
        a password is given, DES-EDE3-CBC is used.

        :param str tag:
            ``"RSA"`` or ``"DSA"``, the tag used to mark the data block.
        :param filename: name of the file to write.
        :param str data: data blob that makes up the private key.
        :param str password: an optional password to use to encrypt the file.

        :raises: ``IOError`` -- if there was an error writing the file.
        """
        # Ensure that we create new key files directly with a user-only mode,
        # instead of opening, writing, then chmodding, which leaves us open to
        # CVE-2022-24302.
        # NOTE: O_TRUNC is a noop on new files, and O_CREAT is a noop on
        # existing files, so using all 3 in both cases is fine. Ditto the use
        # of the 'mode' argument; it should be safe to give even for existing
        # files (though it will not act like a chmod in that case).
        kwargs = dict(flags=os.O_WRONLY | os.O_TRUNC | os.O_CREAT, mode=o600)
        # NOTE: yea, you still gotta inform the FLO that it is in "write" mode
        with os.fdopen(os.open(filename, **kwargs), mode="w") as f:
            # TODO 3.0: remove the now redundant chmod
            os.chmod(filename, o600)
            self._write_private_key(f, key, format, password=password)

    def _write_private_key(self, f, key, format, password=None):
        if password is None:
            encryption = serialization.NoEncryption()
        else:
            encryption = serialization.BestAvailableEncryption(b(password))

        f.write(
            key.private_bytes(
                serialization.Encoding.PEM, format, encryption
            ).decode()
        )

    def _check_type_and_load_cert(self, msg, key_type, cert_type):
        """
        Perform message type-checking & optional certificate loading.

        This includes fast-forwarding cert ``msg`` objects past the nonce, so
        that the subsequent fields are the key numbers; thus the caller may
        expect to treat the message as key material afterwards either way.

        The obtained key type is returned for classes which need to know what
        it was (e.g. ECDSA.)
        """
        # Normalization; most classes have a single key type and give a string,
        # but eg ECDSA is a 1:N mapping.
        key_types = key_type
        cert_types = cert_type
        if isinstance(key_type, string_types):
            key_types = [key_types]
        if isinstance(cert_types, string_types):
            cert_types = [cert_types]
        # Can't do much with no message, that should've been handled elsewhere
        if msg is None:
            raise SSHException("Key object may not be empty")
        # First field is always key type, in either kind of object. (make sure
        # we rewind before grabbing it - sometimes caller had to do their own
        # introspection first!)
        msg.rewind()
        type_ = msg.get_text()
        # Regular public key - nothing special to do besides the implicit
        # type check.
        if type_ in key_types:
            pass
        # OpenSSH-compatible certificate - store full copy as .public_blob
        # (so signing works correctly) and then fast-forward past the
        # nonce.
        elif type_ in cert_types:
            # This seems the cleanest way to 'clone' an already-being-read
            # message; they're *IO objects at heart and their .getvalue()
            # always returns the full value regardless of pointer position.
            self.load_certificate(Message(msg.asbytes()))
            # Read out nonce as it comes before the public numbers.
            # TODO: usefully interpret it & other non-public-number fields
            # (requires going back into per-type subclasses.)
            msg.get_string()
        else:
            err = "Invalid key (class: {}, data type: {}"
            raise SSHException(err.format(self.__class__.__name__, type_))

    def load_certificate(self, value):
        """
        Supplement the private key contents with data loaded from an OpenSSH
        public key (``.pub``) or certificate (``-cert.pub``) file, a string
        containing such a file, or a `.Message` object.

        The .pub contents adds no real value, since the private key
        file includes sufficient information to derive the public
        key info. For certificates, however, this can be used on
        the client side to offer authentication requests to the server
        based on certificate instead of raw public key.

        See:
        https://github.com/openssh/openssh-portable/blob/master/PROTOCOL.certkeys

        Note: very little effort is made to validate the certificate contents,
        that is for the server to decide if it is good enough to authenticate
        successfully.
        """
        if isinstance(value, Message):
            constructor = "from_message"
        elif os.path.isfile(value):
            constructor = "from_file"
        else:
            constructor = "from_string"
        blob = getattr(PublicBlob, constructor)(value)
        if not blob.key_type.startswith(self.get_name()):
            err = "PublicBlob type {} incompatible with key type {}"
            raise ValueError(err.format(blob.key_type, self.get_name()))
        self.public_blob = blob
```

## `paramiko/pkey.py` — `function:PKey._write_private_key_file` (lines 546-573)

```python
    def _write_private_key_file(self, filename, key, format, password=None):
        """
        Write an SSH2-format private key file in a form that can be read by
        paramiko or openssh.  If no password is given, the key is written in
        a trivially-encoded format (base64) which is completely insecure.  If
        a password is given, DES-EDE3-CBC is used.

        :param str tag:
            ``"RSA"`` or ``"DSA"``, the tag used to mark the data block.
        :param filename: name of the file to write.
        :param str data: data blob that makes up the private key.
        :param str password: an optional password to use to encrypt the file.

        :raises: ``IOError`` -- if there was an error writing the file.
        """
        # Ensure that we create new key files directly with a user-only mode,
        # instead of opening, writing, then chmodding, which leaves us open to
        # CVE-2022-24302.
        # NOTE: O_TRUNC is a noop on new files, and O_CREAT is a noop on
        # existing files, so using all 3 in both cases is fine. Ditto the use
        # of the 'mode' argument; it should be safe to give even for existing
        # files (though it will not act like a chmod in that case).
        kwargs = dict(flags=os.O_WRONLY | os.O_TRUNC | os.O_CREAT, mode=o600)
        # NOTE: yea, you still gotta inform the FLO that it is in "write" mode
        with os.fdopen(os.open(filename, **kwargs), mode="w") as f:
            # TODO 3.0: remove the now redundant chmod
            os.chmod(filename, o600)
            self._write_private_key(f, key, format, password=password)
```

## `tests/test_pkey.py` — `class:KeyTest` (lines 140-754)

```python
class KeyTest(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    def assert_keyfile_is_encrypted(self, keyfile):
        """
        A quick check that filename looks like an encrypted key.
        """
        with open(keyfile, "r") as fh:
            self.assertEqual(
                fh.readline()[:-1], "-----BEGIN RSA PRIVATE KEY-----"
            )
            self.assertEqual(fh.readline()[:-1], "Proc-Type: 4,ENCRYPTED")
            self.assertEqual(fh.readline()[0:10], "DEK-Info: ")

    def test_generate_key_bytes(self):
        key = util.generate_key_bytes(md5, x1234, "happy birthday", 30)
        exp = b"\x61\xE1\xF2\x72\xF4\xC1\xC4\x56\x15\x86\xBD\x32\x24\x98\xC0\xE9\x24\x67\x27\x80\xF4\x7B\xB3\x7D\xDA\x7D\x54\x01\x9E\x64"  # noqa
        self.assertEqual(exp, key)

    def test_load_rsa(self):
        key = RSAKey.from_private_key_file(_support("test_rsa.key"))
        self.assertEqual("ssh-rsa", key.get_name())
        exp_rsa = b(FINGER_RSA.split()[1].replace(":", ""))
        my_rsa = hexlify(key.get_fingerprint())
        self.assertEqual(exp_rsa, my_rsa)
        self.assertEqual(PUB_RSA.split()[1], key.get_base64())
        self.assertEqual(1024, key.get_bits())

        s = StringIO()
        key.write_private_key(s)
        self.assertEqual(RSA_PRIVATE_OUT, s.getvalue())
        s.seek(0)
        key2 = RSAKey.from_private_key(s)
        self.assertEqual(key, key2)

    def test_load_rsa_transmutes_crypto_exceptions(self):
        # TODO: nix unittest for pytest
        for exception in (TypeError("onoz"), UnsupportedAlgorithm("oops")):
            with patch(
                "paramiko.rsakey.serialization.load_der_private_key"
            ) as loader:
                loader.side_effect = exception
                with pytest.raises(SSHException, match=str(exception)):
                    RSAKey.from_private_key_file(_support("test_rsa.key"))

    def test_load_rsa_password(self):
        key = RSAKey.from_private_key_file(
            _support("test_rsa_password.key"), "television"
        )
        self.assertEqual("ssh-rsa", key.get_name())
        exp_rsa = b(FINGER_RSA.split()[1].replace(":", ""))
        my_rsa = hexlify(key.get_fingerprint())
        self.assertEqual(exp_rsa, my_rsa)
        self.assertEqual(PUB_RSA.split()[1], key.get_base64())
        self.assertEqual(1024, key.get_bits())

    def test_load_dss(self):
        key = DSSKey.from_private_key_file(_support("test_dss.key"))
        self.assertEqual("ssh-dss", key.get_name())
        exp_dss = b(FINGER_DSS.split()[1].replace(":", ""))
        my_dss = hexlify(key.get_fingerprint())
        self.assertEqual(exp_dss, my_dss)
        self.assertEqual(PUB_DSS.split()[1], key.get_base64())
        self.assertEqual(1024, key.get_bits())

        s = StringIO()
        key.write_private_key(s)
        self.assertEqual(DSS_PRIVATE_OUT, s.getvalue())
        s.seek(0)
        key2 = DSSKey.from_private_key(s)
        self.assertEqual(key, key2)

    def test_load_dss_password(self):
        key = DSSKey.from_private_key_file(
            _support("test_dss_password.key"), "television"
        )
        self.assertEqual("ssh-dss", key.get_name())
        exp_dss = b(FINGER_DSS.split()[1].replace(":", ""))
        my_dss = hexlify(key.get_fingerprint())
        self.assertEqual(exp_dss, my_dss)
        self.assertEqual(PUB_DSS.split()[1], key.get_base64())
        self.assertEqual(1024, key.get_bits())

    def test_compare_rsa(self):
        # verify that the private & public keys compare equal
        key = RSAKey.from_private_key_file(_support("test_rsa.key"))
        self.assertEqual(key, key)
        pub = RSAKey(data=key.asbytes())
        self.assertTrue(key.can_sign())
        self.assertTrue(not pub.can_sign())
        self.assertEqual(key, pub)

    def test_compare_dss(self):
        # verify that the private & public keys compare equal
        key = DSSKey.from_private_key_file(_support("test_dss.key"))
        self.assertEqual(key, key)
        pub = DSSKey(data=key.asbytes())
        self.assertTrue(key.can_sign())
        self.assertTrue(not pub.can_sign())
        self.assertEqual(key, pub)

    def _sign_and_verify_rsa(self, algorithm, saved_sig):
        key = RSAKey.from_private_key_file(_support("test_rsa.key"))
        msg = key.sign_ssh_data(b"ice weasels", algorithm)
        assert isinstance(msg, Message)
        msg.rewind()
        assert msg.get_text() == algorithm
        expected = bytes().join(
            [byte_chr(int(x, 16)) for x in saved_sig.split(":")]
        )
        assert msg.get_binary() == expected
        msg.rewind()
        pub = RSAKey(data=key.asbytes())
        self.assertTrue(pub.verify_ssh_sig(b"ice weasels", msg))

    def test_sign_and_verify_ssh_rsa(self):
        self._sign_and_verify_rsa("ssh-rsa", SIGNED_RSA)

    def test_sign_and_verify_rsa_sha2_512(self):
        self._sign_and_verify_rsa("rsa-sha2-512", SIGNED_RSA_512)

    def test_sign_and_verify_rsa_sha2_256(self):
        self._sign_and_verify_rsa("rsa-sha2-256", SIGNED_RSA_256)

    def test_sign_dss(self):
        # verify that the dss private key can sign and verify
        key = DSSKey.from_private_key_file(_support("test_dss.key"))
        msg = key.sign_ssh_data(b"ice weasels")
        self.assertTrue(type(msg) is Message)
        msg.rewind()
        self.assertEqual("ssh-dss", msg.get_text())
        # can't do the same test as we do for RSA, because DSS signatures
        # are usually different each time.  but we can test verification
        # anyway so it's ok.
        self.assertEqual(40, len(msg.get_binary()))
        msg.rewind()
        pub = DSSKey(data=key.asbytes())
        self.assertTrue(pub.verify_ssh_sig(b"ice weasels", msg))

    def test_generate_rsa(self):
        key = RSAKey.generate(1024)
        msg = key.sign_ssh_data(b"jerri blank")
        msg.rewind()
        self.assertTrue(key.verify_ssh_sig(b"jerri blank", msg))

    def test_generate_dss(self):
        key = DSSKey.generate(1024)
        msg = key.sign_ssh_data(b"jerri blank")
        msg.rewind()
        self.assertTrue(key.verify_ssh_sig(b"jerri blank", msg))

    def test_generate_ecdsa(self):
        key = ECDSAKey.generate()
        msg = key.sign_ssh_data(b"jerri blank")
        msg.rewind()
        self.assertTrue(key.verify_ssh_sig(b"jerri blank", msg))
        self.assertEqual(key.get_bits(), 256)
        self.assertEqual(key.get_name(), "ecdsa-sha2-nistp256")

        key = ECDSAKey.generate(bits=256)
        msg = key.sign_ssh_data(b"jerri blank")
        msg.rewind()
        self.assertTrue(key.verify_ssh_sig(b"jerri blank", msg))
        self.assertEqual(key.get_bits(), 256)
        self.assertEqual(key.get_name(), "ecdsa-sha2-nistp256")

        key = ECDSAKey.generate(bits=384)
        msg = key.sign_ssh_data(b"jerri blank")
        msg.rewind()
        self.assertTrue(key.verify_ssh_sig(b"jerri blank", msg))
        self.assertEqual(key.get_bits(), 384)
        self.assertEqual(key.get_name(), "ecdsa-sha2-nistp384")

        key = ECDSAKey.generate(bits=521)
        msg = key.sign_ssh_data(b"jerri blank")
        msg.rewind()
        self.assertTrue(key.verify_ssh_sig(b"jerri blank", msg))
        self.assertEqual(key.get_bits(), 521)
        self.assertEqual(key.get_name(), "ecdsa-sha2-nistp521")

    def test_load_ecdsa_256(self):
        key = ECDSAKey.from_private_key_file(_support("test_ecdsa_256.key"))
        self.assertEqual("ecdsa-sha2-nistp256", key.get_name())
        exp_ecdsa = b(FINGER_ECDSA_256.split()[1].replace(":", ""))
        my_ecdsa = hexlify(key.get_fingerprint())
        self.assertEqual(exp_ecdsa, my_ecdsa)
        self.assertEqual(PUB_ECDSA_256.split()[1], key.get_base64())
        self.assertEqual(256, key.get_bits())

        s = StringIO()
        key.write_private_key(s)
        self.assertEqual(ECDSA_PRIVATE_OUT_256, s.getvalue())
        s.seek(0)
        key2 = ECDSAKey.from_private_key(s)
        self.assertEqual(key, key2)

    def test_load_ecdsa_password_256(self):
        key = ECDSAKey.from_private_key_file(
            _support("test_ecdsa_password_256.key"), b"television"
        )
        self.assertEqual("ecdsa-sha2-nistp256", key.get_name())
        exp_ecdsa = b(FINGER_ECDSA_256.split()[1].replace(":", ""))
        my_ecdsa = hexlify(key.get_fingerprint())
        self.assertEqual(exp_ecdsa, my_ecdsa)
        self.assertEqual(PUB_ECDSA_256.split()[1], key.get_base64())
        self.assertEqual(256, key.get_bits())

    def test_compare_ecdsa_256(self):
        # verify that the private & public keys compare equal
        key = ECDSAKey.from_private_key_file(_support("test_ecdsa_256.key"))
        self.assertEqual(key, key)
        pub = ECDSAKey(data=key.asbytes())
        self.assertTrue(key.can_sign())
        self.assertTrue(not pub.can_sign())
        self.assertEqual(key, pub)

    def test_sign_ecdsa_256(self):
        # verify that the rsa private key can sign and verify
        key = ECDSAKey.from_private_key_file(_support("test_ecdsa_256.key"))
        msg = key.sign_ssh_data(b"ice weasels")
        self.assertTrue(type(msg) is Message)
        msg.rewind()
        self.assertEqual("ecdsa-sha2-nistp256", msg.get_text())
        # ECDSA signatures, like DSS signatures, tend to be different
        # each time, so we can't compare against a "known correct"
        # signature.
        # Even the length of the signature can change.

        msg.rewind()
        pub = ECDSAKey(data=key.asbytes())
        self.assertTrue(pub.verify_ssh_sig(b"ice weasels", msg))

    def test_load_ecdsa_384(self):
        key = ECDSAKey.from_private_key_file(_support("test_ecdsa_384.key"))
        self.assertEqual("ecdsa-sha2-nistp384", key.get_name())
        exp_ecdsa = b(FINGER_ECDSA_384.split()[1].replace(":", ""))
        my_ecdsa = hexlify(key.get_fingerprint())
        self.assertEqual(exp_ecdsa, my_ecdsa)
        self.assertEqual(PUB_ECDSA_384.split()[1], key.get_base64())
        self.assertEqual(384, key.get_bits())

        s = StringIO()
        key.write_private_key(s)
        self.assertEqual(ECDSA_PRIVATE_OUT_384, s.getvalue())
        s.seek(0)
        key2 = ECDSAKey.from_private_key(s)
        self.assertEqual(key, key2)

    def test_load_ecdsa_password_384(self):
        key = ECDSAKey.from_private_key_file(
            _support("test_ecdsa_password_384.key"), b"television"
        )
        self.assertEqual("ecdsa-sha2-nistp384", key.get_name())
        exp_ecdsa = b(FINGER_ECDSA_384.split()[1].replace(":", ""))
        my_ecdsa = hexlify(key.get_fingerprint())
        self.assertEqual(exp_ecdsa, my_ecdsa)
        self.assertEqual(PUB_ECDSA_384.split()[1], key.get_base64())
        self.assertEqual(384, key.get_bits())

    def test_load_ecdsa_transmutes_crypto_exceptions(self):
        path = _support("test_ecdsa_256.key")
        # TODO: nix unittest for pytest
        for exception in (TypeError("onoz"), UnsupportedAlgorithm("oops")):
            with patch(
                "paramiko.ecdsakey.serialization.load_der_private_key"
            ) as loader:
                loader.side_effect = exception
                with pytest.raises(SSHException, match=str(exception)):
                    ECDSAKey.from_private_key_file(path)

    def test_compare_ecdsa_384(self):
        # verify that the private & public keys compare equal
        key = ECDSAKey.from_private_key_file(_support("test_ecdsa_384.key"))
        self.assertEqual(key, key)
        pub = ECDSAKey(data=key.asbytes())
        self.assertTrue(key.can_sign())
        self.assertTrue(not pub.can_sign())
        self.assertEqual(key, pub)

    def test_sign_ecdsa_384(self):
        # verify that the rsa private key can sign and verify
        key = ECDSAKey.from_private_key_file(_support("test_ecdsa_384.key"))
        msg = key.sign_ssh_data(b"ice weasels")
        self.assertTrue(type(msg) is Message)
        msg.rewind()
        self.assertEqual("ecdsa-sha2-nistp384", msg.get_text())
        # ECDSA signatures, like DSS signatures, tend to be different
        # each time, so we can't compare against a "known correct"
        # signature.
        # Even the length of the signature can change.

        msg.rewind()
        pub = ECDSAKey(data=key.asbytes())
        self.assertTrue(pub.verify_ssh_sig(b"ice weasels", msg))

    def test_load_ecdsa_521(self):
        key = ECDSAKey.from_private_key_file(_support("test_ecdsa_521.key"))
        self.assertEqual("ecdsa-sha2-nistp521", key.get_name())
        exp_ecdsa = b(FINGER_ECDSA_521.split()[1].replace(":", ""))
        my_ecdsa = hexlify(key.get_fingerprint())
        self.assertEqual(exp_ecdsa, my_ecdsa)
        self.assertEqual(PUB_ECDSA_521.split()[1], key.get_base64())
        self.assertEqual(521, key.get_bits())

        s = StringIO()
        key.write_private_key(s)
        # Different versions of OpenSSL (SSLeay versions 0x1000100f and
        # 0x1000207f for instance) use different apparently valid (as far as
        # ssh-keygen is concerned) padding. So we can't check the actual value
        # of the pem encoded key.
        s.seek(0)
        key2 = ECDSAKey.from_private_key(s)
        self.assertEqual(key, key2)

    def test_load_ecdsa_password_521(self):
        key = ECDSAKey.from_private_key_file(
            _support("test_ecdsa_password_521.key"), b"television"
        )
        self.assertEqual("ecdsa-sha2-nistp521", key.get_name())
        exp_ecdsa = b(FINGER_ECDSA_521.split()[1].replace(":", ""))
        my_ecdsa = hexlify(key.get_fingerprint())
        self.assertEqual(exp_ecdsa, my_ecdsa)
        self.assertEqual(PUB_ECDSA_521.split()[1], key.get_base64())
        self.assertEqual(521, key.get_bits())

    def test_compare_ecdsa_521(self):
        # verify that the private & public keys compare equal
        key = ECDSAKey.from_private_key_file(_support("test_ecdsa_521.key"))
        self.assertEqual(key, key)
        pub = ECDSAKey(data=key.asbytes())
        self.assertTrue(key.can_sign())
        self.assertTrue(not pub.can_sign())
        self.assertEqual(key, pub)

    def test_sign_ecdsa_521(self):
        # verify that the rsa private key can sign and verify
        key = ECDSAKey.from_private_key_file(_support("test_ecdsa_521.key"))
        msg = key.sign_ssh_data(b"ice weasels")
        self.assertTrue(type(msg) is Message)
        msg.rewind()
        self.assertEqual("ecdsa-sha2-nistp521", msg.get_text())
        # ECDSA signatures, like DSS signatures, tend to be different
        # each time, so we can't compare against a "known correct"
        # signature.
        # Even the length of the signature can change.

        msg.rewind()
        pub = ECDSAKey(data=key.asbytes())
        self.assertTrue(pub.verify_ssh_sig(b"ice weasels", msg))

    def test_load_openssh_format_RSA_key(self):
        key = RSAKey.from_private_key_file(
            _support("test_rsa_openssh.key"), b"television"
        )
        self.assertEqual("ssh-rsa", key.get_name())
        self.assertEqual(PUB_RSA_2K_OPENSSH.split()[1], key.get_base64())
        self.assertEqual(2048, key.get_bits())
        exp_rsa = b(FINGER_RSA_2K_OPENSSH.split()[1].replace(":", ""))
        my_rsa = hexlify(key.get_fingerprint())
        self.assertEqual(exp_rsa, my_rsa)

    def test_loading_openssh_RSA_keys_uses_correct_p_q(self):
        # Re #1723 - not the most elegant test but given how deep it is...
        with patch(
            "paramiko.rsakey.rsa.RSAPrivateNumbers", wraps=RSAPrivateNumbers
        ) as spy:
            # Load key
            RSAKey.from_private_key_file(
                _support("test_rsa_openssh.key"), b"television"
            )
            # Ensure spy saw the correct P and Q values as derived from
            # hardcoded test private key value
            kwargs = spy.call_args[1]
            assert kwargs["p"] == RSA_2K_OPENSSH_P
            assert kwargs["q"] == RSA_2K_OPENSSH_Q

    def test_load_openssh_format_DSS_key(self):
        key = DSSKey.from_private_key_file(
            _support("test_dss_openssh.key"), b"television"
        )
        self.assertEqual("ssh-dss", key.get_name())
        self.assertEqual(PUB_DSS_1K_OPENSSH.split()[1], key.get_base64())
        self.assertEqual(1024, key.get_bits())
        exp_rsa = b(FINGER_DSS_1K_OPENSSH.split()[1].replace(":", ""))
        my_rsa = hexlify(key.get_fingerprint())
        self.assertEqual(exp_rsa, my_rsa)

    def test_load_openssh_format_EC_key(self):
        key = ECDSAKey.from_private_key_file(
            _support("test_ecdsa_384_openssh.key"), b"television"
        )
        self.assertEqual("ecdsa-sha2-nistp384", key.get_name())
        self.assertEqual(PUB_EC_384_OPENSSH.split()[1], key.get_base64())
        self.assertEqual(384, key.get_bits())
        exp_fp = b(FINGER_EC_384_OPENSSH.split()[1].replace(":", ""))
        my_fp = hexlify(key.get_fingerprint())
        self.assertEqual(exp_fp, my_fp)

    def test_salt_size(self):
        # Read an existing encrypted private key
        file_ = _support("test_rsa_password.key")
        password = "television"
        newfile = file_ + ".new"
        newpassword = "radio"
        key = RSAKey(filename=file_, password=password)
        # Write out a newly re-encrypted copy with a new password.
        # When the bug under test exists, this will ValueError.
        try:
            key.write_private_key_file(newfile, password=newpassword)
            self.assert_keyfile_is_encrypted(newfile)
            # Verify the inner key data still matches (when no ValueError)
            key2 = RSAKey(filename=newfile, password=newpassword)
            self.assertEqual(key, key2)
        finally:
            os.remove(newfile)

    def test_load_openssh_format_RSA_nopad(self):
        # check just not exploding with 'Invalid key'
        RSAKey.from_private_key_file(_support("test_rsa_openssh_nopad.key"))

    def test_stringification(self):
        key = RSAKey.from_private_key_file(_support("test_rsa.key"))
        comparable = TEST_KEY_BYTESTR_2 if PY2 else TEST_KEY_BYTESTR_3
        self.assertEqual(str(key), comparable)

    def test_ed25519(self):
        key1 = Ed25519Key.from_private_key_file(_support("test_ed25519.key"))
        key2 = Ed25519Key.from_private_key_file(
            _support("test_ed25519_password.key"), b"abc123"
        )
        self.assertNotEqual(key1.asbytes(), key2.asbytes())

    def test_ed25519_funky_padding(self):
        # Proves #1306 by just not exploding with 'Invalid key'.
        Ed25519Key.from_private_key_file(
            _support("test_ed25519-funky-padding.key")
        )

    def test_ed25519_funky_padding_with_passphrase(self):
        # Proves #1306 by just not exploding with 'Invalid key'.
        Ed25519Key.from_private_key_file(
            _support("test_ed25519-funky-padding_password.key"), b"asdf"
        )

    def test_ed25519_compare(self):
        # verify that the private & public keys compare equal
        key = Ed25519Key.from_private_key_file(_support("test_ed25519.key"))
        self.assertEqual(key, key)
        pub = Ed25519Key(data=key.asbytes())
        self.assertTrue(key.can_sign())
        self.assertTrue(not pub.can_sign())
        self.assertEqual(key, pub)

    # No point testing on systems that never exhibited the bug originally
    @pytest.mark.skipif(
        not is_low_entropy(), reason="Not a low-entropy system"
    )
    def test_ed25519_32bit_collision(self):
        # Re: 2021.10.19 security report email: two different private keys
        # which Paramiko compared as equal on low-entropy platforms.
        original = Ed25519Key.from_private_key_file(
            _support("badhash_key1.ed25519.key")
        )
        generated = Ed25519Key.from_private_key_file(
            _support("badhash_key2.ed25519.key")
        )
        assert original != generated

    def keys(self):
        for key_class, filename in [
            (RSAKey, "test_rsa.key"),
            (DSSKey, "test_dss.key"),
            (ECDSAKey, "test_ecdsa_256.key"),
            (Ed25519Key, "test_ed25519.key"),
        ]:
            key1 = key_class.from_private_key_file(_support(filename))
            key2 = key_class.from_private_key_file(_support(filename))
            yield key1, key2

    def test_keys_are_comparable(self):
        for key1, key2 in self.keys():
            assert key1 == key2

    def test_keys_are_hashable(self):
        # NOTE: this isn't a great test due to hashseed randomization under
        # Python 3 preventing use of static values, but it does still prove
        # that __hash__ is implemented/doesn't explode & works across instances
        for key1, key2 in self.keys():
            assert hash(key1) == hash(key2)

    def test_ed25519_nonbytes_password(self):
        # https://github.com/paramiko/paramiko/issues/1039
        Ed25519Key.from_private_key_file(
            _support("test_ed25519_password.key"),
            # NOTE: not a bytes. Amusingly, the test above for same key DOES
            # explicitly cast to bytes...code smell!
            "abc123",
        )
        # No exception -> it's good. Meh.

    def test_ed25519_load_from_file_obj(self):
        with open(_support("test_ed25519.key")) as pkey_fileobj:
            key = Ed25519Key.from_private_key(pkey_fileobj)
        self.assertEqual(key, key)
        self.assertTrue(key.can_sign())

    def test_keyfile_is_actually_encrypted(self):
        # Read an existing encrypted private key
        file_ = _support("test_rsa_password.key")
        password = "television"
        newfile = file_ + ".new"
        newpassword = "radio"
        key = RSAKey(filename=file_, password=password)
        # Write out a newly re-encrypted copy with a new password.
        # When the bug under test exists, this will ValueError.
        try:
            key.write_private_key_file(newfile, password=newpassword)
            self.assert_keyfile_is_encrypted(newfile)
        finally:
            os.remove(newfile)

    def test_certificates(self):
        # NOTE: we also test 'live' use of cert auth for all key types in
        # test_client.py; this and nearby cert tests are more about the gritty
        # details.
        # PKey.load_certificate
        key_path = _support(os.path.join("cert_support", "test_rsa.key"))
        key = RSAKey.from_private_key_file(key_path)
        self.assertTrue(key.public_blob is None)
        cert_path = _support(
            os.path.join("cert_support", "test_rsa.key-cert.pub")
        )
        key.load_certificate(cert_path)
        self.assertTrue(key.public_blob is not None)
        self.assertEqual(
            key.public_blob.key_type, "ssh-rsa-cert-v01@openssh.com"
        )
        self.assertEqual(key.public_blob.comment, "test_rsa.key.pub")
        # Delve into blob contents, for test purposes
        msg = Message(key.public_blob.key_blob)
        self.assertEqual(msg.get_text(), "ssh-rsa-cert-v01@openssh.com")
        msg.get_string()
        e = msg.get_mpint()
        n = msg.get_mpint()
        self.assertEqual(e, key.public_numbers.e)
        self.assertEqual(n, key.public_numbers.n)
        # Serial number
        self.assertEqual(msg.get_int64(), 1234)

        # Prevented from loading certificate that doesn't match
        key_path = _support(os.path.join("cert_support", "test_ed25519.key"))
        key1 = Ed25519Key.from_private_key_file(key_path)
        self.assertRaises(
            ValueError,
            key1.load_certificate,
            _support("test_rsa.key-cert.pub"),
        )

    @patch("paramiko.pkey.os")
    def _test_keyfile_race(self, os_, exists):
        # Re: CVE-2022-24302
        password = "television"
        newpassword = "radio"
        source = _support("test_ecdsa_384.key")
        new = source + ".new"
        # Mock setup
        os_.path.exists.return_value = exists
        # Attach os flag values to mock
        for attr, value in vars(os).items():
            if attr.startswith("O_"):
                setattr(os_, attr, value)
        # Load fixture key
        key = ECDSAKey(filename=source, password=password)
        key._write_private_key = Mock()
        # Write out in new location
        key.write_private_key_file(new, password=newpassword)
        # Expected open via os module
        os_.open.assert_called_once_with(new, flags=os.O_WRONLY | os.O_CREAT | os.O_TRUNC, mode=o600)
        os_.fdopen.assert_called_once_with(os_.open.return_value, mode="w")
        # Old chmod still around for backwards compat
        os_.chmod.assert_called_once_with(new, o600)
        assert (
            key._write_private_key.call_args[0][0]
            == os_.fdopen.return_value.__enter__.return_value
        )

    def test_new_keyfiles_avoid_file_descriptor_race_on_chmod(self):
        self._test_keyfile_race(exists=False)

    def test_existing_keyfiles_still_work_ok(self):
        self._test_keyfile_race(exists=True)

    def test_new_keyfiles_avoid_descriptor_race_integration(self):
        # Integration-style version of above
        password = "television"
        newpassword = "radio"
        source = _support("test_ecdsa_384.key")
        new = source + ".new"
        # Load fixture key
        key = ECDSAKey(filename=source, password=password)
        try:
            # Write out in new location
            key.write_private_key_file(new, password=newpassword)
            # Test mode
            assert stat.S_IMODE(os.stat(new).st_mode) == o600
            # Prove can open with new password
            reloaded = ECDSAKey(filename=new, password=newpassword)
            assert reloaded == key
        finally:
            if os.path.exists(new):
                os.unlink(new)
```

## `tests/test_pkey.py` — `function:KeyTest._test_keyfile_race` (lines 702-728)

```python
    def _test_keyfile_race(self, os_, exists):
        # Re: CVE-2022-24302
        password = "television"
        newpassword = "radio"
        source = _support("test_ecdsa_384.key")
        new = source + ".new"
        # Mock setup
        os_.path.exists.return_value = exists
        # Attach os flag values to mock
        for attr, value in vars(os).items():
            if attr.startswith("O_"):
                setattr(os_, attr, value)
        # Load fixture key
        key = ECDSAKey(filename=source, password=password)
        key._write_private_key = Mock()
        # Write out in new location
        key.write_private_key_file(new, password=newpassword)
        # Expected open via os module
        os_.open.assert_called_once_with(new, flags=os.O_WRONLY | os.O_CREAT | os.O_TRUNC, mode=o600)
        os_.fdopen.assert_called_once_with(os_.open.return_value, mode="w")
        # Old chmod still around for backwards compat
        os_.chmod.assert_called_once_with(new, o600)
        assert (
            key._write_private_key.call_args[0][0]
            == os_.fdopen.return_value.__enter__.return_value
        )
```

## `tests/test_pkey.py` — `function:KeyTest.test_new_keyfiles_avoid_file_descriptor_race_on_chmod` (lines 730-731)

```python
    def test_new_keyfiles_avoid_file_descriptor_race_on_chmod(self):
        self._test_keyfile_race(exists=False)
```

## `tests/test_pkey.py` — `function:KeyTest.test_existing_keyfiles_still_work_ok` (lines 733-734)

```python
    def test_existing_keyfiles_still_work_ok(self):
        self._test_keyfile_race(exists=True)
```

## `tests/test_pkey.py` — `function:KeyTest.test_new_keyfiles_avoid_descriptor_race_integration` (lines 736-754)

```python
    def test_new_keyfiles_avoid_descriptor_race_integration(self):
        # Integration-style version of above
        password = "television"
        newpassword = "radio"
        source = _support("test_ecdsa_384.key")
        new = source + ".new"
        # Load fixture key
        key = ECDSAKey(filename=source, password=password)
        try:
            # Write out in new location
            key.write_private_key_file(new, password=newpassword)
            # Test mode
            assert stat.S_IMODE(os.stat(new).st_mode) == o600
            # Prove can open with new password
            reloaded = ECDSAKey(filename=new, password=newpassword)
            assert reloaded == key
        finally:
            if os.path.exists(new):
                os.unlink(new)
```

## `tests/test_pkey.py` — `module:<module>@26` (lines 26-26)

```python
import stat
```

## `tests/test_pkey.py` — `module:<module>@40` (lines 40-40)

```python
from paramiko.common import o600
```

## `tests/test_pkey.py` — `module:<module>@44` (lines 44-44)

```python
from mock import patch, Mock
```
