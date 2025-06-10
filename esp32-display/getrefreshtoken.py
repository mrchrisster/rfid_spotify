# pip install flask requests

import webbrowser
from flask import Flask, request
import base64
import requests

# Spotify API credentials
CLIENT_ID = "your_client_id"
CLIENT_SECRET = "your_client_secret"
REDIRECT_URI = "http://localhost:8080/callback"  # Dummy redirect URI - check in API page on Spotify
SPOTIFY_AUTH_URL = "https://accounts.spotify.com/authorize"
SPOTIFY_TOKEN_URL = "https://accounts.spotify.com/api/token"

# Flask app to handle redirect
app = Flask(__name__)

@app.route("/callback")
def callback():
    # Get the auth code from the URL
    auth_code = request.args.get("code")
    print(f"Authorization Code: {auth_code}")

    # Exchange the auth code for tokens
    tokens = exchange_code_for_tokens(auth_code)
    print(f"Access Token: {tokens['access_token']}")
    print(f"Refresh Token: {tokens['refresh_token']}")
    return "Authorization Complete! Check your terminal for the tokens."

def exchange_code_for_tokens(auth_code):
    auth_header = base64.b64encode(f"{CLIENT_ID}:{CLIENT_SECRET}".encode()).decode()
    headers = {"Authorization": f"Basic {auth_header}"}
    data = {
        "grant_type": "authorization_code",
        "code": auth_code,
        "redirect_uri": REDIRECT_URI
    }
    response = requests.post(SPOTIFY_TOKEN_URL, headers=headers, data=data)
    return response.json()

if __name__ == "__main__":
    # Generate the Spotify authorization URL
    auth_url = (
        f"{SPOTIFY_AUTH_URL}?client_id={CLIENT_ID}&response_type=code&redirect_uri={REDIRECT_URI}&"
        f"scope=user-read-private user-read-email"
    )
    print(f"Open this URL in your browser to authorize the app:\n{auth_url}")

    # Automatically open the authorization URL in the default browser
    webbrowser.open(auth_url)

    # Start the Flask server to handle the redirect
    app.run(port=8080)
