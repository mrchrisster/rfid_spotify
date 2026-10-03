"""Local Spotify OAuth flow shared by both command-line helpers.

Credentials come from SPOTIFY_CLIENT_ID and SPOTIFY_CLIENT_SECRET, never source.
"""
import hmac
import os
import secrets
import threading
import webbrowser
from urllib.parse import urlencode

import requests
from flask import Flask, request
from werkzeug.serving import WSGIRequestHandler, make_server

SCOPES = "user-read-playback-state user-modify-playback-state user-read-currently-playing"
REDIRECT_URI = "http://127.0.0.1:8080/callback"
TOKEN_URL = "https://accounts.spotify.com/api/token"


def exchange_code(code, client_id, client_secret, session=requests):
    response = session.post(
        TOKEN_URL,
        auth=(client_id, client_secret),
        data={"grant_type": "authorization_code", "code": code, "redirect_uri": REDIRECT_URI},
        timeout=(5, 15),
    )
    if response.status_code != 200:
        raise RuntimeError(f"Spotify token exchange failed (HTTP {response.status_code})")
    try:
        token = response.json().get("refresh_token")
    except (ValueError, AttributeError) as exc:
        raise RuntimeError("Spotify returned an invalid token response") from exc
    if not isinstance(token, str) or not token or len(token) > 1024:
        raise RuntimeError("Spotify response has no valid refresh token")
    return token


def create_callback_app(state, client_id, client_secret, done, result, session=requests):
    app = Flask(__name__)
    app.config["MAX_CONTENT_LENGTH"] = 2048
    used = False

    @app.route("/callback")
    def callback():
        nonlocal used
        received_state = request.args.get("state", "")
        if used or not hmac.compare_digest(received_state.encode("utf-8"), state.encode("utf-8")):
            return "Invalid or already used authorization state.", 400
        used = True
        try:
            if request.args.get("error") or not request.args.get("code"):
                raise RuntimeError("Spotify authorization was denied or incomplete")
            result["token"] = exchange_code(request.args["code"], client_id, client_secret, session)
            return "Authorization complete. Return to your terminal to finish installation."
        except (requests.RequestException, RuntimeError) as exc:
            # Do not return raw responses, tokens, or unescaped callback parameters.
            result["error"] = str(exc) if isinstance(exc, RuntimeError) else "Spotify request failed; retry authorization"
            return "Authorization failed. Return to your terminal.", 502
        finally:
            done.set()

    return app


def authorize(timeout=180):
    client_id = os.environ.get("SPOTIFY_CLIENT_ID", "")
    client_secret = os.environ.get("SPOTIFY_CLIENT_SECRET", "")
    if not client_id or not client_secret:
        raise RuntimeError("Set SPOTIFY_CLIENT_ID and SPOTIFY_CLIENT_SECRET in your environment")
    state = secrets.token_urlsafe(32)
    done, result = threading.Event(), {}
    app = create_callback_app(state, client_id, client_secret, done, result)
    # Bind before opening the browser, including when an existing login redirects immediately.
    class PrivateRequestHandler(WSGIRequestHandler):
        def log_request(self, code="-", size="-"):
            pass  # Callback URL contains a short-lived authorization code.

    server = make_server("127.0.0.1", 8080, app, request_handler=PrivateRequestHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = "https://accounts.spotify.com/authorize?" + urlencode({
        "client_id": client_id, "response_type": "code", "redirect_uri": REDIRECT_URI,
        "scope": SCOPES, "state": state,
    })
    try:
        print("Opening Spotify authorization. If needed, open this URL:\n" + url)
        webbrowser.open(url)
        if not done.wait(timeout):
            raise RuntimeError("Authorization timed out; run the command again")
        if "error" in result:
            raise RuntimeError(result["error"])
        return result["token"]
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
