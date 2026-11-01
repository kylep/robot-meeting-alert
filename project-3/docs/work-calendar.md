# Work calendar options (Google Workspace)

The app reads meetings through a **calendar source**, chosen in the app under
Settings → Calendar source. Only meeting times ever reach the robot, never
titles or people.

| Source | Status | Use when |
|---|---|---|
| Mac Calendar app (EventKit) | **Built**, the default | The work account can be added in System Settings → Internet Accounts and shows up in the Calendar app |
| Google sign-in (Calendar API) | Stub | The work laptop's Calendar app can't add the Workspace account (blocked by policy or not allowed) |
| Secret iCal link | Stub | Neither of the above works, and the admin allows secret addresses |

The stubs appear in Settings with a short explanation and never return
events. Their interface is `mac/src/calendar_provider.hpp`, and the
placeholders and TODO notes are in `mac/src/calendar_stubs.cpp`.

## On the work laptop: try in this order

1. **Mac Calendar app.** Add the work Google account in System Settings →
   Internet Accounts, then check that work meetings appear in the Calendar
   app. If they do, pick "Mac Calendar app" in Settings and tick the work
   calendar. Nothing needs building. Device-management policies sometimes
   block adding accounts; if so, go to step 2.
2. **Google sign-in (to build).** The app would open the browser and you'd
   sign in with your work account. It would then read events directly.
   - Needs: a Google Cloud project with the Calendar API enabled and an OAuth
     client ID of type "Desktop app"; a read-only events scope
     (`calendar.events.readonly`); the browser + loopback-redirect sign-in
     flow; a refresh token kept in the macOS Keychain.
   - Workspace admins can block unverified third-party apps (Admin console
     API controls). You may need IT to allow the app, or to mark it as
     trusted.
   - Read with `events.list` and `singleEvents=true` so repeating meetings
     arrive already expanded; skip declined, all-day, free and cancelled
     events, same as now.
3. **Secret iCal link (to build).** Google Calendar → Settings → your
   calendar → "Secret address in iCal format". Under Workspace's default
   sharing settings this address is often hidden; only an admin can turn it
   on. The link works like a password, so it would be kept in the Keychain.
   Parsing repeating events from an iCal file (RRULE, exceptions, time zones)
   is the fiddly part.

Facts on Google's side (scopes, admin settings) change. Check Google's
current docs before building either stub:
[Calendar API scopes](https://developers.google.com/workspace/calendar/api/auth) ·
[Sync your calendar with computer programs](https://support.google.com/calendar/answer/37648).

## Network

Whichever source is used, the work laptop must be able to reach the robot
on Wi-Fi (same network, or type the robot's address under Settings → Robot
address). Wi-Fi setup itself still needs USB.
