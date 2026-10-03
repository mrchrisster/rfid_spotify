// Flash/link experiment: real HTTPS server + certificate + PKCE + callback parsing.
// Deliberately never authorizes Spotify or stores tokens. Do not deploy this probe.
#include "HttpsFlashProbe.h"
#include "ProbeCertificate.h"
#include <Arduino.h>
#include <esp_https_server.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>

namespace {
httpd_handle_t server = nullptr;
char verifier[45], challenge[45], state[45];
uint32_t startedAt = 0;

bool base64url(const uint8_t* input, size_t size, char* output, size_t capacity) {
  size_t written = 0;
  if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(output), capacity, &written, input, size)) return false;
  while (written && output[written - 1] == '=') --written;
  output[written] = 0;
  for (size_t i = 0; i < written; ++i) {
    if (output[i] == '+') output[i] = '-';
    else if (output[i] == '/') output[i] = '_';
  }
  return true;
}
bool createSession() {
  uint8_t random[32], digest[32];
  esp_fill_random(random, sizeof(random));
  if (!base64url(random, sizeof(random), verifier, sizeof(verifier))) return false;
  esp_fill_random(random, sizeof(random));
  if (!base64url(random, sizeof(random), state, sizeof(state))) return false;
  if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(verifier), strlen(verifier), digest, 0)) return false;
  startedAt = millis();
  return base64url(digest, sizeof(digest), challenge, sizeof(challenge));
}
esp_err_t root(httpd_req_t* request) {
  httpd_resp_set_type(request, "text/html");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_sendstr(request,
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Reconnect Spotify</title></head><body><h1>Spotify connection</h1>"
    "<p>The same web interface works on players with and without a screen.</p>"
    "<p>Size experiment only. This does not connect to Spotify.</p>"
    "<form action='/connect' method='post'><button>Reconnect Spotify (test)</button></form></body></html>");
}
esp_err_t connect(httpd_req_t* request) {
  if (!createSession()) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "PKCE generation failed");
  httpd_resp_set_type(request, "text/html");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  char body[512];
  snprintf(body, sizeof(body), "<!doctype html><title>HTTPS flash probe</title><h1>Reconnect flow test</h1>"
    "<p>PKCE S256 challenge: %s</p><p>No Spotify authorization is performed by this experiment.</p>", challenge);
  return httpd_resp_sendstr(request, body);
}
esp_err_t callback(httpd_req_t* request) {
  char query[1024], suppliedState[64], code[768];
  size_t length = httpd_req_get_url_query_len(request);
  if (!length || length >= sizeof(query) || httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "state", suppliedState, sizeof(suppliedState)) != ESP_OK ||
      httpd_query_key_value(query, "code", code, sizeof(code)) != ESP_OK || !state[0] ||
      millis() - startedAt > 180000 || strcmp(state, suppliedState)) {
    return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid authorization callback");
  }
  memset(state, 0, sizeof(state)); memset(verifier, 0, sizeof(verifier)); memset(code, 0, sizeof(code));
  httpd_resp_set_status(request, "501 Not Implemented");
  return httpd_resp_sendstr(request, "Flash experiment only: token exchange is not implemented.");
}
}

void startHttpsFlashProbe() {
  if (server) return;
  httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
  config.httpd.stack_size = 8192;
  config.httpd.max_open_sockets = 1;
  config.httpd.max_uri_handlers = 3;
  config.httpd.lru_purge_enable = true;
  config.httpd.recv_wait_timeout = 5;
  config.httpd.send_wait_timeout = 5;
  config.servercert = reinterpret_cast<const uint8_t*>(probeCertificate);
  config.servercert_len = sizeof(probeCertificate);
  config.prvtkey_pem = reinterpret_cast<const uint8_t*>(probePrivateKey);
  config.prvtkey_len = sizeof(probePrivateKey);
  config.tls_handshake_timeout_ms = 8000;
  if (httpd_ssl_start(&server, &config) != ESP_OK) { Serial.println("HTTPS flash probe failed to start"); return; }
  httpd_uri_t home{}; home.uri = "/"; home.method = HTTP_GET; home.handler = root;
  httpd_uri_t begin{}; begin.uri = "/connect"; begin.method = HTTP_POST; begin.handler = connect;
  httpd_uri_t redirect{}; redirect.uri = "/callback"; redirect.method = HTTP_GET; redirect.handler = callback;
  if (httpd_register_uri_handler(server, &home) != ESP_OK || httpd_register_uri_handler(server, &begin) != ESP_OK || httpd_register_uri_handler(server, &redirect) != ESP_OK) {
    httpd_ssl_stop(server); server = nullptr;
  }
}
