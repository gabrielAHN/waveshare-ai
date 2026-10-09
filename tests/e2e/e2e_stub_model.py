"""Tiny OpenAI-compatible stub model for disposable E2E runs (no network, no paid calls).

GET /v1/models, POST /v1/chat/completions (stream or not). The reply echoes a fixed sentence;
when the last user message contains "slowly" the stream is spread over --slow-seconds so a
Stop/interrupt can be exercised mid-turn. Logs only request counts, never message content.
"""
import argparse
import asyncio
import json
import time

from aiohttp import web

REPLY = "Sure. **The porch light** is now on, and I set a reminder to switch it off at `11 pm`."
SLOW = "Working on it step by step. " * 40


def last_user(messages):
    for message in reversed(messages or []):
        if message.get("role") == "user":
            content = message.get("content")
            if isinstance(content, list):
                content = " ".join(part.get("text", "") for part in content if isinstance(part, dict))
            return str(content or "")
    return ""


def make_app(slow_seconds):
    counts = {"chat": 0, "models": 0}

    async def models(request):
        counts["models"] += 1
        return web.json_response({"object": "list", "data": [{"id": "stub-model", "object": "model", "owned_by": "e2e",
                                                               "context_length": 131072}]})

    async def chat(request):
        body = await request.json()
        counts["chat"] += 1
        text = last_user(body.get("messages"))
        slow = "slowly" in text.lower()
        reply = SLOW if slow else REPLY
        print(json.dumps({"event": "chat", "n": counts["chat"], "stream": bool(body.get("stream")), "slow": slow,
                          "messages": len(body.get("messages") or [])}), flush=True)
        created = int(time.time())
        if not body.get("stream"):
            return web.json_response({"id": "stub", "object": "chat.completion", "created": created, "model": "stub-model",
                                      "choices": [{"index": 0, "finish_reason": "stop",
                                                   "message": {"role": "assistant", "content": reply}}],
                                      "usage": {"prompt_tokens": 10, "completion_tokens": 20, "total_tokens": 30}})
        response = web.StreamResponse(headers={"Content-Type": "text/event-stream", "Cache-Control": "no-cache"})
        await response.prepare(request)
        words = reply.split(" ")
        delay = slow_seconds / len(words) if slow else 0.02
        try:
            for i, word in enumerate(words):
                chunk = {"id": "stub", "object": "chat.completion.chunk", "created": created, "model": "stub-model",
                         "choices": [{"index": 0, "delta": ({"role": "assistant"} if i == 0 else {}) | {"content": word + (" " if i < len(words) - 1 else "")},
                                      "finish_reason": None}]}
                await response.write(b"data: " + json.dumps(chunk).encode() + b"\n\n")
                await asyncio.sleep(delay)
            final = {"id": "stub", "object": "chat.completion.chunk", "created": created, "model": "stub-model",
                     "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}],
                     "usage": {"prompt_tokens": 10, "completion_tokens": len(words), "total_tokens": 10 + len(words)}}
            await response.write(b"data: " + json.dumps(final).encode() + b"\n\ndata: [DONE]\n\n")
        except (ConnectionResetError, asyncio.CancelledError):
            print(json.dumps({"event": "client_disconnected", "n": counts["chat"]}), flush=True)
            raise
        return response

    app = web.Application()
    app.router.add_get("/v1/models", models)
    app.router.add_post("/v1/chat/completions", chat)
    return app


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--slow-seconds", type=float, default=40.0)
    args = parser.parse_args()
    print(json.dumps({"event": "stub-ready", "port": args.port}), flush=True)
    web.run_app(make_app(args.slow_seconds), host="127.0.0.1", port=args.port, print=None, access_log=None)


if __name__ == "__main__":
    main()
