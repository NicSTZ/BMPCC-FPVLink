# v0.10.10 ACTIVE MEDIA FIX — focused test

No diagnostic clearing is required.

1. Flash v0.10.10 and let the saved Pocket 4K reconnect.
2. Slot 1 active: confirm `activeMediaSlot: 1` and `mediaRemaining: 00:24:18`.
3. Move to slot 2: confirm `activeMediaSlot: 2` and `mediaRemaining: 00:23:37`.
4. Move to slot 3: confirm `activeMediaSlot: 3` and `mediaRemaining: 01:27:22`.
5. Remove all media: confirm `activeMediaSlot: 0` and `mediaRemaining: --`.
6. Quick REC -> STOP regression check.

If any slot is wrong, one screenshot of Diagnostics at that state is enough.
