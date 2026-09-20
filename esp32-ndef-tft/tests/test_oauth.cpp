#include "OAuthSession.h"
#include <assert.h>
#include <stdio.h>
int main() {
  char out[64];
  assert(OAuth::parameter("code=a%2Bb%2Fc%3D&state=ok", "code", out, sizeof(out)) && !strcmp(out,"a+b/c="));
  assert(!OAuth::parameter("state=one&state=two", "state", out, sizeof(out)));
  assert(!OAuth::parameter("state=one&%73tate=two", "state", out, sizeof(out)));
  assert(!OAuth::parameter("code=a%00b", "code", out, sizeof(out)));
  assert(!OAuth::parameter("code=a%0Db", "code", out, sizeof(out)));
  assert(!OAuth::parameter("code=a%", "code", out, sizeof(out)));
  assert(!OAuth::parameter("code=a%Q1", "code", out, sizeof(out)));
  assert(!OAuth::parameter("code=abcd", "code", out, 4));
  assert(!OAuth::parameter("state=ok", "code", out, sizeof(out)));
  OAuth::Session session;
  strcpy(session.state,"random-state"); strcpy(session.browser,"random-browser"); strcpy(session.verifier,"secret-verifier");
  session.active=true; session.issued=UINT32_MAX-100;
  assert(session.valid("random-state","random-browser",100));
  assert(!session.valid("wrong-state","random-browser",100));
  assert(!session.valid("random-state","wrong-browser",100));
  assert(!session.valid("random-state","random-browser",session.issued+300000));
  session.consume();
  assert(!session.valid("random-state","random-browser",100));
  assert(!session.verifier[0] && !session.state[0]);
  puts("OAuth callback and session regression tests passed");
}
