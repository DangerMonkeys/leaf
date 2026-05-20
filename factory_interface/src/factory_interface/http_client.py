import socket
import time
from urllib.error import URLError
from urllib.request import Request, urlopen


DEFAULT_HTTP_TIMEOUT_SECONDS = 5.0
TIMEOUT_RETRY_ATTEMPTS = 3
TIMEOUT_RETRY_DELAY_SECONDS = 0.25


def is_timed_out_error(exc: BaseException) -> bool:
    timeout_types = (TimeoutError, socket.timeout)
    if isinstance(exc, timeout_types):
        return True
    if isinstance(exc, URLError):
        reason = exc.reason
        if isinstance(reason, timeout_types):
            return True
        return "timed out" in str(reason).lower()
    return "timed out" in str(exc).lower()


def urlopen_with_timeout_retries(
    request: Request,
    *,
    timeout: float = DEFAULT_HTTP_TIMEOUT_SECONDS,
):
    last_error: BaseException | None = None

    for attempt in range(TIMEOUT_RETRY_ATTEMPTS):
        try:
            return urlopen(request, timeout=timeout)
        except (OSError, URLError, TimeoutError) as exc:
            if not is_timed_out_error(exc):
                raise
            last_error = exc
            if attempt + 1 == TIMEOUT_RETRY_ATTEMPTS:
                break
            time.sleep(TIMEOUT_RETRY_DELAY_SECONDS)

    if last_error is not None:
        raise last_error
    raise RuntimeError("HTTP request was not attempted.")
