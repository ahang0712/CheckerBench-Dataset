# Patch-overlapping Python context before the fix

## `synapse/http/client.py` — `class:SimpleHttpClient` (lines 280-736)

```python
class SimpleHttpClient:
    """
    A simple, no-frills HTTP client with methods that wrap up common ways of
    using HTTP in Matrix
    """

    def __init__(
        self,
        hs: "HomeServer",
        treq_args: Dict[str, Any] = {},
        ip_whitelist: Optional[IPSet] = None,
        ip_blacklist: Optional[IPSet] = None,
        http_proxy: Optional[bytes] = None,
        https_proxy: Optional[bytes] = None,
    ):
        """
        Args:
            hs
            treq_args: Extra keyword arguments to be given to treq.request.
            ip_blacklist: The IP addresses that are blacklisted that
                we may not request.
            ip_whitelist: The whitelisted IP addresses, that we can
               request if it were otherwise caught in a blacklist.
            http_proxy: proxy server to use for http connections. host[:port]
            https_proxy: proxy server to use for https connections. host[:port]
        """
        self.hs = hs

        self._ip_whitelist = ip_whitelist
        self._ip_blacklist = ip_blacklist
        self._extra_treq_args = treq_args

        self.user_agent = hs.version_string
        self.clock = hs.get_clock()
        if hs.config.user_agent_suffix:
            self.user_agent = "%s %s" % (self.user_agent, hs.config.user_agent_suffix)

        # We use this for our body producers to ensure that they use the correct
        # reactor.
        self._cooperator = Cooperator(scheduler=_make_scheduler(hs.get_reactor()))

        self.user_agent = self.user_agent.encode("ascii")

        if self._ip_blacklist:
            # If we have an IP blacklist, we need to use a DNS resolver which
            # filters out blacklisted IP addresses, to prevent DNS rebinding.
            self.reactor = BlacklistingReactorWrapper(
                hs.get_reactor(), self._ip_whitelist, self._ip_blacklist
            )
        else:
            self.reactor = hs.get_reactor()

        # the pusher makes lots of concurrent SSL connections to sygnal, and
        # tends to do so in batches, so we need to allow the pool to keep
        # lots of idle connections around.
        pool = HTTPConnectionPool(self.reactor)
        # XXX: The justification for using the cache factor here is that larger instances
        # will need both more cache and more connections.
        # Still, this should probably be a separate dial
        pool.maxPersistentPerHost = max((100 * hs.config.caches.global_factor, 5))
        pool.cachedConnectionTimeout = 2 * 60

        self.agent = ProxyAgent(
            self.reactor,
            connectTimeout=15,
            contextFactory=self.hs.get_http_client_context_factory(),
            pool=pool,
            http_proxy=http_proxy,
            https_proxy=https_proxy,
        )

        if self._ip_blacklist:
            # If we have an IP blacklist, we then install the blacklisting Agent
            # which prevents direct access to IP addresses, that are not caught
            # by the DNS resolution.
            self.agent = BlacklistingAgentWrapper(
                self.agent,
                ip_whitelist=self._ip_whitelist,
                ip_blacklist=self._ip_blacklist,
            )

    async def request(
        self,
        method: str,
        uri: str,
        data: Optional[bytes] = None,
        headers: Optional[Headers] = None,
    ) -> IResponse:
        """
        Args:
            method: HTTP method to use.
            uri: URI to query.
            data: Data to send in the request body, if applicable.
            headers: Request headers.

        Returns:
            Response object, once the headers have been read.

        Raises:
            RequestTimedOutError if the request times out before the headers are read

        """
        outgoing_requests_counter.labels(method).inc()

        # log request but strip `access_token` (AS requests for example include this)
        logger.debug("Sending request %s %s", method, redact_uri(uri))

        with start_active_span(
            "outgoing-client-request",
            tags={
                tags.SPAN_KIND: tags.SPAN_KIND_RPC_CLIENT,
                tags.HTTP_METHOD: method,
                tags.HTTP_URL: uri,
            },
            finish_on_close=True,
        ):
            try:
                body_producer = None
                if data is not None:
                    body_producer = QuieterFileBodyProducer(
                        BytesIO(data), cooperator=self._cooperator,
                    )

                request_deferred = treq.request(
                    method,
                    uri,
                    agent=self.agent,
                    data=body_producer,
                    headers=headers,
                    **self._extra_treq_args,
                )  # type: defer.Deferred

                # we use our own timeout mechanism rather than treq's as a workaround
                # for https://twistedmatrix.com/trac/ticket/9534.
                request_deferred = timeout_deferred(
                    request_deferred, 60, self.hs.get_reactor(),
                )

                # turn timeouts into RequestTimedOutErrors
                request_deferred.addErrback(_timeout_to_request_timed_out_error)

                response = await make_deferred_yieldable(request_deferred)

                incoming_responses_counter.labels(method, response.code).inc()
                logger.info(
                    "Received response to %s %s: %s",
                    method,
                    redact_uri(uri),
                    response.code,
                )
                return response
            except Exception as e:
                incoming_responses_counter.labels(method, "ERR").inc()
                logger.info(
                    "Error sending request to  %s %s: %s %s",
                    method,
                    redact_uri(uri),
                    type(e).__name__,
                    e.args[0],
                )
                set_tag(tags.ERROR, True)
                set_tag("error_reason", e.args[0])
                raise

    async def post_urlencoded_get_json(
        self,
        uri: str,
        args: Optional[Mapping[str, Union[str, List[str]]]] = None,
        headers: Optional[RawHeaders] = None,
    ) -> Any:
        """
        Args:
            uri: uri to query
            args: parameters to be url-encoded in the body
            headers: a map from header name to a list of values for that header

        Returns:
            parsed json

        Raises:
            RequestTimedOutError: if there is a timeout before the response headers
               are received. Note there is currently no timeout on reading the response
               body.

            HttpResponseException: On a non-2xx HTTP response.

            ValueError: if the response was not JSON
        """

        # TODO: Do we ever want to log message contents?
        logger.debug("post_urlencoded_get_json args: %s", args)

        query_bytes = encode_query_args(args)

        actual_headers = {
            b"Content-Type": [b"application/x-www-form-urlencoded"],
            b"User-Agent": [self.user_agent],
            b"Accept": [b"application/json"],
        }
        if headers:
            actual_headers.update(headers)  # type: ignore

        response = await self.request(
            "POST", uri, headers=Headers(actual_headers), data=query_bytes
        )

        body = await make_deferred_yieldable(readBody(response))

        if 200 <= response.code < 300:
            return json_decoder.decode(body.decode("utf-8"))
        else:
            raise HttpResponseException(
                response.code, response.phrase.decode("ascii", errors="replace"), body
            )

    async def post_json_get_json(
        self, uri: str, post_json: Any, headers: Optional[RawHeaders] = None
    ) -> Any:
        """

        Args:
            uri: URI to query.
            post_json: request body, to be encoded as json
            headers: a map from header name to a list of values for that header

        Returns:
            parsed json

        Raises:
            RequestTimedOutError: if there is a timeout before the response headers
               are received. Note there is currently no timeout on reading the response
               body.

            HttpResponseException: On a non-2xx HTTP response.

            ValueError: if the response was not JSON
        """
        json_str = encode_canonical_json(post_json)

        logger.debug("HTTP POST %s -> %s", json_str, uri)

        actual_headers = {
            b"Content-Type": [b"application/json"],
            b"User-Agent": [self.user_agent],
            b"Accept": [b"application/json"],
        }
        if headers:
            actual_headers.update(headers)  # type: ignore

        response = await self.request(
            "POST", uri, headers=Headers(actual_headers), data=json_str
        )

        body = await make_deferred_yieldable(readBody(response))

        if 200 <= response.code < 300:
            return json_decoder.decode(body.decode("utf-8"))
        else:
            raise HttpResponseException(
                response.code, response.phrase.decode("ascii", errors="replace"), body
            )

    async def get_json(
        self,
        uri: str,
        args: Optional[QueryParams] = None,
        headers: Optional[RawHeaders] = None,
    ) -> Any:
        """Gets some json from the given URI.

        Args:
            uri: The URI to request, not including query parameters
            args: A dictionary used to create query string
            headers: a map from header name to a list of values for that header
        Returns:
            Succeeds when we get a 2xx HTTP response, with the HTTP body as JSON.
        Raises:
            RequestTimedOutError: if there is a timeout before the response headers
               are received. Note there is currently no timeout on reading the response
               body.

            HttpResponseException On a non-2xx HTTP response.

            ValueError: if the response was not JSON
        """
        actual_headers = {b"Accept": [b"application/json"]}
        if headers:
            actual_headers.update(headers)  # type: ignore

        body = await self.get_raw(uri, args, headers=headers)
        return json_decoder.decode(body.decode("utf-8"))

    async def put_json(
        self,
        uri: str,
        json_body: Any,
        args: Optional[QueryParams] = None,
        headers: RawHeaders = None,
    ) -> Any:
        """Puts some json to the given URI.

        Args:
            uri: The URI to request, not including query parameters
            json_body: The JSON to put in the HTTP body,
            args: A dictionary used to create query strings
            headers: a map from header name to a list of values for that header
        Returns:
            Succeeds when we get a 2xx HTTP response, with the HTTP body as JSON.
        Raises:
             RequestTimedOutError: if there is a timeout before the response headers
               are received. Note there is currently no timeout on reading the response
               body.

            HttpResponseException On a non-2xx HTTP response.

            ValueError: if the response was not JSON
        """
        if args:
            query_str = urllib.parse.urlencode(args, True)
            uri = "%s?%s" % (uri, query_str)

        json_str = encode_canonical_json(json_body)

        actual_headers = {
            b"Content-Type": [b"application/json"],
            b"User-Agent": [self.user_agent],
            b"Accept": [b"application/json"],
        }
        if headers:
            actual_headers.update(headers)  # type: ignore

        response = await self.request(
            "PUT", uri, headers=Headers(actual_headers), data=json_str
        )

        body = await make_deferred_yieldable(readBody(response))

        if 200 <= response.code < 300:
            return json_decoder.decode(body.decode("utf-8"))
        else:
            raise HttpResponseException(
                response.code, response.phrase.decode("ascii", errors="replace"), body
            )

    async def get_raw(
        self,
        uri: str,
        args: Optional[QueryParams] = None,
        headers: Optional[RawHeaders] = None,
    ) -> bytes:
        """Gets raw text from the given URI.

        Args:
            uri: The URI to request, not including query parameters
            args: A dictionary used to create query strings
            headers: a map from header name to a list of values for that header
        Returns:
            Succeeds when we get a 2xx HTTP response, with the
            HTTP body as bytes.
        Raises:
            RequestTimedOutError: if there is a timeout before the response headers
               are received. Note there is currently no timeout on reading the response
               body.

            HttpResponseException on a non-2xx HTTP response.
        """
        if args:
            query_str = urllib.parse.urlencode(args, True)
            uri = "%s?%s" % (uri, query_str)

        actual_headers = {b"User-Agent": [self.user_agent]}
        if headers:
            actual_headers.update(headers)  # type: ignore

        response = await self.request("GET", uri, headers=Headers(actual_headers))

        body = await make_deferred_yieldable(readBody(response))

        if 200 <= response.code < 300:
            return body
        else:
            raise HttpResponseException(
                response.code, response.phrase.decode("ascii", errors="replace"), body
            )

    # XXX: FIXME: This is horribly copy-pasted from matrixfederationclient.
    # The two should be factored out.

    async def get_file(
        self,
        url: str,
        output_stream: BinaryIO,
        max_size: Optional[int] = None,
        headers: Optional[RawHeaders] = None,
    ) -> Tuple[int, Dict[bytes, List[bytes]], str, int]:
        """GETs a file from a given URL
        Args:
            url: The URL to GET
            output_stream: File to write the response body to.
            headers: A map from header name to a list of values for that header
        Returns:
            A tuple of the file length, dict of the response
            headers, absolute URI of the response and HTTP response code.

        Raises:
            RequestTimedOutError: if there is a timeout before the response headers
               are received. Note there is currently no timeout on reading the response
               body.

            SynapseError: if the response is not a 2xx, the remote file is too large, or
               another exception happens during the download.
        """

        actual_headers = {b"User-Agent": [self.user_agent]}
        if headers:
            actual_headers.update(headers)  # type: ignore

        response = await self.request("GET", url, headers=Headers(actual_headers))

        resp_headers = dict(response.headers.getAllRawHeaders())

        if (
            b"Content-Length" in resp_headers
            and max_size
            and int(resp_headers[b"Content-Length"][0]) > max_size
        ):
            logger.warning("Requested URL is too large > %r bytes" % (max_size,))
            raise SynapseError(
                502,
                "Requested file is too large > %r bytes" % (max_size,),
                Codes.TOO_LARGE,
            )

        if response.code > 299:
            logger.warning("Got %d when downloading %s" % (response.code, url))
            raise SynapseError(502, "Got error %d" % (response.code,), Codes.UNKNOWN)

        # TODO: if our Content-Type is HTML or something, just read the first
        # N bytes into RAM rather than saving it all to disk only to read it
        # straight back in again

        try:
            length = await make_deferred_yieldable(
                readBodyToFile(response, output_stream, max_size)
            )
        except SynapseError:
            # This can happen e.g. because the body is too large.
            raise
        except Exception as e:
            raise SynapseError(502, ("Failed to download remote body: %s" % e)) from e

        return (
            length,
            resp_headers,
            response.request.absoluteURI.decode("ascii"),
            response.code,
        )
```

## `synapse/http/client.py` — `function:SimpleHttpClient.get_file` (lines 668-736)

```python
    async def get_file(
        self,
        url: str,
        output_stream: BinaryIO,
        max_size: Optional[int] = None,
        headers: Optional[RawHeaders] = None,
    ) -> Tuple[int, Dict[bytes, List[bytes]], str, int]:
        """GETs a file from a given URL
        Args:
            url: The URL to GET
            output_stream: File to write the response body to.
            headers: A map from header name to a list of values for that header
        Returns:
            A tuple of the file length, dict of the response
            headers, absolute URI of the response and HTTP response code.

        Raises:
            RequestTimedOutError: if there is a timeout before the response headers
               are received. Note there is currently no timeout on reading the response
               body.

            SynapseError: if the response is not a 2xx, the remote file is too large, or
               another exception happens during the download.
        """

        actual_headers = {b"User-Agent": [self.user_agent]}
        if headers:
            actual_headers.update(headers)  # type: ignore

        response = await self.request("GET", url, headers=Headers(actual_headers))

        resp_headers = dict(response.headers.getAllRawHeaders())

        if (
            b"Content-Length" in resp_headers
            and max_size
            and int(resp_headers[b"Content-Length"][0]) > max_size
        ):
            logger.warning("Requested URL is too large > %r bytes" % (max_size,))
            raise SynapseError(
                502,
                "Requested file is too large > %r bytes" % (max_size,),
                Codes.TOO_LARGE,
            )

        if response.code > 299:
            logger.warning("Got %d when downloading %s" % (response.code, url))
            raise SynapseError(502, "Got error %d" % (response.code,), Codes.UNKNOWN)

        # TODO: if our Content-Type is HTML or something, just read the first
        # N bytes into RAM rather than saving it all to disk only to read it
        # straight back in again

        try:
            length = await make_deferred_yieldable(
                readBodyToFile(response, output_stream, max_size)
            )
        except SynapseError:
            # This can happen e.g. because the body is too large.
            raise
        except Exception as e:
            raise SynapseError(502, ("Failed to download remote body: %s" % e)) from e

        return (
            length,
            resp_headers,
            response.request.absoluteURI.decode("ascii"),
            response.code,
        )
```

## `synapse/http/client.py` — `class:_ReadBodyToFileProtocol` (lines 751-782)

```python
class _ReadBodyToFileProtocol(protocol.Protocol):
    def __init__(
        self, stream: BinaryIO, deferred: defer.Deferred, max_size: Optional[int]
    ):
        self.stream = stream
        self.deferred = deferred
        self.length = 0
        self.max_size = max_size

    def dataReceived(self, data: bytes) -> None:
        self.stream.write(data)
        self.length += len(data)
        if self.max_size is not None and self.length >= self.max_size:
            self.deferred.errback(
                SynapseError(
                    502,
                    "Requested file is too large > %r bytes" % (self.max_size,),
                    Codes.TOO_LARGE,
                )
            )
            self.deferred = defer.Deferred()
            self.transport.loseConnection()

    def connectionLost(self, reason: Failure) -> None:
        if reason.check(ResponseDone):
            self.deferred.callback(self.length)
        elif reason.check(PotentialDataLoss):
            # stolen from https://github.com/twisted/treq/pull/49/files
            # http://twistedmatrix.com/trac/ticket/4840
            self.deferred.callback(self.length)
        else:
            self.deferred.errback(reason)
```

## `synapse/http/client.py` — `function:_ReadBodyToFileProtocol.__init__` (lines 752-758)

```python
    def __init__(
        self, stream: BinaryIO, deferred: defer.Deferred, max_size: Optional[int]
    ):
        self.stream = stream
        self.deferred = deferred
        self.length = 0
        self.max_size = max_size
```

## `synapse/http/client.py` — `function:_ReadBodyToFileProtocol.dataReceived` (lines 760-772)

```python
    def dataReceived(self, data: bytes) -> None:
        self.stream.write(data)
        self.length += len(data)
        if self.max_size is not None and self.length >= self.max_size:
            self.deferred.errback(
                SynapseError(
                    502,
                    "Requested file is too large > %r bytes" % (self.max_size,),
                    Codes.TOO_LARGE,
                )
            )
            self.deferred = defer.Deferred()
            self.transport.loseConnection()
```

## `synapse/http/client.py` — `function:readBodyToFile` (lines 785-802)

```python
def readBodyToFile(
    response: IResponse, stream: BinaryIO, max_size: Optional[int]
) -> defer.Deferred:
    """
    Read a HTTP response body to a file-object. Optionally enforcing a maximum file size.

    Args:
        response: The HTTP response to read from.
        stream: The file-object to write to.
        max_size: The maximum file size to allow.

    Returns:
        A Deferred which resolves to the length of the read body.
    """

    d = defer.Deferred()
    response.deliverBody(_ReadBodyToFileProtocol(stream, d, max_size))
    return d
```

## `synapse/http/federation/well_known_resolver.py` — `class:WellKnownResolver` (lines 78-270)

```python
class WellKnownResolver:
    """Handles well-known lookups for matrix servers.
    """

    def __init__(
        self,
        reactor: IReactorTime,
        agent: IAgent,
        user_agent: bytes,
        well_known_cache: Optional[TTLCache] = None,
        had_well_known_cache: Optional[TTLCache] = None,
    ):
        self._reactor = reactor
        self._clock = Clock(reactor)

        if well_known_cache is None:
            well_known_cache = _well_known_cache

        if had_well_known_cache is None:
            had_well_known_cache = _had_valid_well_known_cache

        self._well_known_cache = well_known_cache
        self._had_valid_well_known_cache = had_well_known_cache
        self._well_known_agent = RedirectAgent(agent)
        self.user_agent = user_agent

    async def get_well_known(self, server_name: bytes) -> WellKnownLookupResult:
        """Attempt to fetch and parse a .well-known file for the given server

        Args:
            server_name: name of the server, from the requested url

        Returns:
            The result of the lookup
        """
        try:
            prev_result, expiry, ttl = self._well_known_cache.get_with_expiry(
                server_name
            )

            now = self._clock.time()
            if now < expiry - WELL_KNOWN_GRACE_PERIOD_FACTOR * ttl:
                return WellKnownLookupResult(delegated_server=prev_result)
        except KeyError:
            prev_result = None

        # TODO: should we linearise so that we don't end up doing two .well-known
        # requests for the same server in parallel?
        try:
            with Measure(self._clock, "get_well_known"):
                result, cache_period = await self._fetch_well_known(
                    server_name
                )  # type: Optional[bytes], float

        except _FetchWellKnownFailure as e:
            if prev_result and e.temporary:
                # This is a temporary failure and we have a still valid cached
                # result, so lets return that. Hopefully the next time we ask
                # the remote will be back up again.
                return WellKnownLookupResult(delegated_server=prev_result)

            result = None

            if self._had_valid_well_known_cache.get(server_name, False):
                # We have recently seen a valid well-known record for this
                # server, so we cache the lack of well-known for a shorter time.
                cache_period = WELL_KNOWN_DOWN_CACHE_PERIOD
            else:
                cache_period = WELL_KNOWN_INVALID_CACHE_PERIOD

            # add some randomness to the TTL to avoid a stampeding herd
            cache_period *= random.uniform(
                1 - WELL_KNOWN_DEFAULT_CACHE_PERIOD_JITTER,
                1 + WELL_KNOWN_DEFAULT_CACHE_PERIOD_JITTER,
            )

        if cache_period > 0:
            self._well_known_cache.set(server_name, result, cache_period)

        return WellKnownLookupResult(delegated_server=result)

    async def _fetch_well_known(self, server_name: bytes) -> Tuple[bytes, float]:
        """Actually fetch and parse a .well-known, without checking the cache

        Args:
            server_name: name of the server, from the requested url

        Raises:
            _FetchWellKnownFailure if we fail to lookup a result

        Returns:
            The lookup result and cache period.
        """

        had_valid_well_known = self._had_valid_well_known_cache.get(server_name, False)

        # We do this in two steps to differentiate between possibly transient
        # errors (e.g. can't connect to host, 503 response) and more permanent
        # errors (such as getting a 404 response).
        response, body = await self._make_well_known_request(
            server_name, retry=had_valid_well_known
        )

        try:
            if response.code != 200:
                raise Exception("Non-200 response %s" % (response.code,))

            parsed_body = json_decoder.decode(body.decode("utf-8"))
            logger.info("Response from .well-known: %s", parsed_body)

            result = parsed_body["m.server"].encode("ascii")
        except defer.CancelledError:
            # Bail if we've been cancelled
            raise
        except Exception as e:
            logger.info("Error parsing well-known for %s: %s", server_name, e)
            raise _FetchWellKnownFailure(temporary=False)

        cache_period = _cache_period_from_headers(
            response.headers, time_now=self._reactor.seconds
        )
        if cache_period is None:
            cache_period = WELL_KNOWN_DEFAULT_CACHE_PERIOD
            # add some randomness to the TTL to avoid a stampeding herd every 24 hours
            # after startup
            cache_period *= random.uniform(
                1 - WELL_KNOWN_DEFAULT_CACHE_PERIOD_JITTER,
                1 + WELL_KNOWN_DEFAULT_CACHE_PERIOD_JITTER,
            )
        else:
            cache_period = min(cache_period, WELL_KNOWN_MAX_CACHE_PERIOD)
            cache_period = max(cache_period, WELL_KNOWN_MIN_CACHE_PERIOD)

        # We got a success, mark as such in the cache
        self._had_valid_well_known_cache.set(
            server_name,
            bool(result),
            cache_period + WELL_KNOWN_REMEMBER_DOMAIN_HAD_VALID,
        )

        return result, cache_period

    async def _make_well_known_request(
        self, server_name: bytes, retry: bool
    ) -> Tuple[IResponse, bytes]:
        """Make the well known request.

        This will retry the request if requested and it fails (with unable
        to connect or receives a 5xx error).

        Args:
            server_name: name of the server, from the requested url
            retry: Whether to retry the request if it fails.

        Returns:
            Returns the response object and body. Response may be a non-200 response.
        """
        uri = b"https://%s/.well-known/matrix/server" % (server_name,)
        uri_str = uri.decode("ascii")

        headers = {
            b"User-Agent": [self.user_agent],
        }

        i = 0
        while True:
            i += 1

            logger.info("Fetching %s", uri_str)
            try:
                response = await make_deferred_yieldable(
                    self._well_known_agent.request(
                        b"GET", uri, headers=Headers(headers)
                    )
                )
                body = await make_deferred_yieldable(readBody(response))

                if 500 <= response.code < 600:
                    raise Exception("Non-200 response %s" % (response.code,))

                return response, body
            except defer.CancelledError:
                # Bail if we've been cancelled
                raise
            except Exception as e:
                if not retry or i >= WELL_KNOWN_RETRY_ATTEMPTS:
                    logger.info("Error fetching %s: %s", uri_str, e)
                    raise _FetchWellKnownFailure(temporary=True)

                logger.info("Error fetching %s: %s. Retrying", uri_str, e)

            # Sleep briefly in the hopes that they come back up
            await self._clock.sleep(0.5)
```

## `synapse/http/federation/well_known_resolver.py` — `function:WellKnownResolver._make_well_known_request` (lines 220-270)

```python
    async def _make_well_known_request(
        self, server_name: bytes, retry: bool
    ) -> Tuple[IResponse, bytes]:
        """Make the well known request.

        This will retry the request if requested and it fails (with unable
        to connect or receives a 5xx error).

        Args:
            server_name: name of the server, from the requested url
            retry: Whether to retry the request if it fails.

        Returns:
            Returns the response object and body. Response may be a non-200 response.
        """
        uri = b"https://%s/.well-known/matrix/server" % (server_name,)
        uri_str = uri.decode("ascii")

        headers = {
            b"User-Agent": [self.user_agent],
        }

        i = 0
        while True:
            i += 1

            logger.info("Fetching %s", uri_str)
            try:
                response = await make_deferred_yieldable(
                    self._well_known_agent.request(
                        b"GET", uri, headers=Headers(headers)
                    )
                )
                body = await make_deferred_yieldable(readBody(response))

                if 500 <= response.code < 600:
                    raise Exception("Non-200 response %s" % (response.code,))

                return response, body
            except defer.CancelledError:
                # Bail if we've been cancelled
                raise
            except Exception as e:
                if not retry or i >= WELL_KNOWN_RETRY_ATTEMPTS:
                    logger.info("Error fetching %s: %s", uri_str, e)
                    raise _FetchWellKnownFailure(temporary=True)

                logger.info("Error fetching %s: %s. Retrying", uri_str, e)

            # Sleep briefly in the hopes that they come back up
            await self._clock.sleep(0.5)
```

## `synapse/http/federation/well_known_resolver.py` — `module:<module>@18` (lines 18-18)

```python
from typing import Callable, Dict, Optional, Tuple
```

## `synapse/http/federation/well_known_resolver.py` — `module:<module>@24` (lines 24-24)

```python
from twisted.web.client import RedirectAgent, readBody
```

## `synapse/http/federation/well_known_resolver.py` — `module:<module>@25` (lines 25-25)

```python
from twisted.web.http import stringToDatetime
```

## `synapse/http/federation/well_known_resolver.py` — `module:<module>@29` (lines 29-29)

```python
from synapse.logging.context import make_deferred_yieldable
```

## `synapse/http/matrixfederationclient.py` — `class:MatrixFederationHttpClient` (lines 209-999)

```python
class MatrixFederationHttpClient:
    """HTTP client used to talk to other homeservers over the federation
    protocol. Send client certificates and signs requests.

    Attributes:
        agent (twisted.web.client.Agent): The twisted Agent used to send the
            requests.
    """

    def __init__(self, hs, tls_client_options_factory):
        self.hs = hs
        self.signing_key = hs.signing_key
        self.server_name = hs.hostname

        # We need to use a DNS resolver which filters out blacklisted IP
        # addresses, to prevent DNS rebinding.
        self.reactor = BlacklistingReactorWrapper(
            hs.get_reactor(), None, hs.config.federation_ip_range_blacklist
        )

        user_agent = hs.version_string
        if hs.config.user_agent_suffix:
            user_agent = "%s %s" % (user_agent, hs.config.user_agent_suffix)
        user_agent = user_agent.encode("ascii")

        self.agent = MatrixFederationAgent(
            self.reactor,
            tls_client_options_factory,
            user_agent,
            hs.config.federation_ip_range_blacklist,
        )

        # Use a BlacklistingAgentWrapper to prevent circumventing the IP
        # blacklist via IP literals in server names
        self.agent = BlacklistingAgentWrapper(
            self.agent, ip_blacklist=hs.config.federation_ip_range_blacklist,
        )

        self.clock = hs.get_clock()
        self._store = hs.get_datastore()
        self.version_string_bytes = hs.version_string.encode("ascii")
        self.default_timeout = 60

        def schedule(x):
            self.reactor.callLater(_EPSILON, x)

        self._cooperator = Cooperator(scheduler=schedule)

    async def _send_request_with_optional_trailing_slash(
        self,
        request: MatrixFederationRequest,
        try_trailing_slash_on_400: bool = False,
        **send_request_args
    ) -> IResponse:
        """Wrapper for _send_request which can optionally retry the request
        upon receiving a combination of a 400 HTTP response code and a
        'M_UNRECOGNIZED' errcode. This is a workaround for Synapse <= v0.99.3
        due to #3622.

        Args:
            request: details of request to be sent
            try_trailing_slash_on_400: Whether on receiving a 400
                'M_UNRECOGNIZED' from the server to retry the request with a
                trailing slash appended to the request path.
            send_request_args: A dictionary of arguments to pass to `_send_request()`.

        Raises:
            HttpResponseException: If we get an HTTP response code >= 300
                (except 429).

        Returns:
            Parsed JSON response body.
        """
        try:
            response = await self._send_request(request, **send_request_args)
        except HttpResponseException as e:
            # Received an HTTP error > 300. Check if it meets the requirements
            # to retry with a trailing slash
            if not try_trailing_slash_on_400:
                raise

            if e.code != 400 or e.to_synapse_error().errcode != "M_UNRECOGNIZED":
                raise

            # Retry with a trailing slash if we received a 400 with
            # 'M_UNRECOGNIZED' which some endpoints can return when omitting a
            # trailing slash on Synapse <= v0.99.3.
            logger.info("Retrying request with trailing slash")

            # Request is frozen so we create a new instance
            request = attr.evolve(request, path=request.path + "/")

            response = await self._send_request(request, **send_request_args)

        return response

    async def _send_request(
        self,
        request: MatrixFederationRequest,
        retry_on_dns_fail: bool = True,
        timeout: Optional[int] = None,
        long_retries: bool = False,
        ignore_backoff: bool = False,
        backoff_on_404: bool = False,
    ) -> IResponse:
        """
        Sends a request to the given server.

        Args:
            request: details of request to be sent

            retry_on_dns_fail: true if the request should be retied on DNS failures

            timeout: number of milliseconds to wait for the response headers
                (including connecting to the server), *for each attempt*.
                60s by default.

            long_retries: whether to use the long retry algorithm.

                The regular retry algorithm makes 4 attempts, with intervals
                [0.5s, 1s, 2s].

                The long retry algorithm makes 11 attempts, with intervals
                [4s, 16s, 60s, 60s, ...]

                Both algorithms add -20%/+40% jitter to the retry intervals.

                Note that the above intervals are *in addition* to the time spent
                waiting for the request to complete (up to `timeout` ms).

                NB: the long retry algorithm takes over 20 minutes to complete, with
                a default timeout of 60s!

            ignore_backoff: true to ignore the historical backoff data
                and try the request anyway.

            backoff_on_404: Back off if we get a 404

        Returns:
            Resolves with the HTTP response object on success.

        Raises:
            HttpResponseException: If we get an HTTP response code >= 300
                (except 429).
            NotRetryingDestination: If we are not yet ready to retry this
                server.
            FederationDeniedError: If this destination  is not on our
                federation whitelist
            RequestSendFailed: If there were problems connecting to the
                remote, due to e.g. DNS failures, connection timeouts etc.
        """
        if timeout:
            _sec_timeout = timeout / 1000
        else:
            _sec_timeout = self.default_timeout

        if (
            self.hs.config.federation_domain_whitelist is not None
            and request.destination not in self.hs.config.federation_domain_whitelist
        ):
            raise FederationDeniedError(request.destination)

        limiter = await synapse.util.retryutils.get_retry_limiter(
            request.destination,
            self.clock,
            self._store,
            backoff_on_404=backoff_on_404,
            ignore_backoff=ignore_backoff,
        )

        method_bytes = request.method.encode("ascii")
        destination_bytes = request.destination.encode("ascii")
        path_bytes = request.path.encode("ascii")
        if request.query:
            query_bytes = encode_query_args(request.query)
        else:
            query_bytes = b""

        scope = start_active_span(
            "outgoing-federation-request",
            tags={
                tags.SPAN_KIND: tags.SPAN_KIND_RPC_CLIENT,
                tags.PEER_ADDRESS: request.destination,
                tags.HTTP_METHOD: request.method,
                tags.HTTP_URL: request.path,
            },
            finish_on_close=True,
        )

        # Inject the span into the headers
        headers_dict = {}  # type: Dict[bytes, List[bytes]]
        inject_active_span_byte_dict(headers_dict, request.destination)

        headers_dict[b"User-Agent"] = [self.version_string_bytes]

        with limiter, scope:
            # XXX: Would be much nicer to retry only at the transaction-layer
            # (once we have reliable transactions in place)
            if long_retries:
                retries_left = MAX_LONG_RETRIES
            else:
                retries_left = MAX_SHORT_RETRIES

            url_bytes = request.uri
            url_str = url_bytes.decode("ascii")

            url_to_sign_bytes = urllib.parse.urlunparse(
                (b"", b"", path_bytes, None, query_bytes, b"")
            )

            while True:
                try:
                    json = request.get_json()
                    if json:
                        headers_dict[b"Content-Type"] = [b"application/json"]
                        auth_headers = self.build_auth_headers(
                            destination_bytes, method_bytes, url_to_sign_bytes, json
                        )
                        data = encode_canonical_json(json)
                        producer = QuieterFileBodyProducer(
                            BytesIO(data), cooperator=self._cooperator
                        )  # type: Optional[IBodyProducer]
                    else:
                        producer = None
                        auth_headers = self.build_auth_headers(
                            destination_bytes, method_bytes, url_to_sign_bytes
                        )

                    headers_dict[b"Authorization"] = auth_headers

                    logger.debug(
                        "{%s} [%s] Sending request: %s %s; timeout %fs",
                        request.txn_id,
                        request.destination,
                        request.method,
                        url_str,
                        _sec_timeout,
                    )

                    outgoing_requests_counter.labels(request.method).inc()

                    try:
                        with Measure(self.clock, "outbound_request"):
                            # we don't want all the fancy cookie and redirect handling
                            # that treq.request gives: just use the raw Agent.
                            request_deferred = self.agent.request(
                                method_bytes,
                                url_bytes,
                                headers=Headers(headers_dict),
                                bodyProducer=producer,
                            )

                            request_deferred = timeout_deferred(
                                request_deferred,
                                timeout=_sec_timeout,
                                reactor=self.reactor,
                            )

                            response = await request_deferred
                    except DNSLookupError as e:
                        raise RequestSendFailed(e, can_retry=retry_on_dns_fail) from e
                    except Exception as e:
                        raise RequestSendFailed(e, can_retry=True) from e

                    incoming_responses_counter.labels(
                        request.method, response.code
                    ).inc()

                    set_tag(tags.HTTP_STATUS_CODE, response.code)
                    response_phrase = response.phrase.decode("ascii", errors="replace")

                    if 200 <= response.code < 300:
                        logger.debug(
                            "{%s} [%s] Got response headers: %d %s",
                            request.txn_id,
                            request.destination,
                            response.code,
                            response_phrase,
                        )
                        pass
                    else:
                        logger.info(
                            "{%s} [%s] Got response headers: %d %s",
                            request.txn_id,
                            request.destination,
                            response.code,
                            response_phrase,
                        )
                        # :'(
                        # Update transactions table?
                        d = treq.content(response)
                        d = timeout_deferred(
                            d, timeout=_sec_timeout, reactor=self.reactor
                        )

                        try:
                            body = await make_deferred_yieldable(d)
                        except Exception as e:
                            # Eh, we're already going to raise an exception so lets
                            # ignore if this fails.
                            logger.warning(
                                "{%s} [%s] Failed to get error response: %s %s: %s",
                                request.txn_id,
                                request.destination,
                                request.method,
                                url_str,
                                _flatten_response_never_received(e),
                            )
                            body = None

                        exc = HttpResponseException(
                            response.code, response_phrase, body
                        )

                        # Retry if the error is a 429 (Too Many Requests),
                        # otherwise just raise a standard HttpResponseException
                        if response.code == 429:
                            raise RequestSendFailed(exc, can_retry=True) from exc
                        else:
                            raise exc

                    break
                except RequestSendFailed as e:
                    logger.info(
                        "{%s} [%s] Request failed: %s %s: %s",
                        request.txn_id,
                        request.destination,
                        request.method,
                        url_str,
                        _flatten_response_never_received(e.inner_exception),
                    )

                    if not e.can_retry:
                        raise

                    if retries_left and not timeout:
                        if long_retries:
                            delay = 4 ** (MAX_LONG_RETRIES + 1 - retries_left)
                            delay = min(delay, 60)
                            delay *= random.uniform(0.8, 1.4)
                        else:
                            delay = 0.5 * 2 ** (MAX_SHORT_RETRIES - retries_left)
                            delay = min(delay, 2)
                            delay *= random.uniform(0.8, 1.4)

                        logger.debug(
                            "{%s} [%s] Waiting %ss before re-sending...",
                            request.txn_id,
                            request.destination,
                            delay,
                        )

                        await self.clock.sleep(delay)
                        retries_left -= 1
                    else:
                        raise

                except Exception as e:
                    logger.warning(
                        "{%s} [%s] Request failed: %s %s: %s",
                        request.txn_id,
                        request.destination,
                        request.method,
                        url_str,
                        _flatten_response_never_received(e),
                    )
                    raise
        return response

    def build_auth_headers(
        self,
        destination: Optional[bytes],
        method: bytes,
        url_bytes: bytes,
        content: Optional[JsonDict] = None,
        destination_is: Optional[bytes] = None,
    ) -> List[bytes]:
        """
        Builds the Authorization headers for a federation request
        Args:
            destination: The destination homeserver of the request.
                May be None if the destination is an identity server, in which case
                destination_is must be non-None.
            method: The HTTP method of the request
            url_bytes: The URI path of the request
            content: The body of the request
            destination_is: As 'destination', but if the destination is an
                identity server

        Returns:
            A list of headers to be added as "Authorization:" headers
        """
        request = {
            "method": method.decode("ascii"),
            "uri": url_bytes.decode("ascii"),
            "origin": self.server_name,
        }

        if destination is not None:
            request["destination"] = destination.decode("ascii")

        if destination_is is not None:
            request["destination_is"] = destination_is.decode("ascii")

        if content is not None:
            request["content"] = content

        request = sign_json(request, self.server_name, self.signing_key)

        auth_headers = []

        for key, sig in request["signatures"][self.server_name].items():
            auth_headers.append(
                (
                    'X-Matrix origin=%s,key="%s",sig="%s"'
                    % (self.server_name, key, sig)
                ).encode("ascii")
            )
        return auth_headers

    async def put_json(
        self,
        destination: str,
        path: str,
        args: Optional[QueryArgs] = None,
        data: Optional[JsonDict] = None,
        json_data_callback: Optional[Callable[[], JsonDict]] = None,
        long_retries: bool = False,
        timeout: Optional[int] = None,
        ignore_backoff: bool = False,
        backoff_on_404: bool = False,
        try_trailing_slash_on_400: bool = False,
    ) -> Union[JsonDict, list]:
        """ Sends the specified json data using PUT

        Args:
            destination: The remote server to send the HTTP request to.
            path: The HTTP path.
            args: query params
            data: A dict containing the data that will be used as
                the request body. This will be encoded as JSON.
            json_data_callback: A callable returning the dict to
                use as the request body.

            long_retries: whether to use the long retry algorithm. See
                docs on _send_request for details.

            timeout: number of milliseconds to wait for the response.
                self._default_timeout (60s) by default.

                Note that we may make several attempts to send the request; this
                timeout applies to the time spent waiting for response headers for
                *each* attempt (including connection time) as well as the time spent
                reading the response body after a 200 response.

            ignore_backoff: true to ignore the historical backoff data
                and try the request anyway.
            backoff_on_404: True if we should count a 404 response as
                a failure of the server (and should therefore back off future
                requests).
            try_trailing_slash_on_400: True if on a 400 M_UNRECOGNIZED
                response we should try appending a trailing slash to the end
                of the request. Workaround for #3622 in Synapse <= v0.99.3. This
                will be attempted before backing off if backing off has been
                enabled.

        Returns:
            Succeeds when we get a 2xx HTTP response. The
            result will be the decoded JSON body.

        Raises:
            HttpResponseException: If we get an HTTP response code >= 300
                (except 429).
            NotRetryingDestination: If we are not yet ready to retry this
                server.
            FederationDeniedError: If this destination  is not on our
                federation whitelist
            RequestSendFailed: If there were problems connecting to the
                remote, due to e.g. DNS failures, connection timeouts etc.
        """
        request = MatrixFederationRequest(
            method="PUT",
            destination=destination,
            path=path,
            query=args,
            json_callback=json_data_callback,
            json=data,
        )

        start_ms = self.clock.time_msec()

        response = await self._send_request_with_optional_trailing_slash(
            request,
            try_trailing_slash_on_400,
            backoff_on_404=backoff_on_404,
            ignore_backoff=ignore_backoff,
            long_retries=long_retries,
            timeout=timeout,
        )

        if timeout is not None:
            _sec_timeout = timeout / 1000
        else:
            _sec_timeout = self.default_timeout

        body = await _handle_json_response(
            self.reactor, _sec_timeout, request, response, start_ms
        )

        return body

    async def post_json(
        self,
        destination: str,
        path: str,
        data: Optional[JsonDict] = None,
        long_retries: bool = False,
        timeout: Optional[int] = None,
        ignore_backoff: bool = False,
        args: Optional[QueryArgs] = None,
    ) -> Union[JsonDict, list]:
        """ Sends the specified json data using POST

        Args:
            destination: The remote server to send the HTTP request to.

            path: The HTTP path.

            data: A dict containing the data that will be used as
                the request body. This will be encoded as JSON.

            long_retries: whether to use the long retry algorithm. See
                docs on _send_request for details.

            timeout: number of milliseconds to wait for the response.
                self._default_timeout (60s) by default.

                Note that we may make several attempts to send the request; this
                timeout applies to the time spent waiting for response headers for
                *each* attempt (including connection time) as well as the time spent
                reading the response body after a 200 response.

            ignore_backoff: true to ignore the historical backoff data and
                try the request anyway.

            args: query params
        Returns:
            dict|list: Succeeds when we get a 2xx HTTP response. The
            result will be the decoded JSON body.

        Raises:
            HttpResponseException: If we get an HTTP response code >= 300
                (except 429).
            NotRetryingDestination: If we are not yet ready to retry this
                server.
            FederationDeniedError: If this destination  is not on our
                federation whitelist
            RequestSendFailed: If there were problems connecting to the
                remote, due to e.g. DNS failures, connection timeouts etc.
        """

        request = MatrixFederationRequest(
            method="POST", destination=destination, path=path, query=args, json=data
        )

        start_ms = self.clock.time_msec()

        response = await self._send_request(
            request,
            long_retries=long_retries,
            timeout=timeout,
            ignore_backoff=ignore_backoff,
        )

        if timeout:
            _sec_timeout = timeout / 1000
        else:
            _sec_timeout = self.default_timeout

        body = await _handle_json_response(
            self.reactor, _sec_timeout, request, response, start_ms,
        )
        return body

    async def get_json(
        self,
        destination: str,
        path: str,
        args: Optional[QueryArgs] = None,
        retry_on_dns_fail: bool = True,
        timeout: Optional[int] = None,
        ignore_backoff: bool = False,
        try_trailing_slash_on_400: bool = False,
    ) -> Union[JsonDict, list]:
        """ GETs some json from the given host homeserver and path

        Args:
            destination: The remote server to send the HTTP request to.

            path: The HTTP path.

            args: A dictionary used to create query strings, defaults to
                None.

            timeout: number of milliseconds to wait for the response.
                self._default_timeout (60s) by default.

                Note that we may make several attempts to send the request; this
                timeout applies to the time spent waiting for response headers for
                *each* attempt (including connection time) as well as the time spent
                reading the response body after a 200 response.

            ignore_backoff: true to ignore the historical backoff data
                and try the request anyway.

            try_trailing_slash_on_400: True if on a 400 M_UNRECOGNIZED
                response we should try appending a trailing slash to the end of
                the request. Workaround for #3622 in Synapse <= v0.99.3.
        Returns:
            Succeeds when we get a 2xx HTTP response. The
            result will be the decoded JSON body.

        Raises:
            HttpResponseException: If we get an HTTP response code >= 300
                (except 429).
            NotRetryingDestination: If we are not yet ready to retry this
                server.
            FederationDeniedError: If this destination  is not on our
                federation whitelist
            RequestSendFailed: If there were problems connecting to the
                remote, due to e.g. DNS failures, connection timeouts etc.
        """
        request = MatrixFederationRequest(
            method="GET", destination=destination, path=path, query=args
        )

        start_ms = self.clock.time_msec()

        response = await self._send_request_with_optional_trailing_slash(
            request,
            try_trailing_slash_on_400,
            backoff_on_404=False,
            ignore_backoff=ignore_backoff,
            retry_on_dns_fail=retry_on_dns_fail,
            timeout=timeout,
        )

        if timeout is not None:
            _sec_timeout = timeout / 1000
        else:
            _sec_timeout = self.default_timeout

        body = await _handle_json_response(
            self.reactor, _sec_timeout, request, response, start_ms
        )

        return body

    async def delete_json(
        self,
        destination: str,
        path: str,
        long_retries: bool = False,
        timeout: Optional[int] = None,
        ignore_backoff: bool = False,
        args: Optional[QueryArgs] = None,
    ) -> Union[JsonDict, list]:
        """Send a DELETE request to the remote expecting some json response

        Args:
            destination: The remote server to send the HTTP request to.
            path: The HTTP path.

            long_retries: whether to use the long retry algorithm. See
                docs on _send_request for details.

            timeout: number of milliseconds to wait for the response.
                self._default_timeout (60s) by default.

                Note that we may make several attempts to send the request; this
                timeout applies to the time spent waiting for response headers for
                *each* attempt (including connection time) as well as the time spent
                reading the response body after a 200 response.

            ignore_backoff: true to ignore the historical backoff data and
                try the request anyway.

            args: query params
        Returns:
            Succeeds when we get a 2xx HTTP response. The
            result will be the decoded JSON body.

        Raises:
            HttpResponseException: If we get an HTTP response code >= 300
                (except 429).
            NotRetryingDestination: If we are not yet ready to retry this
                server.
            FederationDeniedError: If this destination  is not on our
                federation whitelist
            RequestSendFailed: If there were problems connecting to the
                remote, due to e.g. DNS failures, connection timeouts etc.
        """
        request = MatrixFederationRequest(
            method="DELETE", destination=destination, path=path, query=args
        )

        start_ms = self.clock.time_msec()

        response = await self._send_request(
            request,
            long_retries=long_retries,
            timeout=timeout,
            ignore_backoff=ignore_backoff,
        )

        if timeout is not None:
            _sec_timeout = timeout / 1000
        else:
            _sec_timeout = self.default_timeout

        body = await _handle_json_response(
            self.reactor, _sec_timeout, request, response, start_ms
        )
        return body

    async def get_file(
        self,
        destination: str,
        path: str,
        output_stream,
        args: Optional[QueryArgs] = None,
        retry_on_dns_fail: bool = True,
        max_size: Optional[int] = None,
        ignore_backoff: bool = False,
    ) -> Tuple[int, Dict[bytes, List[bytes]]]:
        """GETs a file from a given homeserver
        Args:
            destination: The remote server to send the HTTP request to.
            path: The HTTP path to GET.
            output_stream: File to write the response body to.
            args: Optional dictionary used to create the query string.
            ignore_backoff: true to ignore the historical backoff data
                and try the request anyway.

        Returns:
            Resolves with an (int,dict) tuple of
            the file length and a dict of the response headers.

        Raises:
            HttpResponseException: If we get an HTTP response code >= 300
                (except 429).
            NotRetryingDestination: If we are not yet ready to retry this
                server.
            FederationDeniedError: If this destination  is not on our
                federation whitelist
            RequestSendFailed: If there were problems connecting to the
                remote, due to e.g. DNS failures, connection timeouts etc.
        """
        request = MatrixFederationRequest(
            method="GET", destination=destination, path=path, query=args
        )

        response = await self._send_request(
            request, retry_on_dns_fail=retry_on_dns_fail, ignore_backoff=ignore_backoff
        )

        headers = dict(response.headers.getAllRawHeaders())

        try:
            d = readBodyToFile(response, output_stream, max_size)
            d.addTimeout(self.default_timeout, self.reactor)
            length = await make_deferred_yieldable(d)
        except Exception as e:
            logger.warning(
                "{%s} [%s] Error reading response: %s",
                request.txn_id,
                request.destination,
                e,
            )
            raise
        logger.info(
            "{%s} [%s] Completed: %d %s [%d bytes] %s %s",
            request.txn_id,
            request.destination,
            response.code,
            response.phrase.decode("ascii", errors="replace"),
            length,
            request.method,
            request.uri.decode("ascii"),
        )
        return (length, headers)
```

## `synapse/http/matrixfederationclient.py` — `function:MatrixFederationHttpClient.get_file` (lines 934-999)

```python
    async def get_file(
        self,
        destination: str,
        path: str,
        output_stream,
        args: Optional[QueryArgs] = None,
        retry_on_dns_fail: bool = True,
        max_size: Optional[int] = None,
        ignore_backoff: bool = False,
    ) -> Tuple[int, Dict[bytes, List[bytes]]]:
        """GETs a file from a given homeserver
        Args:
            destination: The remote server to send the HTTP request to.
            path: The HTTP path to GET.
            output_stream: File to write the response body to.
            args: Optional dictionary used to create the query string.
            ignore_backoff: true to ignore the historical backoff data
                and try the request anyway.

        Returns:
            Resolves with an (int,dict) tuple of
            the file length and a dict of the response headers.

        Raises:
            HttpResponseException: If we get an HTTP response code >= 300
                (except 429).
            NotRetryingDestination: If we are not yet ready to retry this
                server.
            FederationDeniedError: If this destination  is not on our
                federation whitelist
            RequestSendFailed: If there were problems connecting to the
                remote, due to e.g. DNS failures, connection timeouts etc.
        """
        request = MatrixFederationRequest(
            method="GET", destination=destination, path=path, query=args
        )

        response = await self._send_request(
            request, retry_on_dns_fail=retry_on_dns_fail, ignore_backoff=ignore_backoff
        )

        headers = dict(response.headers.getAllRawHeaders())

        try:
            d = readBodyToFile(response, output_stream, max_size)
            d.addTimeout(self.default_timeout, self.reactor)
            length = await make_deferred_yieldable(d)
        except Exception as e:
            logger.warning(
                "{%s} [%s] Error reading response: %s",
                request.txn_id,
                request.destination,
                e,
            )
            raise
        logger.info(
            "{%s} [%s] Completed: %d %s [%d bytes] %s %s",
            request.txn_id,
            request.destination,
            response.code,
            response.phrase.decode("ascii", errors="replace"),
            length,
            request.method,
            request.uri.decode("ascii"),
        )
        return (length, headers)
```

## `synapse/http/matrixfederationclient.py` — `module:<module>@39` (lines 39-43)

```python
from synapse.api.errors import (
    FederationDeniedError,
    HttpResponseException,
    RequestSendFailed,
)
```

## `synapse/http/matrixfederationclient.py` — `module:<module>@45` (lines 45-50)

```python
from synapse.http.client import (
    BlacklistingAgentWrapper,
    BlacklistingReactorWrapper,
    encode_query_args,
    readBodyToFile,
)
```

## `tests/http/federation/test_matrix_federation_agent.py` — `class:MatrixFederationAgentTests` (lines 79-1165)

```python
class MatrixFederationAgentTests(unittest.TestCase):
    def setUp(self):
        self.reactor = ThreadedMemoryReactorClock()

        self.mock_resolver = Mock()

        config_dict = default_config("test", parse=False)
        config_dict["federation_custom_ca_list"] = [get_test_ca_cert_file()]

        self._config = config = HomeServerConfig()
        config.parse_config_dict(config_dict, "", "")

        self.tls_factory = FederationPolicyForHTTPS(config)

        self.well_known_cache = TTLCache("test_cache", timer=self.reactor.seconds)
        self.had_well_known_cache = TTLCache("test_cache", timer=self.reactor.seconds)
        self.well_known_resolver = WellKnownResolver(
            self.reactor,
            Agent(self.reactor, contextFactory=self.tls_factory),
            b"test-agent",
            well_known_cache=self.well_known_cache,
            had_well_known_cache=self.had_well_known_cache,
        )

        self.agent = MatrixFederationAgent(
            reactor=self.reactor,
            tls_client_options_factory=self.tls_factory,
            user_agent="test-agent",  # Note that this is unused since _well_known_resolver is provided.
            ip_blacklist=IPSet(),
            _srv_resolver=self.mock_resolver,
            _well_known_resolver=self.well_known_resolver,
        )

    def _make_connection(self, client_factory, expected_sni):
        """Builds a test server, and completes the outgoing client connection

        Returns:
            HTTPChannel: the test server
        """

        # build the test server
        server_tls_protocol = _build_test_server(get_connection_factory())

        # now, tell the client protocol factory to build the client protocol (it will be a
        # _WrappingProtocol, around a TLSMemoryBIOProtocol, around an
        # HTTP11ClientProtocol) and wire the output of said protocol up to the server via
        # a FakeTransport.
        #
        # Normally this would be done by the TCP socket code in Twisted, but we are
        # stubbing that out here.
        client_protocol = client_factory.buildProtocol(None)
        client_protocol.makeConnection(
            FakeTransport(server_tls_protocol, self.reactor, client_protocol)
        )

        # tell the server tls protocol to send its stuff back to the client, too
        server_tls_protocol.makeConnection(
            FakeTransport(client_protocol, self.reactor, server_tls_protocol)
        )

        # grab a hold of the TLS connection, in case it gets torn down
        server_tls_connection = server_tls_protocol._tlsConnection

        # fish the test server back out of the server-side TLS protocol.
        http_protocol = server_tls_protocol.wrappedProtocol

        # give the reactor a pump to get the TLS juices flowing.
        self.reactor.pump((0.1,))

        # check the SNI
        server_name = server_tls_connection.get_servername()
        self.assertEqual(
            server_name,
            expected_sni,
            "Expected SNI %s but got %s" % (expected_sni, server_name),
        )

        return http_protocol

    @defer.inlineCallbacks
    def _make_get_request(self, uri):
        """
        Sends a simple GET request via the agent, and checks its logcontext management
        """
        with LoggingContext("one") as context:
            fetch_d = self.agent.request(b"GET", uri)

            # Nothing happened yet
            self.assertNoResult(fetch_d)

            # should have reset logcontext to the sentinel
            _check_logcontext(SENTINEL_CONTEXT)

            try:
                fetch_res = yield fetch_d
                return fetch_res
            except Exception as e:
                logger.info("Fetch of %s failed: %s", uri.decode("ascii"), e)
                raise
            finally:
                _check_logcontext(context)

    def _handle_well_known_connection(
        self, client_factory, expected_sni, content, response_headers={}
    ):
        """Handle an outgoing HTTPs connection: wire it up to a server, check that the
        request is for a .well-known, and send the response.

        Args:
            client_factory (IProtocolFactory): outgoing connection
            expected_sni (bytes): SNI that we expect the outgoing connection to send
            content (bytes): content to send back as the .well-known
        Returns:
            HTTPChannel: server impl
        """
        # make the connection for .well-known
        well_known_server = self._make_connection(
            client_factory, expected_sni=expected_sni
        )
        # check the .well-known request and send a response
        self.assertEqual(len(well_known_server.requests), 1)
        request = well_known_server.requests[0]
        self.assertEqual(
            request.requestHeaders.getRawHeaders(b"user-agent"), [b"test-agent"]
        )
        self._send_well_known_response(request, content, headers=response_headers)
        return well_known_server

    def _send_well_known_response(self, request, content, headers={}):
        """Check that an incoming request looks like a valid .well-known request, and
        send back the response.
        """
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/.well-known/matrix/server")
        self.assertEqual(request.requestHeaders.getRawHeaders(b"host"), [b"testserv"])
        # send back a response
        for k, v in headers.items():
            request.setHeader(k, v)
        request.write(content)
        request.finish()

        self.reactor.pump((0.1,))

    def test_get(self):
        """
        happy-path test of a GET request with an explicit port
        """
        self.reactor.lookups["testserv"] = "1.2.3.4"
        test_d = self._make_get_request(b"matrix://testserv:8448/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        # Make sure treq is trying to connect
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients[0]
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 8448)

        # make a test server, and wire up the client
        http_server = self._make_connection(client_factory, expected_sni=b"testserv")

        self.assertEqual(len(http_server.requests), 1)
        request = http_server.requests[0]
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/foo/bar")
        self.assertEqual(
            request.requestHeaders.getRawHeaders(b"host"), [b"testserv:8448"]
        )
        self.assertEqual(
            request.requestHeaders.getRawHeaders(b"user-agent"), [b"test-agent"]
        )
        content = request.content.read()
        self.assertEqual(content, b"")

        # Deferred is still without a result
        self.assertNoResult(test_d)

        # send the headers
        request.responseHeaders.setRawHeaders(b"Content-Type", [b"application/json"])
        request.write("")

        self.reactor.pump((0.1,))

        response = self.successResultOf(test_d)

        # that should give us a Response object
        self.assertEqual(response.code, 200)

        # Send the body
        request.write('{ "a": 1 }'.encode("ascii"))
        request.finish()

        self.reactor.pump((0.1,))

        # check it can be read
        json = self.successResultOf(treq.json_content(response))
        self.assertEqual(json, {"a": 1})

    def test_get_ip_address(self):
        """
        Test the behaviour when the server name contains an explicit IP (with no port)
        """
        # there will be a getaddrinfo on the IP
        self.reactor.lookups["1.2.3.4"] = "1.2.3.4"

        test_d = self._make_get_request(b"matrix://1.2.3.4/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        # Make sure treq is trying to connect
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients[0]
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 8448)

        # make a test server, and wire up the client
        http_server = self._make_connection(client_factory, expected_sni=None)

        self.assertEqual(len(http_server.requests), 1)
        request = http_server.requests[0]
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/foo/bar")
        self.assertEqual(request.requestHeaders.getRawHeaders(b"host"), [b"1.2.3.4"])

        # finish the request
        request.finish()
        self.reactor.pump((0.1,))
        self.successResultOf(test_d)

    def test_get_ipv6_address(self):
        """
        Test the behaviour when the server name contains an explicit IPv6 address
        (with no port)
        """

        # there will be a getaddrinfo on the IP
        self.reactor.lookups["::1"] = "::1"

        test_d = self._make_get_request(b"matrix://[::1]/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        # Make sure treq is trying to connect
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients[0]
        self.assertEqual(host, "::1")
        self.assertEqual(port, 8448)

        # make a test server, and wire up the client
        http_server = self._make_connection(client_factory, expected_sni=None)

        self.assertEqual(len(http_server.requests), 1)
        request = http_server.requests[0]
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/foo/bar")
        self.assertEqual(request.requestHeaders.getRawHeaders(b"host"), [b"[::1]"])

        # finish the request
        request.finish()
        self.reactor.pump((0.1,))
        self.successResultOf(test_d)

    def test_get_ipv6_address_with_port(self):
        """
        Test the behaviour when the server name contains an explicit IPv6 address
        (with explicit port)
        """

        # there will be a getaddrinfo on the IP
        self.reactor.lookups["::1"] = "::1"

        test_d = self._make_get_request(b"matrix://[::1]:80/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        # Make sure treq is trying to connect
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients[0]
        self.assertEqual(host, "::1")
        self.assertEqual(port, 80)

        # make a test server, and wire up the client
        http_server = self._make_connection(client_factory, expected_sni=None)

        self.assertEqual(len(http_server.requests), 1)
        request = http_server.requests[0]
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/foo/bar")
        self.assertEqual(request.requestHeaders.getRawHeaders(b"host"), [b"[::1]:80"])

        # finish the request
        request.finish()
        self.reactor.pump((0.1,))
        self.successResultOf(test_d)

    def test_get_hostname_bad_cert(self):
        """
        Test the behaviour when the certificate on the server doesn't match the hostname
        """
        self.mock_resolver.resolve_service.side_effect = generate_resolve_service([])
        self.reactor.lookups["testserv1"] = "1.2.3.4"

        test_d = self._make_get_request(b"matrix://testserv1/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        # No SRV record lookup yet
        self.mock_resolver.resolve_service.assert_not_called()

        # there should be an attempt to connect on port 443 for the .well-known
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients[0]
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 443)

        # fonx the connection
        client_factory.clientConnectionFailed(None, Exception("nope"))

        # attemptdelay on the hostnameendpoint is 0.3, so takes that long before the
        # .well-known request fails.
        self.reactor.pump((0.4,))

        # now there should be a SRV lookup
        self.mock_resolver.resolve_service.assert_called_once_with(
            b"_matrix._tcp.testserv1"
        )

        # we should fall back to a direct connection
        self.assertEqual(len(clients), 2)
        (host, port, client_factory, _timeout, _bindAddress) = clients[1]
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 8448)

        # make a test server, and wire up the client
        http_server = self._make_connection(client_factory, expected_sni=b"testserv1")

        # there should be no requests
        self.assertEqual(len(http_server.requests), 0)

        # ... and the request should have failed
        e = self.failureResultOf(test_d, ResponseNeverReceived)
        failure_reason = e.value.reasons[0]
        self.assertIsInstance(failure_reason.value, VerificationError)

    def test_get_ip_address_bad_cert(self):
        """
        Test the behaviour when the server name contains an explicit IP, but
        the server cert doesn't cover it
        """
        # there will be a getaddrinfo on the IP
        self.reactor.lookups["1.2.3.5"] = "1.2.3.5"

        test_d = self._make_get_request(b"matrix://1.2.3.5/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        # Make sure treq is trying to connect
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients[0]
        self.assertEqual(host, "1.2.3.5")
        self.assertEqual(port, 8448)

        # make a test server, and wire up the client
        http_server = self._make_connection(client_factory, expected_sni=None)

        # there should be no requests
        self.assertEqual(len(http_server.requests), 0)

        # ... and the request should have failed
        e = self.failureResultOf(test_d, ResponseNeverReceived)
        failure_reason = e.value.reasons[0]
        self.assertIsInstance(failure_reason.value, VerificationError)

    def test_get_no_srv_no_well_known(self):
        """
        Test the behaviour when the server name has no port, no SRV, and no well-known
        """

        self.mock_resolver.resolve_service.side_effect = generate_resolve_service([])
        self.reactor.lookups["testserv"] = "1.2.3.4"

        test_d = self._make_get_request(b"matrix://testserv/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        # No SRV record lookup yet
        self.mock_resolver.resolve_service.assert_not_called()

        # there should be an attempt to connect on port 443 for the .well-known
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients[0]
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 443)

        # fonx the connection
        client_factory.clientConnectionFailed(None, Exception("nope"))

        # attemptdelay on the hostnameendpoint is 0.3, so  takes that long before the
        # .well-known request fails.
        self.reactor.pump((0.4,))

        # now there should be a SRV lookup
        self.mock_resolver.resolve_service.assert_called_once_with(
            b"_matrix._tcp.testserv"
        )

        # we should fall back to a direct connection
        self.assertEqual(len(clients), 2)
        (host, port, client_factory, _timeout, _bindAddress) = clients[1]
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 8448)

        # make a test server, and wire up the client
        http_server = self._make_connection(client_factory, expected_sni=b"testserv")

        self.assertEqual(len(http_server.requests), 1)
        request = http_server.requests[0]
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/foo/bar")
        self.assertEqual(request.requestHeaders.getRawHeaders(b"host"), [b"testserv"])

        # finish the request
        request.finish()
        self.reactor.pump((0.1,))
        self.successResultOf(test_d)

    def test_get_well_known(self):
        """Test the behaviour when the .well-known delegates elsewhere
        """

        self.mock_resolver.resolve_service.side_effect = generate_resolve_service([])
        self.reactor.lookups["testserv"] = "1.2.3.4"
        self.reactor.lookups["target-server"] = "1::f"

        test_d = self._make_get_request(b"matrix://testserv/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        # there should be an attempt to connect on port 443 for the .well-known
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients[0]
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 443)

        self._handle_well_known_connection(
            client_factory,
            expected_sni=b"testserv",
            content=b'{ "m.server": "target-server" }',
        )

        # there should be a SRV lookup
        self.mock_resolver.resolve_service.assert_called_once_with(
            b"_matrix._tcp.target-server"
        )

        # now we should get a connection to the target server
        self.assertEqual(len(clients), 2)
        (host, port, client_factory, _timeout, _bindAddress) = clients[1]
        self.assertEqual(host, "1::f")
        self.assertEqual(port, 8448)

        # make a test server, and wire up the client
        http_server = self._make_connection(
            client_factory, expected_sni=b"target-server"
        )

        self.assertEqual(len(http_server.requests), 1)
        request = http_server.requests[0]
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/foo/bar")
        self.assertEqual(
            request.requestHeaders.getRawHeaders(b"host"), [b"target-server"]
        )

        # finish the request
        request.finish()
        self.reactor.pump((0.1,))
        self.successResultOf(test_d)

        self.assertEqual(self.well_known_cache[b"testserv"], b"target-server")

        # check the cache expires
        self.reactor.pump((48 * 3600,))
        self.well_known_cache.expire()
        self.assertNotIn(b"testserv", self.well_known_cache)

    def test_get_well_known_redirect(self):
        """Test the behaviour when the server name has no port and no SRV record, but
        the .well-known has a 300 redirect
        """
        self.mock_resolver.resolve_service.side_effect = generate_resolve_service([])
        self.reactor.lookups["testserv"] = "1.2.3.4"
        self.reactor.lookups["target-server"] = "1::f"

        test_d = self._make_get_request(b"matrix://testserv/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        # there should be an attempt to connect on port 443 for the .well-known
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients.pop()
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 443)

        redirect_server = self._make_connection(
            client_factory, expected_sni=b"testserv"
        )

        # send a 302 redirect
        self.assertEqual(len(redirect_server.requests), 1)
        request = redirect_server.requests[0]
        request.redirect(b"https://testserv/even_better_known")
        request.finish()

        self.reactor.pump((0.1,))

        # now there should be another connection
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients.pop()
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 443)

        well_known_server = self._make_connection(
            client_factory, expected_sni=b"testserv"
        )

        self.assertEqual(len(well_known_server.requests), 1, "No request after 302")
        request = well_known_server.requests[0]
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/even_better_known")
        request.write(b'{ "m.server": "target-server" }')
        request.finish()

        self.reactor.pump((0.1,))

        # there should be a SRV lookup
        self.mock_resolver.resolve_service.assert_called_once_with(
            b"_matrix._tcp.target-server"
        )

        # now we should get a connection to the target server
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients[0]
        self.assertEqual(host, "1::f")
        self.assertEqual(port, 8448)

        # make a test server, and wire up the client
        http_server = self._make_connection(
            client_factory, expected_sni=b"target-server"
        )

        self.assertEqual(len(http_server.requests), 1)
        request = http_server.requests[0]
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/foo/bar")
        self.assertEqual(
            request.requestHeaders.getRawHeaders(b"host"), [b"target-server"]
        )

        # finish the request
        request.finish()
        self.reactor.pump((0.1,))
        self.successResultOf(test_d)

        self.assertEqual(self.well_known_cache[b"testserv"], b"target-server")

        # check the cache expires
        self.reactor.pump((48 * 3600,))
        self.well_known_cache.expire()
        self.assertNotIn(b"testserv", self.well_known_cache)

    def test_get_invalid_well_known(self):
        """
        Test the behaviour when the server name has an *invalid* well-known (and no SRV)
        """

        self.mock_resolver.resolve_service.side_effect = generate_resolve_service([])
        self.reactor.lookups["testserv"] = "1.2.3.4"

        test_d = self._make_get_request(b"matrix://testserv/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        # No SRV record lookup yet
        self.mock_resolver.resolve_service.assert_not_called()

        # there should be an attempt to connect on port 443 for the .well-known
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients.pop()
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 443)

        self._handle_well_known_connection(
            client_factory, expected_sni=b"testserv", content=b"NOT JSON"
        )

        # now there should be a SRV lookup
        self.mock_resolver.resolve_service.assert_called_once_with(
            b"_matrix._tcp.testserv"
        )

        # we should fall back to a direct connection
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients.pop()
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 8448)

        # make a test server, and wire up the client
        http_server = self._make_connection(client_factory, expected_sni=b"testserv")

        self.assertEqual(len(http_server.requests), 1)
        request = http_server.requests[0]
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/foo/bar")
        self.assertEqual(request.requestHeaders.getRawHeaders(b"host"), [b"testserv"])

        # finish the request
        request.finish()
        self.reactor.pump((0.1,))
        self.successResultOf(test_d)

    def test_get_well_known_unsigned_cert(self):
        """Test the behaviour when the .well-known server presents a cert
        not signed by a CA
        """

        # we use the same test server as the other tests, but use an agent with
        # the config left to the default, which will not trust it (since the
        # presented cert is signed by a test CA)

        self.mock_resolver.resolve_service.side_effect = generate_resolve_service([])
        self.reactor.lookups["testserv"] = "1.2.3.4"

        config = default_config("test", parse=True)

        # Build a new agent and WellKnownResolver with a different tls factory
        tls_factory = FederationPolicyForHTTPS(config)
        agent = MatrixFederationAgent(
            reactor=self.reactor,
            tls_client_options_factory=tls_factory,
            user_agent=b"test-agent",  # This is unused since _well_known_resolver is passed below.
            ip_blacklist=IPSet(),
            _srv_resolver=self.mock_resolver,
            _well_known_resolver=WellKnownResolver(
                self.reactor,
                Agent(self.reactor, contextFactory=tls_factory),
                b"test-agent",
                well_known_cache=self.well_known_cache,
                had_well_known_cache=self.had_well_known_cache,
            ),
        )

        test_d = agent.request(b"GET", b"matrix://testserv/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        # there should be an attempt to connect on port 443 for the .well-known
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients[0]
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 443)

        http_proto = self._make_connection(client_factory, expected_sni=b"testserv")

        # there should be no requests
        self.assertEqual(len(http_proto.requests), 0)

        # and there should be a SRV lookup instead
        self.mock_resolver.resolve_service.assert_called_once_with(
            b"_matrix._tcp.testserv"
        )

    def test_get_hostname_srv(self):
        """
        Test the behaviour when there is a single SRV record
        """
        self.mock_resolver.resolve_service.side_effect = generate_resolve_service(
            [Server(host=b"srvtarget", port=8443)]
        )
        self.reactor.lookups["srvtarget"] = "1.2.3.4"

        test_d = self._make_get_request(b"matrix://testserv/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        # the request for a .well-known will have failed with a DNS lookup error.
        self.mock_resolver.resolve_service.assert_called_once_with(
            b"_matrix._tcp.testserv"
        )

        # Make sure treq is trying to connect
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients[0]
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 8443)

        # make a test server, and wire up the client
        http_server = self._make_connection(client_factory, expected_sni=b"testserv")

        self.assertEqual(len(http_server.requests), 1)
        request = http_server.requests[0]
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/foo/bar")
        self.assertEqual(request.requestHeaders.getRawHeaders(b"host"), [b"testserv"])

        # finish the request
        request.finish()
        self.reactor.pump((0.1,))
        self.successResultOf(test_d)

    def test_get_well_known_srv(self):
        """Test the behaviour when the .well-known redirects to a place where there
        is a SRV.
        """
        self.reactor.lookups["testserv"] = "1.2.3.4"
        self.reactor.lookups["srvtarget"] = "5.6.7.8"

        test_d = self._make_get_request(b"matrix://testserv/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        # there should be an attempt to connect on port 443 for the .well-known
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients[0]
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 443)

        self.mock_resolver.resolve_service.side_effect = generate_resolve_service(
            [Server(host=b"srvtarget", port=8443)]
        )

        self._handle_well_known_connection(
            client_factory,
            expected_sni=b"testserv",
            content=b'{ "m.server": "target-server" }',
        )

        # there should be a SRV lookup
        self.mock_resolver.resolve_service.assert_called_once_with(
            b"_matrix._tcp.target-server"
        )

        # now we should get a connection to the target of the SRV record
        self.assertEqual(len(clients), 2)
        (host, port, client_factory, _timeout, _bindAddress) = clients[1]
        self.assertEqual(host, "5.6.7.8")
        self.assertEqual(port, 8443)

        # make a test server, and wire up the client
        http_server = self._make_connection(
            client_factory, expected_sni=b"target-server"
        )

        self.assertEqual(len(http_server.requests), 1)
        request = http_server.requests[0]
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/foo/bar")
        self.assertEqual(
            request.requestHeaders.getRawHeaders(b"host"), [b"target-server"]
        )

        # finish the request
        request.finish()
        self.reactor.pump((0.1,))
        self.successResultOf(test_d)

    def test_idna_servername(self):
        """test the behaviour when the server name has idna chars in"""

        self.mock_resolver.resolve_service.side_effect = generate_resolve_service([])

        # the resolver is always called with the IDNA hostname as a native string.
        self.reactor.lookups["xn--bcher-kva.com"] = "1.2.3.4"

        # this is idna for bücher.com
        test_d = self._make_get_request(b"matrix://xn--bcher-kva.com/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        # No SRV record lookup yet
        self.mock_resolver.resolve_service.assert_not_called()

        # there should be an attempt to connect on port 443 for the .well-known
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients[0]
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 443)

        # fonx the connection
        client_factory.clientConnectionFailed(None, Exception("nope"))

        # attemptdelay on the hostnameendpoint is 0.3, so  takes that long before the
        # .well-known request fails.
        self.reactor.pump((0.4,))

        # now there should have been a SRV lookup
        self.mock_resolver.resolve_service.assert_called_once_with(
            b"_matrix._tcp.xn--bcher-kva.com"
        )

        # We should fall back to port 8448
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 2)
        (host, port, client_factory, _timeout, _bindAddress) = clients[1]
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 8448)

        # make a test server, and wire up the client
        http_server = self._make_connection(
            client_factory, expected_sni=b"xn--bcher-kva.com"
        )

        self.assertEqual(len(http_server.requests), 1)
        request = http_server.requests[0]
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/foo/bar")
        self.assertEqual(
            request.requestHeaders.getRawHeaders(b"host"), [b"xn--bcher-kva.com"]
        )

        # finish the request
        request.finish()
        self.reactor.pump((0.1,))
        self.successResultOf(test_d)

    def test_idna_srv_target(self):
        """test the behaviour when the target of a SRV record has idna chars"""

        self.mock_resolver.resolve_service.side_effect = generate_resolve_service(
            [Server(host=b"xn--trget-3qa.com", port=8443)]  # târget.com
        )
        self.reactor.lookups["xn--trget-3qa.com"] = "1.2.3.4"

        test_d = self._make_get_request(b"matrix://xn--bcher-kva.com/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        self.mock_resolver.resolve_service.assert_called_once_with(
            b"_matrix._tcp.xn--bcher-kva.com"
        )

        # Make sure treq is trying to connect
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients[0]
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 8443)

        # make a test server, and wire up the client
        http_server = self._make_connection(
            client_factory, expected_sni=b"xn--bcher-kva.com"
        )

        self.assertEqual(len(http_server.requests), 1)
        request = http_server.requests[0]
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/foo/bar")
        self.assertEqual(
            request.requestHeaders.getRawHeaders(b"host"), [b"xn--bcher-kva.com"]
        )

        # finish the request
        request.finish()
        self.reactor.pump((0.1,))
        self.successResultOf(test_d)

    def test_well_known_cache(self):
        self.reactor.lookups["testserv"] = "1.2.3.4"

        fetch_d = defer.ensureDeferred(
            self.well_known_resolver.get_well_known(b"testserv")
        )

        # there should be an attempt to connect on port 443 for the .well-known
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients.pop(0)
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 443)

        well_known_server = self._handle_well_known_connection(
            client_factory,
            expected_sni=b"testserv",
            response_headers={b"Cache-Control": b"max-age=1000"},
            content=b'{ "m.server": "target-server" }',
        )

        r = self.successResultOf(fetch_d)
        self.assertEqual(r.delegated_server, b"target-server")

        # close the tcp connection
        well_known_server.loseConnection()

        # repeat the request: it should hit the cache
        fetch_d = defer.ensureDeferred(
            self.well_known_resolver.get_well_known(b"testserv")
        )
        r = self.successResultOf(fetch_d)
        self.assertEqual(r.delegated_server, b"target-server")

        # expire the cache
        self.reactor.pump((1000.0,))

        # now it should connect again
        fetch_d = defer.ensureDeferred(
            self.well_known_resolver.get_well_known(b"testserv")
        )

        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients.pop(0)
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 443)

        self._handle_well_known_connection(
            client_factory,
            expected_sni=b"testserv",
            content=b'{ "m.server": "other-server" }',
        )

        r = self.successResultOf(fetch_d)
        self.assertEqual(r.delegated_server, b"other-server")

    def test_well_known_cache_with_temp_failure(self):
        """Test that we refetch well-known before the cache expires, and that
        it ignores transient errors.
        """

        self.reactor.lookups["testserv"] = "1.2.3.4"

        fetch_d = defer.ensureDeferred(
            self.well_known_resolver.get_well_known(b"testserv")
        )

        # there should be an attempt to connect on port 443 for the .well-known
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients.pop(0)
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 443)

        well_known_server = self._handle_well_known_connection(
            client_factory,
            expected_sni=b"testserv",
            response_headers={b"Cache-Control": b"max-age=1000"},
            content=b'{ "m.server": "target-server" }',
        )

        r = self.successResultOf(fetch_d)
        self.assertEqual(r.delegated_server, b"target-server")

        # close the tcp connection
        well_known_server.loseConnection()

        # Get close to the cache expiry, this will cause the resolver to do
        # another lookup.
        self.reactor.pump((900.0,))

        fetch_d = defer.ensureDeferred(
            self.well_known_resolver.get_well_known(b"testserv")
        )

        # The resolver may retry a few times, so fonx all requests that come along
        attempts = 0
        while self.reactor.tcpClients:
            clients = self.reactor.tcpClients
            (host, port, client_factory, _timeout, _bindAddress) = clients.pop(0)

            attempts += 1

            # fonx the connection attempt, this will be treated as a temporary
            # failure.
            client_factory.clientConnectionFailed(None, Exception("nope"))

            # There's a few sleeps involved, so we have to pump the reactor a
            # bit.
            self.reactor.pump((1.0, 1.0))

        # We expect to see more than one attempt as there was previously a valid
        # well known.
        self.assertGreater(attempts, 1)

        # Resolver should return cached value, despite the lookup failing.
        r = self.successResultOf(fetch_d)
        self.assertEqual(r.delegated_server, b"target-server")

        # Expire both caches and repeat the request
        self.reactor.pump((10000.0,))

        # Repated the request, this time it should fail if the lookup fails.
        fetch_d = defer.ensureDeferred(
            self.well_known_resolver.get_well_known(b"testserv")
        )

        clients = self.reactor.tcpClients
        (host, port, client_factory, _timeout, _bindAddress) = clients.pop(0)
        client_factory.clientConnectionFailed(None, Exception("nope"))
        self.reactor.pump((0.4,))

        r = self.successResultOf(fetch_d)
        self.assertEqual(r.delegated_server, None)

    def test_srv_fallbacks(self):
        """Test that other SRV results are tried if the first one fails.
        """
        self.mock_resolver.resolve_service.side_effect = generate_resolve_service(
            [
                Server(host=b"target.com", port=8443),
                Server(host=b"target.com", port=8444),
            ]
        )
        self.reactor.lookups["target.com"] = "1.2.3.4"

        test_d = self._make_get_request(b"matrix://testserv/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        self.mock_resolver.resolve_service.assert_called_once_with(
            b"_matrix._tcp.testserv"
        )

        # We should see an attempt to connect to the first server
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients.pop(0)
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 8443)

        # Fonx the connection
        client_factory.clientConnectionFailed(None, Exception("nope"))

        # There's a 300ms delay in HostnameEndpoint
        self.reactor.pump((0.4,))

        # Hasn't failed yet
        self.assertNoResult(test_d)

        # We shouldnow see an attempt to connect to the second server
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients.pop(0)
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 8444)

        # make a test server, and wire up the client
        http_server = self._make_connection(client_factory, expected_sni=b"testserv")

        self.assertEqual(len(http_server.requests), 1)
        request = http_server.requests[0]
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/foo/bar")
        self.assertEqual(request.requestHeaders.getRawHeaders(b"host"), [b"testserv"])

        # finish the request
        request.finish()
        self.reactor.pump((0.1,))
        self.successResultOf(test_d)
```

## `tests/http/federation/test_matrix_federation_agent.py` — `function:MatrixFederationAgentTests.test_srv_fallbacks` (lines 1110-1165)

```python
    def test_srv_fallbacks(self):
        """Test that other SRV results are tried if the first one fails.
        """
        self.mock_resolver.resolve_service.side_effect = generate_resolve_service(
            [
                Server(host=b"target.com", port=8443),
                Server(host=b"target.com", port=8444),
            ]
        )
        self.reactor.lookups["target.com"] = "1.2.3.4"

        test_d = self._make_get_request(b"matrix://testserv/foo/bar")

        # Nothing happened yet
        self.assertNoResult(test_d)

        self.mock_resolver.resolve_service.assert_called_once_with(
            b"_matrix._tcp.testserv"
        )

        # We should see an attempt to connect to the first server
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients.pop(0)
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 8443)

        # Fonx the connection
        client_factory.clientConnectionFailed(None, Exception("nope"))

        # There's a 300ms delay in HostnameEndpoint
        self.reactor.pump((0.4,))

        # Hasn't failed yet
        self.assertNoResult(test_d)

        # We shouldnow see an attempt to connect to the second server
        clients = self.reactor.tcpClients
        self.assertEqual(len(clients), 1)
        (host, port, client_factory, _timeout, _bindAddress) = clients.pop(0)
        self.assertEqual(host, "1.2.3.4")
        self.assertEqual(port, 8444)

        # make a test server, and wire up the client
        http_server = self._make_connection(client_factory, expected_sni=b"testserv")

        self.assertEqual(len(http_server.requests), 1)
        request = http_server.requests[0]
        self.assertEqual(request.method, b"GET")
        self.assertEqual(request.path, b"/foo/bar")
        self.assertEqual(request.requestHeaders.getRawHeaders(b"host"), [b"testserv"])

        # finish the request
        request.finish()
        self.reactor.pump((0.1,))
        self.successResultOf(test_d)
```

## `tests/http/federation/test_matrix_federation_agent.py` — `module:<module>@38` (lines 38-41)

```python
from synapse.http.federation.well_known_resolver import (
    WellKnownResolver,
    _cache_period_from_headers,
)
```
