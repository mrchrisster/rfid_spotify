#!/usr/bin/env python3
"""Authorize Spotify and save a refresh token to a new, private file."""
import argparse
import os
import requests
from spotify_auth import authorize


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, help="New file, created with owner-only permissions")
    args = parser.parse_args()
    # Reserve path before authorizing, so an existing file is never replaced accidentally.
    fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(fd, "w") as output:
            output.write(authorize() + "\n")
    except BaseException:
        os.unlink(args.output)
        raise
    print("Refresh token saved to " + args.output)


if __name__ == "__main__":
    try:
        main()
    except (requests.RequestException, RuntimeError, ValueError, OSError) as exc:
        raise SystemExit(str(exc))
