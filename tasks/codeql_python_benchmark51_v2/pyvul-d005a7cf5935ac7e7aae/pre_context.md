# Patch-overlapping Python context before the fix

## `jupyter_server/base/handlers.py` — `class:APIHandler` (lines 726-832)

```python
class APIHandler(JupyterHandler):
    """Base class for API handlers"""

    async def prepare(self) -> None:
        """Prepare an API response."""
        await super().prepare()
        if not self.check_origin():
            raise web.HTTPError(404)

    def write_error(self, status_code: int, **kwargs: Any) -> None:
        """APIHandler errors are JSON, not human pages"""
        self.set_header("Content-Type", "application/json")
        message = responses.get(status_code, "Unknown HTTP Error")
        reply: dict[str, Any] = {
            "message": message,
        }
        exc_info = kwargs.get("exc_info")
        if exc_info:
            e = exc_info[1]
            if isinstance(e, HTTPError):
                reply["message"] = e.log_message or message
                reply["reason"] = e.reason
            else:
                reply["message"] = "Unhandled error"
                reply["reason"] = None
                reply["traceback"] = "".join(traceback.format_exception(*exc_info))
        self.log.warning("wrote error: %r", reply["message"], exc_info=True)
        self.finish(json.dumps(reply))

    def get_login_url(self) -> str:
        """Get the login url."""
        # if get_login_url is invoked in an API handler,
        # that means @web.authenticated is trying to trigger a redirect.
        # instead of redirecting, raise 403 instead.
        if not self.current_user:
            raise web.HTTPError(403)
        return super().get_login_url()

    @property
    def content_security_policy(self) -> str:
        csp = "; ".join(
            [
                super().content_security_policy,
                "default-src 'none'",
            ]
        )
        return csp

    # set _track_activity = False on API handlers that shouldn't track activity
    _track_activity = True

    def update_api_activity(self) -> None:
        """Update last_activity of API requests"""
        # record activity of authenticated requests
        if (
            self._track_activity
            and getattr(self, "_jupyter_current_user", None)
            and self.get_argument("no_track_activity", None) is None
        ):
            self.settings["api_last_activity"] = utcnow()

    def finish(self, *args: Any, **kwargs: Any) -> Future[Any]:
        """Finish an API response."""
        self.update_api_activity()
        # Allow caller to indicate content-type...
        set_content_type = kwargs.pop("set_content_type", "application/json")
        self.set_header("Content-Type", set_content_type)
        return super().finish(*args, **kwargs)

    def options(self, *args: Any, **kwargs: Any) -> None:
        """Get the options."""
        if "Access-Control-Allow-Headers" in self.settings.get("headers", {}):
            self.set_header(
                "Access-Control-Allow-Headers",
                self.settings["headers"]["Access-Control-Allow-Headers"],
            )
        else:
            self.set_header(
                "Access-Control-Allow-Headers",
                "accept, content-type, authorization, x-xsrftoken",
            )
        self.set_header("Access-Control-Allow-Methods", "GET, PUT, POST, PATCH, DELETE, OPTIONS")

        # if authorization header is requested,
        # that means the request is token-authenticated.
        # avoid browser-side rejection of the preflight request.
        # only allow this exception if allow_origin has not been specified
        # and Jupyter server authentication is enabled.
        # If the token is not valid, the 'real' request will still be rejected.
        requested_headers = self.request.headers.get("Access-Control-Request-Headers", "").split(
            ","
        )
        if (
            requested_headers
            and any(h.strip().lower() == "authorization" for h in requested_headers)
            and (
                # FIXME: it would be even better to check specifically for token-auth,
                # but there is currently no API for this.
                self.login_available
            )
            and (
                self.allow_origin
                or self.allow_origin_pat
                or "Access-Control-Allow-Origin" in self.settings.get("headers", {})
            )
        ):
            self.set_header("Access-Control-Allow-Origin", self.request.headers.get("Origin", ""))
```

## `jupyter_server/base/handlers.py` — `function:APIHandler.write_error` (lines 735-753)

```python
    def write_error(self, status_code: int, **kwargs: Any) -> None:
        """APIHandler errors are JSON, not human pages"""
        self.set_header("Content-Type", "application/json")
        message = responses.get(status_code, "Unknown HTTP Error")
        reply: dict[str, Any] = {
            "message": message,
        }
        exc_info = kwargs.get("exc_info")
        if exc_info:
            e = exc_info[1]
            if isinstance(e, HTTPError):
                reply["message"] = e.log_message or message
                reply["reason"] = e.reason
            else:
                reply["message"] = "Unhandled error"
                reply["reason"] = None
                reply["traceback"] = "".join(traceback.format_exception(*exc_info))
        self.log.warning("wrote error: %r", reply["message"], exc_info=True)
        self.finish(json.dumps(reply))
```

## `jupyter_server/base/handlers.py` — `module:<module>@13` (lines 13-13)

```python
import traceback
```

## `jupyter_server/services/kernels/handlers.py` — `class:KernelActionHandler` (lines 88-111)

```python
class KernelActionHandler(KernelsAPIHandler):
    """A kernel action API handler."""

    @web.authenticated
    @authorized
    async def post(self, kernel_id, action):
        """Interrupt or restart a kernel."""
        km = self.kernel_manager
        if action == "interrupt":
            await ensure_async(km.interrupt_kernel(kernel_id))  # type:ignore[func-returns-value]
            self.set_status(204)
        if action == "restart":
            try:
                await km.restart_kernel(kernel_id)
            except Exception as e:
                message = "Exception restarting kernel"
                self.log.error(message, exc_info=True)
                traceback = format_tb(e.__traceback__)
                self.write(json.dumps({"message": message, "traceback": traceback}))
                self.set_status(500)
            else:
                model = await ensure_async(km.kernel_model(kernel_id))
                self.write(json.dumps(model, default=json_default))
        self.finish()
```

## `jupyter_server/services/kernels/handlers.py` — `function:KernelActionHandler.post` (lines 91-111)

```python
    async def post(self, kernel_id, action):
        """Interrupt or restart a kernel."""
        km = self.kernel_manager
        if action == "interrupt":
            await ensure_async(km.interrupt_kernel(kernel_id))  # type:ignore[func-returns-value]
            self.set_status(204)
        if action == "restart":
            try:
                await km.restart_kernel(kernel_id)
            except Exception as e:
                message = "Exception restarting kernel"
                self.log.error(message, exc_info=True)
                traceback = format_tb(e.__traceback__)
                self.write(json.dumps({"message": message, "traceback": traceback}))
                self.set_status(500)
            else:
                model = await ensure_async(km.kernel_model(kernel_id))
                self.write(json.dumps(model, default=json_default))
        self.finish()
```

## `jupyter_server/services/kernels/handlers.py` — `module:<module>@8` (lines 8-8)

```python
from traceback import format_tb
```
