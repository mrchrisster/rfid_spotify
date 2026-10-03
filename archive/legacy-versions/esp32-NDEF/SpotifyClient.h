#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>

struct HttpResult {
    int httpCode;
    String payload;
};

class SpotifyClient {
public:
    SpotifyClient(String clientId, String clientSecret, String deviceName, String refreshToken);

    void FetchToken();                               // Fetches a new access token
    int Play(String context_uri);                    // Starts playback of a given context
    int Shuffle();                                   // Enables shuffle on the active device
    int Next();                                      // Skips to the next track
    String GetDevices();                             // Fetches a list of devices and sets the active device
    HttpResult CallAPI(String method, String url, String body); // Generic API call method
    void ResetState();                               // Resets token and device state
    bool IsTokenValid() { return tokenValid; }       // Getter for token validity
    bool IsTokenExpired();                           // Checks if the token is expired
    unsigned long GetTokenRefreshInterval() const { return tokenRefreshInterval; } // Getter for token refresh interval
	bool EnsureTokenFresh();


private:
    WiFiClientSecure client;
    String clientId;              // Spotify Client ID
    String clientSecret;          // Spotify Client Secret
    String accessToken;           // Spotify Access Token
    String refreshToken;          // Spotify Refresh Token
    String deviceId;              // Active Spotify Device ID
    String deviceName;            // Name of the device to control

    String ParseJson(String key, String json);       // Extracts a value from JSON by key
    String GetDeviceId(String json);                // Extracts device ID from the devices JSON
    String ParseDeviceId(String json);              // Parses and sets the active device ID

    bool tokenValid = false;        // Tracks if the access token is valid
    unsigned long tokenRefreshInterval = 3600000; // Token expiration time in milliseconds
    unsigned long lastTokenRefresh = 0;          // Timestamp of the last token refresh
    unsigned long tokenExpiresIn = 0;            // Token expiration duration in milliseconds

    int MakeAPIRequest(String method, String url, String body); // Handles API requests

    // Root certificate for spotify.com
    const char* digicert_root_ca = \
    "-----BEGIN CERTIFICATE-----\n"
    "MIIEyDCCA7CgAwIBAgIQDPW9BitWAvR6uFAsI8zwZjANBgkqhkiG9w0BAQsFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
    "MjAeFw0yMTAzMzAwMDAwMDBaFw0zMTAzMjkyMzU5NTlaMFkxCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxMzAxBgNVBAMTKkRpZ2lDZXJ0IEdsb2Jh\n"
    "bCBHMiBUTFMgUlNBIFNIQTI1NiAyMDIwIENBMTCCASIwDQYJKoZIhvcNAQEBBQAD\n"
    "ggEPADCCAQoCggEBAMz3EGJPprtjb+2QUlbFbSd7ehJWivH0+dbn4Y+9lavyYEEV\n"
    "cNsSAPonCrVXOFt9slGTcZUOakGUWzUb+nv6u8W+JDD+Vu/E832X4xT1FE3LpxDy\n"
    "FuqrIvAxIhFhaZAmunjZlx/jfWardUSVc8is/+9dCopZQ+GssjoP80j812s3wWPc\n"
    "3kbW20X+fSP9kOhRBx5Ro1/tSUZUfyyIxfQTnJcVPAPooTncaQwywa8WV0yUR0J8\n"
    "osicfebUTVSvQpmowQTCd5zWSOTOEeAqgJnwQ3DPP3Zr0UxJqyRewg2C/Uaoq2yT\n"
    "zGJSQnWS+Jr6Xl6ysGHlHx+5fwmY6D36g39HaaECAwEAAaOCAYIwggF+MBIGA1Ud\n"
    "EwEB/wQIMAYBAf8CAQAwHQYDVR0OBBYEFHSFgMBmx9833s+9KTeqAx2+7c0XMB8G\n"
    "A1UdIwQYMBaAFE4iVCAYlebjbuYP+vq5Eu0GF485MA4GA1UdDwEB/wQEAwIBhjAd\n"
    "BgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwdgYIKwYBBQUHAQEEajBoMCQG\n"
    "CCsGAQUFBzABhhhodHRwOi8vb2NzcC5kaWdpY2VydC5jb20wQAYIKwYBBQUHMAKG\n"
    "NGh0dHA6Ly9jYWNlcnRzLmRpZ2ljZXJ0LmNvbS9EaWdpQ2VydEdsb2JhbFJvb3RH\n"
    "Mi5jcnQwQgYDVR0fBDswOTA3oDWgM4YxaHR0cDovL2NybDMuZGlnaWNlcnQuY29t\n"
    "L0RpZ2lDZXJ0R2xvYmFsUm9vdEcyLmNybDA9BgNVHSAENjA0MAsGCWCGSAGG/WwC\n"
    "ATAHBgVngQwBATAIBgZngQwBAgEwCAYGZ4EMAQICMAgGBmeBDAECAzANBgkqhkiG\n"
    "9w0BAQsFAAOCAQEAkPFwyyiXaZd8dP3A+iZ7U6utzWX9upwGnIrXWkOH7U1MVl+t\n"
    "wcW1BSAuWdH/SvWgKtiwla3JLko716f2b4gp/DA/JIS7w7d7kwcsr4drdjPtAFVS\n"
    "slme5LnQ89/nD/7d+MS5EHKBCQRfz5eeLjJ1js+aWNJXMX43AYGyZm0pGrFmCW3R\n"
    "bpD0ufovARTFXFZkAdl9h6g4U5+LXUZtXMYnhIHUfoyMo5tS58aI7Dd8KvvwVVo4\n"
    "chDYABPPTHPbqjc1qCmBaZx2vN4Ye5DUys/vZwP9BFohFrH/6j/f3IL16/RZkiMN\n"
    "JCqVJUzKoZHm1Lesh3Sz8W2jmdv51b2EQJ8HmA==\n"
    "-----END CERTIFICATE-----\n";
};
