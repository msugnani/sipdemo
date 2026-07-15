"""Local AI gateway: receive PCM + DTMF from sipdemo over WebSocket, run Vosk STT.

Run (WSL):
  python3 -m venv .venv && source .venv/bin/activate
  pip install -r requirements.txt
  # Download vosk-model-small-en-us-0.15 into ./model/  (see README)
  uvicorn app:app --host 127.0.0.1 --port 8080
"""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from vosk import KaldiRecognizer, Model, SetLogLevel

SetLogLevel(-1)

MODEL_DIR = Path(os.environ.get("VOSK_MODEL_PATH", Path(__file__).parent / "model"))
SAMPLE_RATE = 16000

app = FastAPI(title="sipdemo AI gateway")
_model: Optional[Model] = None


def get_model() -> Model:
    global _model
    if _model is None:
        if not MODEL_DIR.is_dir():
            raise RuntimeError(
                f"Vosk model not found at {MODEL_DIR}. "
                "Download vosk-model-small-en-us-0.15 and unzip into gateway/model/"
            )
        print(f"[AI GATEWAY] Loading Vosk model from {MODEL_DIR} ...", flush=True)
        _model = Model(str(MODEL_DIR))
        print("[AI GATEWAY] Model ready.", flush=True)
    return _model


@app.on_event("startup")
def startup() -> None:
    get_model()
    print("[AI GATEWAY] Listening on ws://127.0.0.1:8080/stream", flush=True)


@app.websocket("/stream")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await websocket.accept()
    print("\n[MEDIA SERVER CONNECTED] Streaming started...", flush=True)
    rec = KaldiRecognizer(get_model(), SAMPLE_RATE)
    rec.SetWords(True)

    try:
        while True:
            message = await websocket.receive()
            if message.get("type") == "websocket.disconnect":
                break

            if "bytes" in message and message["bytes"] is not None:
                audio = message["bytes"]
                if rec.AcceptWaveform(audio):
                    result = json.loads(rec.Result())
                    text = (result.get("text") or "").strip()
                    if text:
                        print(f"[STT TRANSCRIPT]: {text}", flush=True)
                else:
                    partial = json.loads(rec.PartialResult())
                    ptext = (partial.get("partial") or "").strip()
                    if ptext:
                        print(f"[STT PARTIAL]: {ptext}", end="\r", flush=True)

            elif "text" in message and message["text"] is not None:
                try:
                    payload = json.loads(message["text"])
                except json.JSONDecodeError:
                    print(f"[TEXT] {message['text']}", flush=True)
                    continue
                digit = payload.get("digit", "?")
                print(f"\n[KEYPAD INTERCEPTED]: User typed key {digit}", flush=True)

    except WebSocketDisconnect:
        print("\n[DISCONNECTED]: Media server closed the socket.", flush=True)
    except Exception as e:
        print(f"\n[DISCONNECTED]: Session ended ({e})", flush=True)
    finally:
        final = json.loads(rec.FinalResult())
        text = (final.get("text") or "").strip()
        if text:
            print(f"[STT FINAL]: {text}", flush=True)
        print("[AI GATEWAY] Stream session ended (call hangup or WS drop).", flush=True)
