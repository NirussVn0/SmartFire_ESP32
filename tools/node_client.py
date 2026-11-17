#!/usr/bin/env python3
"""Manual LAN API client. Token is prompted privately and is never printed."""
import argparse
import getpass
import json
import urllib.error
import urllib.parse
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base_url")
    parser.add_argument("action", choices=["status", "events", "ack", "silence", "test"])
    parser.add_argument("--since", type=int, default=0)
    args = parser.parse_args()
    parts = urllib.parse.urlsplit(args.base_url)
    if parts.scheme != "http" or not parts.hostname or parts.username or parts.password or parts.query or parts.fragment or parts.path not in ("", "/"):
        parser.error("Use a plain http://NODE_IP[:port] base URL on your trusted LAN")
    if not 0 <= args.since <= 0xFFFFFFFF:
        parser.error("since must be a uint32")
    token = getpass.getpass("Node API token: ")
    read = args.action in ("status", "events")
    path = "/v1/" + args.action if read else "/v1/commands/" + args.action
    if args.action == "events":
        path += "?since=" + str(args.since)
    request = urllib.request.Request(args.base_url.rstrip("/") + path,
                                     method="GET" if read else "POST",
                                     headers={"Authorization": "Bearer " + token},
                                     data=None if read else b"")
    # Refuse redirects so a node cannot forward the authorization header elsewhere.
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *unused):
            return None
    try:
        with urllib.request.build_opener(NoRedirect).open(request, timeout=3) as response:
            print(json.dumps(json.load(response), indent=2, ensure_ascii=False))
            if response.status == 202:
                print("Queued only. Check /v1/events for applied/rejected/expired.")
    except urllib.error.HTTPError as error:
        print("HTTP", error.code, error.read(2048).decode("utf-8", errors="replace"))
        return 1
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as error:
        print("Request failed:", type(error).__name__, "(do not auto-retry control commands)")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
