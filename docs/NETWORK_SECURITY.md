# Network access and host testing

## Dashboard access

The Web Dashboard listens on port 8095. By default it binds to `127.0.0.1`, so devices on the LAN cannot connect. Use the console's local browser at `http://127.0.0.1:8095/` where that browser supports loopback access.

To enable access from another LAN device, create a random token on a trusted computer:

```sh
openssl rand -hex 32 > web.token
chmod 600 web.token
```

Install the file as `/data/anypad/web.token` on the console, preserving mode exactly `0600`, and restart OmniPad. A regular file with 32 to 128 ASCII letters, digits, underscores, or hyphens enables LAN binding. A missing, unreadable, differently permissioned, or invalid file leaves the server bound to loopback. The token is read only at startup.

In LAN mode:

- `GET /` serves the Dashboard without a token.
- `GET /api/status` is public and read-only.
- `GET /api/log` and every `POST /api/*` endpoint require `Authorization: Bearer <token>`.
- The Dashboard's LAN access token field stores the token for the current tab session and sends it in the authorization header.
- Responses do not contain the token. The server does not log authorization values and does not send CORS permission headers.
- In loopback mode, browser control requests carrying an `Origin` must match an HTTP host of `localhost` or `127.0.0.1`; this also rejects DNS-rebinding hostnames. In LAN mode, origin matching does not replace the bearer-token requirement.

For example, a control request must include the configured token:

```sh
curl -H 'Authorization: Bearer <TOKEN>' -X POST \
  'http://<PS5_IP>:8095/api/disconnect?slot=-1'
```

The server uses plain HTTP. Token mode prevents unauthenticated control requests, but does not encrypt traffic. Use it only on a trusted LAN; keep the default loopback binding on networks where other users can observe traffic.

## TCP debug input

Production builds omit the TCP frame listener. Enable it only for local development with:

```sh
make ps5 TCP_DEBUG=1
```

The listener binds to `127.0.0.1:9045`; remote TCP debug access is not supported. Each connection can send fixed 16-byte input frames. Frames are accumulated across TCP reads, validated before use, and tied to a virtual slot that was free when the connection arrived. Disconnect, invalid input, incomplete trailing data, or a five-second idle timeout releases the debug-owned slot. A physical controller's occupied slot is never selected.

Run `tools/send_frame.py 127.0.0.1 cross 1` on the machine running the opt-in payload. The tool sends repeated frames for the requested duration and closes the connection to release its slot.

## Host regression tests

Run the host suite with:

```sh
make host-test
```

Run the host-compatible tests under AddressSanitizer and UndefinedBehaviorSanitizer with:

```sh
make host-test-sanitize
```

The tests cover host parsers, Bluetooth packet boundaries and device-to-slot lookup, TCP frame parsing and local listener lifecycle, HTTP parsing/authorization/JSON serialization, and injected USB lifecycle failures for setup, slot reservation, thread creation, joining, and cleanup ordering. USB ioctl behavior, ProsperoOS ABI interactions, virtual DualSense behavior, ShellUI injection, firmware compatibility, and physical controller behavior require the PS5 SDK or hardware and are not simulated by the host suite. A host test pass is not a PS5 runtime validation.

## PS5 manual test matrix

Run this matrix on each supported firmware family before release. Record the console model, firmware, payload build, and controller model with each result.

| Check | Procedure | Expected result |
| --- | --- | --- |
| Original DualSense | Start the payload, connect the original controller, then use its buttons and sticks in the system UI and a game. | The controller remains functional and its virtual slot reports normal input. |
| Supported USB controller | Connect a supported wired controller and then a 2.4 GHz receiver. | Each device is identified and delivers input; receiver idle standby and wake behavior remain intact. |
| Disconnect and reconnect | Repeat USB insert, removal, and reconnect cycles; repeat with Bluetooth controllers when enabled by the build. | Slots and worker threads are reclaimed once and can be reused. |
| Multiple slots | Connect multiple supported controllers and exercise each in turn. | Each controller updates its own slot without taking another controller's slot. |
| User and PS button | Rebind a ready slot to a logged-in user, then trigger the PS button through the authorized control endpoint. | The selected user and controller receive the expected action. |
| Dashboard | Open the dashboard from the console in loopback mode; then configure a `0600` token and open it from a trusted LAN device. | The dashboard loads and status refreshes in both modes; LAN controls work with the token. |
| Unauthorized HTTP | In token mode, call `/api/log` and every `POST /api/*` route without a token, then with an incorrect token. | Each request receives `401`; controller state and payload execution are unchanged. |
| TCP debug default | Build the production payload without `TCP_DEBUG=1` and attempt a local connection to port `9045`. | No debug listener is available. |
| Clean stop and restart | Stop through the authorized `/api/exit` route, wait for payload shutdown, and launch it again. | USB workers and network sockets close; the next launch initializes cleanly. |

For the opt-in debug build, run with `TCP_DEBUG=1`, send frames from the PS5 host using `tools/send_frame.py`, verify an occupied slot is left alone, then disconnect and confirm the debug slot is released.
