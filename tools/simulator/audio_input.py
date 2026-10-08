#!/usr/bin/env python3
"""Host microphone capture feeding the desktop simulator's audio input.

The tuner's capture task reads 16 kHz / 16-bit / mono PCM. This module captures
the host microphone at the same format and pushes it into the simulator, so the
application behaves as if the device's ES8311 were connected.

Uses the optional ``sounddevice`` package. If it is missing, or the microphone
cannot be opened (for example, permission denied), the caller should fall back
to the simulator's built-in "no signal" behaviour.
"""

from __future__ import annotations

SAMPLE_RATE = 16000
BLOCK_SAMPLES = 512  # matches the tuner's TUNER_HOP, i.e. one block per read
CHANNELS = 1


class MicCapture:
    """Streams the host microphone into a backend's audio input."""

    def __init__(self, backend, gain: float = 1.0):
        self._backend = backend
        self._gain = gain
        self._stream = None

    def open(self) -> None:
        """Open and start capture. Raises if the microphone is unavailable."""
        import sounddevice as sd  # imported here so the IDE runs without it

        self._stream = sd.InputStream(
            samplerate=SAMPLE_RATE,
            channels=CHANNELS,
            dtype="int16",
            blocksize=BLOCK_SAMPLES,
            callback=self._on_audio,
        )
        self._stream.start()

    def _on_audio(self, indata, frames, time_info, status) -> None:
        # Runs on PortAudio's callback thread; sim_audio_push() is mutex-guarded.
        data = indata.tobytes()
        if self._gain != 1.0:
            import numpy as np

            scaled = np.clip(indata.astype(np.float32) * self._gain, -32768, 32767)
            data = scaled.astype(np.int16).tobytes()
        self._backend.push_audio(data)

    def close(self) -> None:
        if self._stream is None:
            return
        try:
            self._stream.stop()
        finally:
            self._stream.close()
            self._stream = None
