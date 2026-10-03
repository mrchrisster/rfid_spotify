#!/usr/bin/env python3
"""Authorize Spotify and submit a validated token update to an authenticated ESP32."""
import argparse
import getpass
import os
import time
from urllib.parse import urlsplit

import requests
from requests.auth import HTTPDigestAuth
from spotify_auth import authorize


def install_token(base_url, token, password, session=None, timeout=60, sleep=time.sleep, clock=time.monotonic):
    parsed = urlsplit(base_url)
    if parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment or parsed.path not in ("", "/"):
        raise ValueError("ESP32 URL must be an http(s) origin, e.g. http://spotify-player.local")
    base_url = base_url.rstrip("/")
    session = session or requests.Session()
    session.auth = HTTPDigestAuth("admin", password)
    response = session.post(base_url + "/api/token", json={"token": token},
                            headers={"X-Requested-With": "RFIDPlayer"}, timeout=(5, 10), allow_redirects=False)
    if response.status_code != 202:
        raise RuntimeError(f"ESP32 refused token update (HTTP {response.status_code})")
    job = response.json().get("job")
    if not isinstance(job, int):
        raise RuntimeError("ESP32 did not return a job identifier")
    deadline = clock() + timeout
    while clock() < deadline:
        response = session.get(base_url + "/api/status", timeout=(5, 10), allow_redirects=False)
        if response.status_code != 200:
            raise RuntimeError(f"Cannot read update status (HTTP {response.status_code})")
        for entry in response.json().get("jobs", []):
            if entry.get("id") != job or entry.get("code") == 202:
                continue
            if entry.get("code") not in (200, 204):
                raise RuntimeError(f"Token validation/persistence failed (code {entry.get('code')})")
            return
        sleep(1)
    raise RuntimeError(f"Update {job} is unconfirmed; check the dashboard before resubmitting")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://spotify-player.local")
    args = parser.parse_args()
    password = os.environ.get("ESP32_ADMIN_PASSWORD") or getpass.getpass("ESP32 admin password (USB Serial at boot): ")
    token = authorize()
    try:
        install_token(args.url, token, password)
    except (requests.RequestException, RuntimeError, ValueError):
        # Let the user retain the newly issued token without exposing it to logs.
        if input("Installation unconfirmed. Save token to a private local file? [y/N] ").lower() == "y":
            path = input("New file path: ").strip()
            fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, "w") as output:
                output.write(token + "\n")
        raise
    print("Refresh token validated and saved. Speaker discovery is independent of token installation.")


if __name__ == "__main__":
    try:
        main()
    except (requests.RequestException, RuntimeError, ValueError, OSError) as exc:
        raise SystemExit(str(exc))
