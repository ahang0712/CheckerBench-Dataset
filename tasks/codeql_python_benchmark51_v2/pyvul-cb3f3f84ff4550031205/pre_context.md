# Patch-overlapping Python context before the fix

## `rdiffweb/core/model/__init__.py` — `module:<module>@22` (lines 22-22)

```python
from sqlalchemy import event
```

## `rdiffweb/core/model/__init__.py` — `module:<module>@23` (lines 23-23)

```python
from sqlalchemy.exc import IntegrityError
```

## `rdiffweb/core/model/__init__.py` — `module:<module>@27` (lines 27-27)

```python
from ._sshkey import SshKey  # noqa
```

## `rdiffweb/core/model/__init__.py` — `module:<module>@28` (lines 28-28)

```python
from ._token import Token  # noqa
```

## `rdiffweb/core/model/_sshkey.py` — `module:<module>@19` (lines 19-19)

```python
from sqlalchemy import Column, Integer, Text
```

## `rdiffweb/core/model/_user.py` — `class:UserObject` (lines 54-413)

```python
class UserObject(Base):
    __tablename__ = 'users'
    __table_args__ = {'sqlite_autoincrement': True}

    # Value for role.
    ADMIN_ROLE = 0
    MAINTAINER_ROLE = 5
    USER_ROLE = 10
    ROLES = {
        'admin': ADMIN_ROLE,
        'maintainer': MAINTAINER_ROLE,
        'user': USER_ROLE,
    }
    # Value for mfa field
    DISABLED_MFA = 0
    ENABLED_MFA = 1

    # Regex pattern to be used for validation.
    PATTERN_EMAIL = r"[a-z0-9._%+-]+@[a-z0-9.-]+\.[a-z]{2,4}$"
    PATTERN_FULLNAME = r"""[^!"#$%&()*+,./:;<=>?@[\]_{|}~]+$"""
    PATTERN_USERNAME = r"[a-zA-Z0-9_.\-]+$"

    userid = Column('UserID', Integer, primary_key=True)
    username = Column('Username', String, nullable=False)
    hash_password = Column('Password', String, nullable=False, default="")
    user_root = Column('UserRoot', String, nullable=False, default="")
    _is_admin = deferred(
        Column(
            'IsAdmin',
            SmallInteger,
            nullable=False,
            server_default="0",
            doc="DEPRECATED This column is replaced by 'role'",
        )
    )
    email = Column('UserEmail', String, nullable=False, default="")
    restore_format = deferred(
        Column(
            'RestoreFormat',
            SmallInteger,
            nullable=False,
            server_default="1",
            doc="DEPRECATED This column is not used anymore",
        )
    )
    role = Column('role', SmallInteger, nullable=False, server_default=str(USER_ROLE), default=USER_ROLE)
    fullname = Column('fullname', String, nullable=False, default="")
    mfa = Column('mfa', SmallInteger, nullable=False, default=DISABLED_MFA)
    repo_objs = relationship(
        'RepoObject',
        foreign_keys='UserObject.userid',
        primaryjoin='UserObject.userid == RepoObject.userid',
        uselist=True,
        lazy=True,
        order_by=lambda: RepoObject.repopath,
    )

    @classmethod
    def get_user(cls, user):
        """Return a user object with username case-insensitive"""
        return UserObject.query.filter(func.lower(UserObject.username) == user.lower()).first()

    @classmethod
    def create_admin_user(cls, default_username, default_password):
        # Check if admin user exists. If not, created it.
        userobj = UserObject.get_user(default_username)
        if not userobj:
            userobj = cls.add_user(default_username, role=UserObject.ADMIN_ROLE, user_root='/backups')
            userobj.hash_password = hash_password('admin123')
        # Also make sure to update the password with latest value from config file.
        if default_password:
            if default_password.startswith('{SSHA}') or default_password.startswith('$argon2'):
                userobj.hash_password = default_password
            else:
                userobj.hash_password = hash_password(default_password)
        userobj.add()
        return userobj

    @classmethod
    def add_user(cls, username, password=None, role=USER_ROLE, **attrs):
        """
        Used to add a new user with an optional password.
        """
        assert password is None or isinstance(password, str)
        # Check if user already exists.
        if UserObject.get_user(username):
            raise ValueError(_("User %s already exists." % (username,)))

        # Find a database where to add the user
        logger.info("adding new user [%s]", username)
        userobj = UserObject(
            username=username,
            hash_password=hash_password(password) if password else '',
            role=role,
            **attrs,
        ).add()
        # Return user object
        return userobj

    def add_authorizedkey(self, key, comment=None):
        """
        Add the given key to the user. Adding the key to his `authorized_keys`
        file if it exists and adding it to database.
        """
        # Parse and validate ssh key
        assert key
        key = authorizedkeys.check_publickey(key)

        # Remove option, replace comments.
        key = authorizedkeys.AuthorizedKey(
            options=None, keytype=key.keytype, key=key.key, comment=comment or key.comment
        )

        # If a filename exists, use it by default.
        filename = os.path.join(self.user_root, '.ssh', 'authorized_keys')
        if os.path.isfile(filename):
            with open(filename, mode="r+", encoding='utf-8') as fh:
                if authorizedkeys.exists(fh, key):
                    raise DuplicateSSHKeyError(_("SSH key already exists"))
                logger.info("add key [%s] to [%s] authorized_keys", key, self.username)
                authorizedkeys.add(fh, key)
        else:
            # Also look in database.
            logger.info("add key [%s] to [%s] database", key, self.username)
            try:
                SshKey(userid=self.userid, fingerprint=key.fingerprint, key=key.getvalue()).add().flush()
            except IntegrityError:
                raise DuplicateSSHKeyError(
                    _("Duplicate key. This key already exists or is associated to another user.")
                )
        cherrypy.engine.publish('user_attr_changed', self, {'authorizedkeys': True})
        cherrypy.engine.publish('authorizedkey_added', self, fingerprint=key.fingerprint, comment=comment)

    def add_access_token(self, name, expiration_time=None, length=16):
        """
        Create a new access token. Return the un-encrypted value of the token.
        """
        assert name
        assert length >= 8
        # Generate a random token
        token = ''.join(secrets.choice(string.ascii_lowercase) for i in range(length))
        # Store hash token
        try:
            Token(
                userid=self.userid, name=name, hash_token=hash_password(token), expiration_time=expiration_time
            ).add().flush()
        except IntegrityError:
            raise ValueError(_("Duplicate token name: %s") % name)
        cherrypy.engine.publish('access_token_added', self, name)
        return token

    def valid_user_root(self):
        """
        Check if the current user_root is valid and readable
        """
        try:
            return os.access(self.user_root, os.F_OK) and os.path.isdir(self.user_root)
        except Exception:
            return False

    def delete(self, *args, **kwargs):
        cfg = cherrypy.tree.apps[''].cfg
        if self.username == cfg.admin_user:
            raise ValueError(_("can't delete admin user"))
        # FIXME This should be deleted by cascade
        SshKey.query.filter(SshKey.userid == self.userid).delete()
        RepoObject.query.filter(RepoObject.userid == self.userid).delete()
        Token.query.filter(Token.userid == self.userid).delete()
        # Delete ourself
        return Base.delete(self)

    def delete_authorizedkey(self, fingerprint):
        """
        Remove the given key from the user. Remove the key from his
        `authorized_keys` file if it exists and from database database.
        """
        # If a filename exists, use it by default.
        filename = os.path.join(self.user_root, '.ssh', 'authorized_keys')
        if os.path.isfile(filename):
            with open(filename, mode='r+', encoding='utf-8') as fh:
                logger.info("removing key [%s] from [%s] authorized_keys", fingerprint, self.username)
                authorizedkeys.remove(fh, fingerprint)
        else:
            # Also look in database.
            logger.info("removing key [%s] from [%s] database", fingerprint, self.username)
            SshKey.query.filter(and_(SshKey.userid == self.userid, SshKey.fingerprint == fingerprint)).delete()
        cherrypy.engine.publish('user_attr_changed', self, {'authorizedkeys': True})

    def delete_access_token(self, name):
        assert name
        if not Token.query.filter(Token.userid == self.userid, Token.name == name).delete():
            raise ValueError(_("token name doesn't exists: %s") % name)

    @property
    def disk_usage(self):
        # Skip if user_root is invalid.
        if not self.user_root or not os.path.exists(self.user_root):
            return 0
        values = cherrypy.engine.publish('get_disk_usage', self)
        # Return the first not None value
        return next((v for v in values if v is not None), 0)

    @property
    def disk_quota(self):
        # Skip if user_root is invalid.
        if not self.user_root or not os.path.exists(self.user_root):
            return 0
        values = cherrypy.engine.publish('get_disk_quota', self)
        # Return the first not None value
        return next((v for v in values if v is not None), 0)

    @disk_quota.setter
    def disk_quota(self, value):
        # Skip if user_root is invalid.
        if not self.user_root or not os.path.exists(self.user_root):
            return
        cherrypy.engine.publish('set_disk_quota', self, value)

    @property
    def authorizedkeys(self):
        """
        Return an iterator on the authorized key. Either from his
        `authorized_keys` file if it exists or from database.
        """
        # If a filename exists, use it by default.
        filename = os.path.join(self.user_root, '.ssh', 'authorized_keys')
        if os.path.isfile(filename):
            for k in authorizedkeys.read(filename):
                yield k

        # Also look in database.
        for record in SshKey.query.filter(SshKey.userid == self.userid).all():
            yield authorizedkeys.check_publickey(record.key)

    def refresh_repos(self, delete=False):
        """
        Return list of repositories object to reflect the filesystem folders.

        Return a RepoObject for each sub directories under `user_root` with `rdiff-backup-data`.
        """
        # Update the repositories by walking in the directory tree.
        def _onerror(unused):
            logger.error('error updating user [%s] repos' % self.username, exc_info=1)

        # Get application config
        cfg = cherrypy.tree.apps[''].cfg

        dirty = False
        records = RepoObject.query.filter(RepoObject.userid == self.userid).order_by(RepoObject.repopath).all()
        user_root = os.fsencode(self.user_root)
        for root, dirs, unused_files in os.walk(user_root, _onerror):
            for name in dirs.copy():
                if name.startswith(b'.'):
                    dirs.remove(name)
            if b'rdiff-backup-data' in dirs:
                repopath = os.path.relpath(root, start=user_root)
                del dirs[:]
                # Handle special scenario when the repo is the
                # user_root
                repopath = b'' if repopath == b'.' else repopath

                # Check if repo path exists.
                record_match = next((record for record in records if record.repopath == os.fsdecode(repopath)), None)
                if not record_match:
                    # Add repository to database.
                    RepoObject(user=self, repopath=os.fsdecode(repopath)).add()
                    dirty = True
                else:
                    records.remove(record_match)
            if root.count(SEP) - user_root.count(SEP) >= cfg.max_depth:
                del dirs[:]
        # If enabled, remove entried from database
        if delete:
            for record in records:
                RepoObject.query.filter(RepoObject.repoid == record.repoid).delete()
        return dirty

    @hybrid_property
    def is_admin(self):
        return self.role is not None and self.role <= self.ADMIN_ROLE

    @hybrid_property
    def is_ldap(self):
        return self.hash_password is None or self.hash_password == ''

    @is_ldap.expression
    def is_ldap(cls):
        return or_(cls.hash_password.is_(None), cls.hash_password == '')

    @hybrid_property
    def is_maintainer(self):
        return self.role is not None and self.role <= self.MAINTAINER_ROLE

    def set_password(self, password):
        """
        Change the user's password. Raise a ValueError if the username or
        the password are invalid.
        """
        assert isinstance(password, str)
        if not password:
            raise ValueError("password can't be empty")
        cfg = cherrypy.tree.apps[''].cfg

        # Cannot update admin-password if defined
        if self.username == cfg.admin_user and cfg.admin_password:
            raise ValueError(_("can't update admin-password defined in configuration file"))

        # Check password length
        if cfg.password_min_length > len(password) or len(password) > cfg.password_max_length:
            raise ValueError(
                _('Password must have between %(min)d and %(max)d characters.')
                % {'min': cfg.password_min_length, 'max': cfg.password_max_length}
            )

        # Verify password score using zxcvbn
        stats = zxcvbn(password)
        if stats.get('score') < cfg.password_score:
            msg = _('Password too weak.')
            warning = stats.get('feedback', {}).get('warning')
            suggestions = stats.get('feedback', {}).get('suggestions')
            if warning:
                msg += ' ' + warning
            if suggestions:
                msg += ' ' + ' '.join(suggestions)
            raise ValueError(msg)

        # Store password
        logger.info("updating user password [%s] and revoke sessions", self.username)
        self.hash_password = hash_password(password)

        # Revoke other session to force re-login
        session_id = cherrypy.serving.session.id if hasattr(cherrypy.serving, 'session') else None
        SessionObject.query.filter(
            SessionObject.username == self.username,
            SessionObject.id != session_id,
        ).delete()

    def __eq__(self, other):
        return type(self) == type(other) and inspect(self).key == inspect(other).key

    @validates('username')
    def validates_username(self, key, value):
        if self.username:
            raise ValueError('Username cannot be modified.')
        return value

    def validate_access_token(self, token):
        """
        Check if the given token matches.
        """
        for access_token in Token.query.all():
            if access_token.is_expired:
                continue
            if check_password(token, access_token.hash_token):
                # When it matches, return the record.
                return access_token
        return False

    def validate_password(self, password):
        return check_password(password, self.hash_password)
```

## `rdiffweb/core/model/_user.py` — `function:UserObject.add_authorizedkey` (lines 153-185)

```python
    def add_authorizedkey(self, key, comment=None):
        """
        Add the given key to the user. Adding the key to his `authorized_keys`
        file if it exists and adding it to database.
        """
        # Parse and validate ssh key
        assert key
        key = authorizedkeys.check_publickey(key)

        # Remove option, replace comments.
        key = authorizedkeys.AuthorizedKey(
            options=None, keytype=key.keytype, key=key.key, comment=comment or key.comment
        )

        # If a filename exists, use it by default.
        filename = os.path.join(self.user_root, '.ssh', 'authorized_keys')
        if os.path.isfile(filename):
            with open(filename, mode="r+", encoding='utf-8') as fh:
                if authorizedkeys.exists(fh, key):
                    raise DuplicateSSHKeyError(_("SSH key already exists"))
                logger.info("add key [%s] to [%s] authorized_keys", key, self.username)
                authorizedkeys.add(fh, key)
        else:
            # Also look in database.
            logger.info("add key [%s] to [%s] database", key, self.username)
            try:
                SshKey(userid=self.userid, fingerprint=key.fingerprint, key=key.getvalue()).add().flush()
            except IntegrityError:
                raise DuplicateSSHKeyError(
                    _("Duplicate key. This key already exists or is associated to another user.")
                )
        cherrypy.engine.publish('user_attr_changed', self, {'authorizedkeys': True})
        cherrypy.engine.publish('authorizedkey_added', self, fingerprint=key.fingerprint, comment=comment)
```

## `rdiffweb/core/model/tests/test_user.py` — `class:UserObjectTest` (lines 38-471)

```python
class UserObjectTest(rdiffweb.test.WebCase):
    def _read_ssh_key(self):
        """Readthe pub key from test packages"""
        filename = pkg_resources.resource_filename('rdiffweb.core.tests', 'test_publickey_ssh_rsa.pub')
        with open(filename, 'r', encoding='utf8') as f:
            return f.readline()

    def _read_authorized_keys(self):
        """Read the content of test_authorized_keys"""
        filename = pkg_resources.resource_filename('rdiffweb.core.tests', 'test_authorized_keys')
        with open(filename, 'r', encoding='utf8') as f:
            return f.read()

    def setUp(self):
        super().setUp()
        self.listener = MagicMock()
        cherrypy.engine.subscribe('access_token_added', self.listener.access_token_added, priority=50)
        cherrypy.engine.subscribe('queue_mail', self.listener.queue_mail, priority=50)
        cherrypy.engine.subscribe('user_added', self.listener.user_added, priority=50)
        cherrypy.engine.subscribe('user_attr_changed', self.listener.user_attr_changed, priority=50)
        cherrypy.engine.subscribe('user_deleted', self.listener.user_deleted, priority=50)
        cherrypy.engine.subscribe('user_login', self.listener.user_login, priority=50)
        cherrypy.engine.subscribe('user_password_changed', self.listener.user_password_changed, priority=50)

    def tearDown(self):
        cherrypy.engine.unsubscribe('access_token_added', self.listener.access_token_added)
        cherrypy.engine.unsubscribe('queue_mail', self.listener.queue_mail)
        cherrypy.engine.unsubscribe('user_added', self.listener.user_added)
        cherrypy.engine.unsubscribe('user_attr_changed', self.listener.user_attr_changed)
        cherrypy.engine.unsubscribe('user_deleted', self.listener.user_deleted)
        cherrypy.engine.unsubscribe('user_login', self.listener.user_login)
        cherrypy.engine.unsubscribe('user_password_changed', self.listener.user_password_changed)
        return super().tearDown()

    def test_add_user(self):
        """Add user to database."""
        userobj = UserObject.add_user('joe')
        userobj.commit()
        self.assertIsNotNone(UserObject.get_user('joe'))
        # Check if listener called
        self.listener.user_added.assert_called_once_with(userobj)

    def test_add_user_updated_by_listener(self):
        """Add user to database."""
        # Given a listener with side effet
        def change_user_obj(userobj):
            userobj.user_root = '/new/value'

        self.listener.user_added.side_effect = change_user_obj
        # When adding user
        userobj = UserObject.add_user('joe')
        userobj.commit()
        self.assertIsNotNone(UserObject.get_user('joe'))
        # Then lister get called
        self.listener.user_added.assert_called_once_with(userobj)
        # Then object was updated by listener
        self.assertEqual('/new/value', userobj.user_root)

    def test_add_user_with_duplicate(self):
        """Add user to database."""
        user = UserObject.add_user('denise')
        user.commit()
        self.listener.user_added.reset_mock()
        with self.assertRaises(ValueError):
            UserObject.add_user('denise')
        # Check if listener called
        self.listener.user_added.assert_not_called()

    def test_add_user_with_duplicate_caseinsensitive(self):
        """Add user to database."""
        user = UserObject.add_user('denise')
        user.commit()
        self.listener.user_added.reset_mock()
        with self.assertRaises(ValueError):
            UserObject.add_user('dEnIse')
        # Check if listener called
        self.listener.user_added.assert_not_called()

    def test_add_user_with_password(self):
        """Add user to database with password."""
        userobj = UserObject.add_user('jo', 'password')
        userobj.commit()
        self.assertIsNotNone(UserObject.get_user('jo'))
        # Check if listener called
        self.listener.user_added.assert_called_once_with(userobj)

    def test_delete_admin_user(self):
        # Trying to delete admin user should raise an error.
        userobj = UserObject.get_user('admin')
        with self.assertRaises(ValueError):
            userobj.delete()

    def test_users(self):
        # Check admin exists
        self.assertEqual(1, UserObject.query.count())
        # Create user.
        user = UserObject.add_user('annik')
        user.commit()
        users = UserObject.query.all()
        self.assertEqual(2, len(users))
        self.assertEqual('annik', users[1].username)
        # Then 2 user exists
        self.assertEqual(2, UserObject.query.count())

    def test_get_user(self):
        # Create new user
        user = UserObject.add_user('bernie', 'my-password')
        user.user_root = self.testcases
        user.role = UserObject.ADMIN_ROLE
        user.email = 'bernie@gmail.com'
        user.refresh_repos()
        user.commit()
        self.assertEqual(['broker-repo', 'testcases'], sorted([r.name for r in user.repo_objs]))
        user.repo_objs[0].maxage = -1
        user.repo_objs[1].maxage = 3
        user.commit()

        # Get user record.
        obj = UserObject.get_user('bernie')
        self.assertIsNotNone(obj)
        self.assertEqual('bernie', obj.username)
        self.assertEqual('bernie@gmail.com', obj.email)
        self.assertEqual(['broker-repo', 'testcases'], sorted([r.name for r in obj.repo_objs]))
        self.assertEqual(self.testcases, obj.user_root)
        self.assertEqual(True, obj.is_admin)
        self.assertEqual(UserObject.ADMIN_ROLE, obj.role)

        # Get repo object
        self.assertEqual('broker-repo', obj.repo_objs[0].name)
        self.assertEqual(-1, obj.repo_objs[0].maxage)
        self.assertEqual('testcases', obj.repo_objs[1].name)
        self.assertEqual(3, obj.repo_objs[1].maxage)

    def test_get_user_case_insensitive(self):
        userobj1 = UserObject.get_user(self.USERNAME)
        userobj2 = UserObject.get_user(self.USERNAME.lower())
        userobj3 = UserObject.get_user(self.USERNAME.upper())
        self.assertEqual(userobj1, userobj2)
        self.assertEqual(userobj2, userobj3)

    def test_get_user_with_invalid_user(self):
        self.assertIsNone(UserObject.get_user('invalid'))

    def test_get_set(self):
        user = UserObject.add_user('larry', 'password')
        user.add().commit()

        self.assertEqual('', user.email)
        self.assertEqual([], user.repo_objs)
        self.assertEqual('', user.user_root)
        self.assertEqual(False, user.is_admin)
        self.assertEqual(UserObject.USER_ROLE, user.role)

        user.user_root = self.testcases
        user.refresh_repos()
        user.commit()
        self.listener.user_attr_changed.assert_called_with(user, {'user_root': ('', self.testcases)})
        self.listener.user_attr_changed.reset_mock()
        user = UserObject.get_user('larry')
        user.role = UserObject.ADMIN_ROLE
        user.commit()
        self.listener.user_attr_changed.assert_called_with(
            user, {'role': (UserObject.USER_ROLE, UserObject.ADMIN_ROLE)}
        )
        self.listener.user_attr_changed.reset_mock()
        user = UserObject.get_user('larry')
        user.email = 'larry@gmail.com'
        user.commit()
        self.listener.user_attr_changed.assert_called_with(user, {'email': ('', 'larry@gmail.com')})
        self.listener.user_attr_changed.reset_mock()

        self.assertEqual('larry@gmail.com', user.email)
        self.assertEqual(['broker-repo', 'testcases'], sorted([r.name for r in user.repo_objs]))
        self.assertEqual(self.testcases, user.user_root)
        self.assertEqual(True, user.is_admin)
        self.assertEqual(UserObject.ADMIN_ROLE, user.role)

    def test_set_role_null(self):
        # Given a user
        user = UserObject.add_user('annik', 'password')
        user.add().commit()
        # When trying to set the role to null
        user.role = None
        # Then an exception is raised
        with self.assertRaises(Exception):
            user.add().commit()

    @parameterized.expand(
        [
            (-1, True),
            (0, True),
            (5, False),
            (10, False),
            (15, False),
        ]
    )
    def test_is_admin(self, role, expected_is_admin):
        # Given a user
        user = UserObject.add_user('annik', 'password')
        # When setting the role value
        user.role = role
        user.commit()
        # Then the is_admin value get updated too
        self.assertEqual(expected_is_admin, user.is_admin)

    @parameterized.expand(
        [
            (-1, True),
            (0, True),
            (5, True),
            (10, False),
            (15, False),
        ]
    )
    def test_is_maintainer(self, role, expected_is_maintainer):
        # Given a user
        user = UserObject.add_user('annik', 'password')
        # When setting the role value
        user.role = role
        user.commit()
        # Then the is_admin value get updated too
        self.assertEqual(expected_is_maintainer, user.is_maintainer)

    def test_set_password_update(self):
        # Given a user in database with a password
        userobj = UserObject.add_user('annik', 'password')
        userobj.commit()
        self.listener.user_password_changed.reset_mock()
        # When updating the user's password
        userobj.set_password('new_password')
        userobj.commit()
        # Then password is SSHA
        self.assertTrue(check_password('new_password', userobj.hash_password))
        # Check if listener called
        self.listener.user_password_changed.assert_called_once_with(userobj)

    def test_delete_user(self):
        # Given an existing user in database
        userobj = UserObject.add_user('vicky')
        userobj.commit()
        self.assertIsNotNone(UserObject.get_user('vicky'))
        # When deleting that user
        userobj.delete()
        userobj.commit()
        # Then user it no longer in database
        self.assertIsNone(UserObject.get_user('vicky'))
        # Then listner was called
        self.listener.user_deleted.assert_called_once_with('vicky')

    def test_set_password_empty(self):
        """Expect error when trying to update password of invalid user."""
        userobj = UserObject.add_user('john')
        userobj.commit()
        with self.assertRaises(ValueError):
            self.assertFalse(userobj.set_password(''))

    def test_disk_quota(self):
        """
        Just make a call to the function.
        """
        userobj = UserObject.get_user(self.USERNAME)
        userobj.disk_quota

    def test_disk_usage(self):
        """
        Just make a call to the function.
        """
        userobj = UserObject.get_user(self.USERNAME)
        disk_usage = userobj.disk_usage
        self.assertIsInstance(disk_usage, int)

    def test_add_authorizedkey_without_file(self):
        """
        Add an ssh key for a user without an authorizedkey file.
        """
        # Read the pub key
        key = self._read_ssh_key()
        # Add the key to the user
        userobj = UserObject.get_user(self.USERNAME)
        userobj.add_authorizedkey(key)
        userobj.commit()

        # validate
        keys = list(userobj.authorizedkeys)
        self.assertEqual(1, len(keys), "expecting one key")
        self.assertEqual("3c:99:ed:a7:82:a8:71:09:2c:15:3d:78:4a:8c:11:99", keys[0].fingerprint)

    def test_add_authorizedkey_duplicate(self):
        # Read the pub key
        key = self._read_ssh_key()
        # Add the key to the user
        userobj = UserObject.get_user(self.USERNAME)
        userobj.add_authorizedkey(key)
        userobj.commit()
        # Add the same key
        with self.assertRaises(DuplicateSSHKeyError):
            userobj.add_authorizedkey(key)
            userobj.commit()

    def test_add_authorizedkey_with_file(self):
        """
        Add an ssh key for a user with an authorizedkey file.
        """
        userobj = UserObject.get_user(self.USERNAME)

        # Create empty authorized_keys file
        os.mkdir(os.path.join(userobj.user_root, '.ssh'))
        filename = os.path.join(userobj.user_root, '.ssh', 'authorized_keys')
        open(filename, 'a').close()

        # Read the pub key
        key = self._read_ssh_key()
        userobj.add_authorizedkey(key)
        userobj.commit()

        # Validate
        with open(filename, 'r') as fh:
            self.assertEqual(key, fh.read())

    def test_delete_authorizedkey_without_file(self):
        """
        Remove an ssh key for a user without authorizedkey file.
        """
        # Update user with ssh keys.
        data = self._read_authorized_keys()
        userobj = UserObject.get_user(self.USERNAME)
        for k in authorizedkeys.read(StringIO(data)):
            try:
                userobj.add_authorizedkey(k.getvalue())
            except ValueError:
                # Some ssh key in the testing file are not valid.
                pass

        # Get the keys
        keys = list(userobj.authorizedkeys)
        self.assertEqual(2, len(keys))

        # Remove a key
        userobj.delete_authorizedkey("9a:f1:69:3c:bc:5a:cd:02:5e:33:bc:cd:c0:01:eb:4c")
        userobj.commit()

        # Validate
        keys = list(userobj.authorizedkeys)
        self.assertEqual(1, len(keys))

    def test_delete_authorizedkey_with_file(self):
        """
        Remove an ssh key for a user with authorizedkey file.
        """
        # Create authorized_keys file
        data = self._read_authorized_keys()
        userobj = UserObject.get_user(self.USERNAME)
        os.mkdir(os.path.join(userobj.user_root, '.ssh'))
        filename = os.path.join(userobj.user_root, '.ssh', 'authorized_keys')
        with open(filename, 'w') as f:
            f.write(data)

        # Get the keys
        keys = list(userobj.authorizedkeys)
        self.assertEqual(5, len(keys))

        # Remove a key
        userobj.delete_authorizedkey("9a:f1:69:3c:bc:5a:cd:02:5e:33:bc:cd:c0:01:eb:4c")

        # Validate
        keys = list(userobj.authorizedkeys)
        self.assertEqual(4, len(keys))

    def test_repo_objs(self):
        # Given a user with a list of repositories
        userobj = UserObject.get_user(self.USERNAME)
        repos = sorted(userobj.repo_objs, key=lambda r: r.name)
        self.assertEqual(['broker-repo', 'testcases'], [r.name for r in repos])
        # When deleting a repository empty list
        repos[1].delete()
        repos[1].commit()
        # Then the repository is removed from the list.
        self.assertEqual(['broker-repo'], sorted([r.name for r in userobj.repo_objs]))

    def test_refresh_repos_without_delete(self):
        # Given a user with invalid repositories
        userobj = UserObject.get_user(self.USERNAME)
        RepoObject.query.delete()
        RepoObject(userid=userobj.userid, repopath='invalid').add().commit()
        self.assertEqual(['invalid'], sorted([r.name for r in userobj.repo_objs]))
        # When updating the repository list without deletion
        userobj.refresh_repos()
        userobj.commit()
        # Then the list invlaid the invalid repo and new repos
        self.assertEqual(['broker-repo', 'invalid', 'testcases'], sorted([r.name for r in userobj.repo_objs]))

    def test_refresh_repos_with_delete(self):
        # Given a user with invalid repositories
        userobj = UserObject.get_user(self.USERNAME)
        RepoObject.query.delete()
        RepoObject(userid=userobj.userid, repopath='invalid').add().commit()
        self.assertEqual(['invalid'], sorted([r.name for r in userobj.repo_objs]))
        # When updating the repository list without deletion
        userobj.refresh_repos(delete=True)
        userobj.commit()
        # Then the list invlaid the invalid repo and new repos
        userobj.expire()
        self.assertEqual(['broker-repo', 'testcases'], sorted([r.name for r in userobj.repo_objs]))

    def test_refresh_repos_with_single_repo(self):
        # Given a user with invalid repositories
        userobj = UserObject.get_user(self.USERNAME)
        userobj.user_root = os.path.join(self.testcases, 'testcases')
        # When updating the repository list without deletion
        userobj.refresh_repos(delete=True)
        userobj.commit()
        # Then the list invlaid the invalid repo and new repos
        userobj.expire()
        self.assertEqual([''], sorted([r.name for r in userobj.repo_objs]))

    def test_refresh_repos_with_empty_userroot(self):
        # Given a user with valid repositories relative to root
        userobj = UserObject.get_user(self.USERNAME)
        for repo in userobj.repo_objs:
            repo.repopath = self.testcases[1:] + '/' + repo.repopath
            repo.add().commit()
        userobj.user_root = '/'
        userobj.add().commit()
        self.assertEqual(['interrupted', 'ok'], sorted([r.status[0] for r in userobj.repo_objs]))
        # When updating it's userroot directory to an empty value
        userobj.user_root = ''
        userobj.add().commit()
        UserObject.session.expire_all()
        # Then close session
        cherrypy.tools.db.on_end_resource()
        # Then repo status is "broken"
        userobj = UserObject.get_user(self.USERNAME)
        self.assertFalse(userobj.valid_user_root())
        self.assertEqual(['failed', 'failed'], [r.status[0] for r in userobj.repo_objs])
```

## `rdiffweb/core/model/tests/test_user.py` — `function:UserObjectTest.test_add_authorizedkey_duplicate` (lines 325-335)

```python
    def test_add_authorizedkey_duplicate(self):
        # Read the pub key
        key = self._read_ssh_key()
        # Add the key to the user
        userobj = UserObject.get_user(self.USERNAME)
        userobj.add_authorizedkey(key)
        userobj.commit()
        # Add the same key
        with self.assertRaises(DuplicateSSHKeyError):
            userobj.add_authorizedkey(key)
            userobj.commit()
```

## `rdiffweb/core/model/tests/test_user.py` — `function:UserObjectTest.test_add_authorizedkey_with_file` (lines 337-355)

```python
    def test_add_authorizedkey_with_file(self):
        """
        Add an ssh key for a user with an authorizedkey file.
        """
        userobj = UserObject.get_user(self.USERNAME)

        # Create empty authorized_keys file
        os.mkdir(os.path.join(userobj.user_root, '.ssh'))
        filename = os.path.join(userobj.user_root, '.ssh', 'authorized_keys')
        open(filename, 'a').close()

        # Read the pub key
        key = self._read_ssh_key()
        userobj.add_authorizedkey(key)
        userobj.commit()

        # Validate
        with open(filename, 'r') as fh:
            self.assertEqual(key, fh.read())
```
