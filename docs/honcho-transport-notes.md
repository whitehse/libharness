# Honcho transport notes (caller-owned)

libharness never opens sockets. Use `harness_honcho_build_messages_request` to
prepare a JSON body, then POST it with your HTTP stack (curl, librest/shaggy, etc.).

## Narrative message (preferred)

```bash
# After harness_honcho_build_messages_request(...), send the buffer:
curl --request POST \
  --url "https://api.honcho.dev/v1/workspaces/$WORKSPACE/sessions/$SESSION/messages" \
  --header "Authorization: Bearer $HONCHO_TOKEN" \
  --header "Content-Type: application/json" \
  --data @request.json
```

Example body shape produced by the library:

```json
{
  "messages": [
    {
      "peer_id": "agent_devbot",
      "content": "Migration script validation passed.",
      "metadata": {"type": "agent_response", "model": "gpt-4o"},
      "session_id": "sess_deploy",
      "workspace_id": "ws_demo"
    }
  ]
}
```

Helpers:

- `harness_honcho_metadata_chat` → `{"type":"chat_message"}`
- `harness_honcho_metadata_agent(buf, cap, &n, model)` → agent_response + model

## Policy

- Do **not** mirror tool-call payloads (`harness_honcho_should_mirror` returns false).
- Full model interaction audit belongs in PostgreSQL via libpique, not Honcho.

## Parsing responses

Feed Honcho JSON into `harness_honcho_parse_response` to emit
`HARNESS_EVENT_HONCHO_RESPONSE_PARSED` events with peer ids — still no I/O.
