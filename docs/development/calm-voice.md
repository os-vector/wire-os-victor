# Lower-pitched English speech at the original pace

The English native TTS configuration in `resources/config/engine/tts_config.json`
restores the original speaking pace and lowers the pitch for a deeper,
CASE-inspired delivery. It replaces the earlier slowdown:

| Setting | Original | Previous calm voice | Current |
| --- | --- | --- | --- |
| Base speed | 80 | 76 | 80 |
| Speed for text longer than 10 characters | 83 | 76 | 83 |
| Speed for text up to 10 characters | 68 | 68 | 68 |
| Pitch | 100 | 94 | 85 |
| Voice shaping | 130 | 120 | 120 |

Both the robot (`vicos`) and macOS English entries use these settings. Pitch and
shaping are synthesizer parameters, not percentages of acoustic frequency.
The pitch setting of 85 is a starting point for a noticeably deeper voice;
the warmer shaping from the previous configuration is retained. The providers
pass speed and pitch separately to Acapela, so lowering pitch does not require
slowing speech or changing the playback sample rate.
Volume, equalization, punctuation pauses, and the selected base voice retain
their existing settings. Other languages retain their existing configuration.

The speed traits override the base speed for matching text lengths, so the
longer-text trait is restored along with the base. The existing inclusive
ranges overlap at 10 characters; the first matching trait wins, retaining speed
68 for that boundary. Text outside the traits uses the original base speed, 80.

The synthesizer still divides speed by each request's duration scalar. With a
scalar of 0.95, longer replies use 83 / 0.95, or about 87.4, restored from 80.
With a scalar of 1.0, they use 83, restored from 76. Short acknowledgements
retain their original pace as well.

These changes take effect after deploying the updated resources and restarting
the animation process, which loads the TTS configuration. They apply to native
synthesized English speech, including Rocket-Pod's native `SayText` requests.
Externally generated speech streamed by Rocket-Pod and prerecorded character
sounds use separate audio paths and do not inherit these settings.

The source configuration and effective rates can be checked on the desktop,
but the result still needs a listening check on Vector. In particular, verify
that the installed voice accepts the pitch and shaping controls, and listen
for a deeper tone, consonant clarity, consistent loudness, and the restored
pace through the robot processing. This tunes the existing Ben voice rather
than reproducing CASE's voice exactly.
Useful sample phrases are:

- "Your timer is ready."
- "The temperature is seventy-two degrees."
- "You have a reminder at three thirty."

To restore the original English voice settings, use the Original values in the
table for both English configuration entries and redeploy the resources.
