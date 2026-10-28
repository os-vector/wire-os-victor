# Calm English speech

The English native TTS configuration in `resources/config/engine/tts_config.json`
uses a conservative starting point for calmer, mellower speech:

| Setting | Original | Calm |
| --- | --- | --- |
| Base speed | 80 | 76 |
| Speed for text longer than 10 characters | 83 | 76 |
| Speed for text up to 10 characters | 68 | 68 |
| Pitch | 100 | 94 |
| Voice shaping | 130 | 120 |

Both the robot (`vicos`) and macOS English entries use these settings. Pitch and
shaping are synthesizer parameters, not percentages of acoustic frequency.
The lower settings aim for a less bright delivery without a large pitch shift.
Volume, equalization, punctuation pauses, and the selected base voice retain
their existing settings. Other languages retain their existing configuration.

The speed traits override the base speed for matching text lengths, so the
longer-text trait is adjusted along with the base. The existing inclusive
ranges overlap at 10 characters; the first matching trait wins, retaining speed
68 for that boundary. Text outside the traits uses the new base speed.

Rocket-Pod's native conversation speech currently requests duration scalar
0.95, whereas reminders normally request 1.0. The synthesizer divides speed by
that scalar: longer replies now use 76 / 0.95 = 80 instead of about 87.4, and
reminders use 76 instead of 83. Both rates are about 8.4% lower than before.
Short acknowledgements retain their existing pace.

These changes take effect after deploying the updated resources and restarting
the animation process, which loads the TTS configuration. They apply to native
synthesized English speech, including Rocket-Pod's native `SayText` requests.
Externally generated speech streamed by Rocket-Pod and prerecorded character
sounds use separate audio paths and do not inherit these settings.

The source configuration and effective rates can be checked on the desktop,
but the result still needs a listening check on Vector. In particular, verify
that the installed voice accepts the pitch and shaping controls, and listen
for consonant clarity and consistent loudness through the robot processing.
Useful sample phrases are:

- "Your timer is ready."
- "The temperature is seventy-two degrees."
- "You have a reminder at three thirty."

To restore the original English voice settings, use the Original values in the
table for both English configuration entries and redeploy the resources.
