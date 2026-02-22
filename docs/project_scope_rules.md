# Project Scope Rules (Current Direction)

These rules keep Saturnis aligned with the Ymir timing-tool roadmap.

1. **Offline-first by default**
   - Prioritize trace capture, comparative replay, and deterministic analysis over runtime emulator breadth.

2. **Replay pipeline and busarb parity come first**
   - Prefer work that improves `libbusarb`, replay fidelity, diff clarity, and calibration confidence.

3. **No emulator-core expansion unless directly required**
   - Do not start decode/interpreter/BIOS bring-up work unless it is necessary to validate Ymir timing-tool behavior.

4. **Public interfaces must remain Saturnis-independent**
   - Keep `libbusarb` interfaces reusable outside Saturnis internals.

5. **Determinism is mandatory**
   - Preserve deterministic replay ordering and reproducible test outcomes.
   - Classify expected deltas explicitly using known-gap classification.

6. **Keep scaffolding, label it clearly**
   - Existing emulator/harness code may remain for validation and experiments.
   - New docs/comments should distinguish active deliverables from parked legacy work.
