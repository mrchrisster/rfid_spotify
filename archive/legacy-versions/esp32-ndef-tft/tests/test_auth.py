import threading
import unittest
from unittest.mock import Mock

from spotify_auth import create_callback_app, exchange_code
from renew_token import install_token


def response(code, body):
    r = Mock(status_code=code)
    r.json.return_value = body
    return r


class AuthTests(unittest.TestCase):
    def test_rejects_missing_token_and_http_errors(self):
        for code, body in [(200, {}), (200, {"refresh_token": None}), (400, {"refresh_token": "sensitive"})]:
            session = Mock()
            session.post.return_value = response(code, body)
            with self.assertRaises(RuntimeError):
                exchange_code("code", "id", "secret", session)
            self.assertEqual(session.post.call_args.kwargs["timeout"], (5, 15))

    def test_state_prevents_unsolicited_and_replayed_callbacks(self):
        done, result = threading.Event(), {}
        session = Mock()
        session.post.return_value = response(200, {"refresh_token": "private-token"})
        app = create_callback_app("expected", "id", "secret", done, result, session)
        client = app.test_client()
        self.assertEqual(client.get("/callback?state=wrong&code=x").status_code, 400)
        self.assertEqual(client.get("/callback?state=%C3%A9&code=x").status_code, 400)
        self.assertFalse(done.is_set())
        session.post.assert_not_called()
        reply = client.get("/callback?state=expected&code=x")
        self.assertEqual(reply.status_code, 200)
        self.assertNotIn(b"private-token", reply.data)
        self.assertEqual(result["token"], "private-token")
        self.assertTrue(done.is_set())
        self.assertEqual(client.get("/callback?state=expected&code=x").status_code, 400)
        self.assertEqual(session.post.call_count, 1)

    def test_denial_is_not_reflected_as_html(self):
        done, result = threading.Event(), {}
        app = create_callback_app("expected", "id", "secret", done, result, Mock())
        reply = app.test_client().get("/callback?state=expected&error=%3Cscript%3E")
        self.assertNotIn(b"<script>", reply.data)
        self.assertTrue(done.is_set())
        self.assertIn("error", result)

    def test_update_waits_for_job_completion(self):
        session = Mock()
        session.post.return_value = response(202, {"job": 7})
        session.get.side_effect = [response(200, {"jobs": [{"id": 7, "code": 202}]}),
                                   response(200, {"jobs": [{"id": 7, "code": 200}]})]
        install_token("http://device", "private", "password", session, sleep=lambda _: None)
        self.assertEqual(session.get.call_count, 2)
        self.assertEqual(session.post.call_args.kwargs["headers"]["X-Requested-With"], "RFIDPlayer")
        self.assertFalse(session.post.call_args.kwargs["allow_redirects"])

    def test_rejected_token_is_not_reported_as_success(self):
        session = Mock()
        session.post.return_value = response(202, {"job": 7})
        session.get.return_value = response(200, {"jobs": [{"id": 7, "code": 400}]})
        with self.assertRaisesRegex(RuntimeError, "failed"):
            install_token("http://device", "private", "password", session)

    def test_timeout_reports_unknown_outcome(self):
        session = Mock()
        session.post.return_value = response(202, {"job": 7})
        with self.assertRaisesRegex(RuntimeError, "unconfirmed"):
            install_token("http://device", "private", "password", session, timeout=0)

    def test_rejects_redirects_and_bad_origin(self):
        session = Mock()
        session.post.return_value = response(302, {})
        with self.assertRaises(RuntimeError):
            install_token("http://device", "private", "password", session)
        for url in ["http://user:pass@device", "file:///tmp/a", "http://device/api", "http://device?q=x"]:
            with self.assertRaises(ValueError):
                install_token(url, "private", "password", session)


if __name__ == "__main__":
    unittest.main()
