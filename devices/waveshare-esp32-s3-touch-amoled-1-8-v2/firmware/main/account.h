#pragma once
/* Sign-in labels, one per provider (plugins.h). Each names the account (sign-in gateway) that
 * provider's phone QR signs in to (docs/SETUP.md) and is shown as that provider's Settings tab title,
 * "<label>: signed in as", "<label>: not signed in", the sign-out warning and the other tab's
 * "Shared with <label> sign-in" line. Nothing else depends on them.
 *
 * On the ESP32 they come from Kconfig CONFIG_WAVESHARE_AI_HERMES_ACCOUNT_NAME and
 * CONFIG_WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME (main/Kconfig.projbuild, `idf.py menuconfig` -> "Waveshare AI
 * providers", or a CONFIG_WAVESHARE_AI_HERMES_ACCOUNT_NAME="My Lab" line in an sdkconfig defaults file). Host
 * tests may pass -DWAVESHARE_AI_HERMES_ACCOUNT_NAME='"My Lab"' / -DWAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME=...
 * (tests/run_host_tests.sh). Missing = "Hermes" / "Home Assistant".
 *
 * Printable ASCII (the 9 px font draws nothing else), 1..WAVESHARE_AI_ACCOUNT_NAME_MAX characters: a
 * 16-glyph title is the widest that fits the rounded top edge at the big text size. Up to
 * WAVESHARE_AI_ACCOUNT_CAPTION_MAX characters the signed-in row reads "<label>: signed in as" next to its
 * "Sign out" hint; a longer label drops out of that one caption ("Signed in as") so nothing overlaps. */
#include "plugins.h"
#ifdef WAVESHARE_AI_ACCOUNT_NAME
#error "renamed: WAVESHARE_AI_ACCOUNT_NAME -> WAVESHARE_AI_HERMES_ACCOUNT_NAME (and WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME)"
#endif
#if defined(ESP_PLATFORM)
#include "sdkconfig.h"
#if !defined(WAVESHARE_AI_HERMES_ACCOUNT_NAME) && defined(CONFIG_WAVESHARE_AI_HERMES_ACCOUNT_NAME)
#define WAVESHARE_AI_HERMES_ACCOUNT_NAME CONFIG_WAVESHARE_AI_HERMES_ACCOUNT_NAME
#endif
#if !defined(WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME) && defined(CONFIG_WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME)
#define WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME CONFIG_WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME
#endif
#endif
#ifndef WAVESHARE_AI_HERMES_ACCOUNT_NAME
#define WAVESHARE_AI_HERMES_ACCOUNT_NAME "Hermes"
#endif
#ifndef WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME
#define WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME "Home Assistant"
#endif
#define WAVESHARE_AI_ACCOUNT_NAME_MAX 16
#define WAVESHARE_AI_ACCOUNT_CAPTION_MAX 9
_Static_assert(sizeof(WAVESHARE_AI_HERMES_ACCOUNT_NAME)>1&&sizeof(WAVESHARE_AI_HERMES_ACCOUNT_NAME)-1<=WAVESHARE_AI_ACCOUNT_NAME_MAX,
               "WAVESHARE_AI_HERMES_ACCOUNT_NAME must be 1..16 characters");
_Static_assert(sizeof(WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME)>1&&sizeof(WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME)-1<=WAVESHARE_AI_ACCOUNT_NAME_MAX,
               "WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME must be 1..16 characters");
#define WAVESHARE_AI_ACCOUNT_SIGNED_IN_AS(name) (sizeof(name)-1<=WAVESHARE_AI_ACCOUNT_CAPTION_MAX?name ": signed in as":"Signed in as")
#define WAVESHARE_AI_HERMES_SIGNED_IN WAVESHARE_AI_ACCOUNT_SIGNED_IN_AS(WAVESHARE_AI_HERMES_ACCOUNT_NAME)
#define WAVESHARE_AI_HERMES_SIGNED_OUT WAVESHARE_AI_HERMES_ACCOUNT_NAME ": not signed in"
#define WAVESHARE_AI_HERMES_SIGNOUT_NOTE "Ask stops working until you sign in to " WAVESHARE_AI_HERMES_ACCOUNT_NAME " again."
#define WAVESHARE_AI_HERMES_SHARED "Shared with " WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME " sign-in"
#define WAVESHARE_AI_HOME_ASSISTANT_SIGNED_IN WAVESHARE_AI_ACCOUNT_SIGNED_IN_AS(WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME)
#define WAVESHARE_AI_HOME_ASSISTANT_SIGNED_OUT WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME ": not signed in"
#define WAVESHARE_AI_HOME_ASSISTANT_SIGNOUT_NOTE "Sensor stops working until you sign in to " WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME " again."
#define WAVESHARE_AI_HOME_ASSISTANT_SHARED "Shared with " WAVESHARE_AI_HERMES_ACCOUNT_NAME " sign-in"
/* Status flag 8 (phone_qr.h PHONE_FLAG_SHARED): both providers use one sign-in gateway on the bridge,
 * so signing out of either signs out both. Names only the gated tiles this firmware has. */
#if WAVESHARE_AI_PLUGIN_AI && !WAVESHARE_AI_PLUGIN_HOME_ASSISTANT
#define WAVESHARE_AI_SIGNIN_TILES_STOP "Ask stops"
#elif WAVESHARE_AI_PLUGIN_HOME_ASSISTANT && !WAVESHARE_AI_PLUGIN_AI
#define WAVESHARE_AI_SIGNIN_TILES_STOP "Sensor stops"
#else
#define WAVESHARE_AI_SIGNIN_TILES_STOP "Ask and Sensor stop"
#endif
#define WAVESHARE_AI_SHARED_SIGNOUT_NOTE "Signs out both " WAVESHARE_AI_HERMES_ACCOUNT_NAME " and " WAVESHARE_AI_HOME_ASSISTANT_ACCOUNT_NAME \
 " (shared sign-in). " WAVESHARE_AI_SIGNIN_TILES_STOP " working until you sign in again."
/* The one account tab of a shared sign-in (both providers built, flag 8: home_ui.h home_signin_shared)
 * names the two providers it signs in to (provider names, not the account labels; one 32-glyph line). */
#define WAVESHARE_AI_SHARED_COVERS "Covers Hermes and Home Assistant"
