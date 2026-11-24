#include <furi.h>
#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <notification/notification_messages.h>
#include <stdio.h>
#include <string.h>

#define MAX_NFC_PLAYERS 8
#define MAX_UID_LEN     10

typedef enum {
    ViewIdMenu,
    ViewIdBeacon,
    ViewIdSeeker,
    ViewIdNfc,
} ViewId;

typedef enum {
    MenuIndexBeacon,
    MenuIndexSeeker,
    MenuIndexNfc,
} MenuIndex;

typedef enum {
    CustomEventNfcTagRead = 1,
} CustomEvent;

/* -------------------- Notification Sequences -------------------- */

static const NotificationSequence seq_tick = {
    &message_note_c5,
    &message_delay_50,
    &message_sound_off,
    &message_blue_255,
    &message_delay_50,
    NULL,
};

static const NotificationSequence seq_error = {
    &message_red_255,
    &message_vibro_on,
    &message_note_c4,
    &message_delay_100,
    &message_sound_off,
    &message_vibro_off,
    NULL,
};

static const NotificationSequence seq_start_fanfare = {
    &message_vibro_on,
    &message_green_255,
    &message_note_c5,
    &message_delay_100,
    &message_note_e5,
    &message_delay_100,
    &message_note_g5,
    &message_delay_100,
    &message_note_c6,
    &message_delay_250,
    &message_sound_off,
    &message_vibro_off,
    NULL,
};

static const NotificationSequence seq_beacon_chirp = {
    &message_red_255,
    &message_vibro_on,
    &message_note_a5,
    &message_delay_100,
    &message_sound_off,
    &message_vibro_off,
    &message_delay_50,
    &message_note_c6,
    &message_delay_100,
    &message_sound_off,
    NULL,
};

static const NotificationSequence seq_beacon_urgent = {
    &message_red_255,
    &message_vibro_on,
    &message_note_c6,
    &message_delay_50,
    &message_sound_off,
    &message_delay_50,
    &message_blue_255,
    &message_note_c6,
    &message_delay_50,
    &message_sound_off,
    &message_delay_50,
    &message_red_255,
    &message_note_c6,
    &message_delay_100,
    &message_sound_off,
    &message_vibro_off,
    NULL,
};

static const NotificationSequence seq_marco_horn = {
    &message_blue_255,
    &message_vibro_on,
    &message_note_g5,
    &message_delay_250,
    &message_sound_off,
    &message_delay_50,
    &message_note_c6,
    &message_delay_250,
    &message_sound_off,
    &message_vibro_off,
    NULL,
};

static const NotificationSequence seq_found_chime = {
    &message_green_255,
    &message_vibro_on,
    &message_note_e5,
    &message_delay_100,
    &message_note_g5,
    &message_delay_100,
    &message_sound_off,
    &message_vibro_off,
    NULL,
};

static const NotificationSequence seq_victory = {
    &message_green_255,
    &message_vibro_on,
    &message_note_c5,
    &message_delay_100,
    &message_note_e5,
    &message_delay_100,
    &message_note_g5,
    &message_delay_100,
    &message_note_c6,
    &message_delay_250,
    &message_vibro_off,
    &message_delay_50,
    &message_vibro_on,
    &message_note_c6,
    &message_delay_250,
    &message_sound_off,
    &message_vibro_off,
    NULL,
};

static const NotificationSequence seq_timeout = {
    &message_red_255,
    &message_vibro_on,
    &message_note_g4,
    &message_delay_250,
    &message_note_e4,
    &message_delay_250,
    &message_note_c4,
    &message_delay_250,
    &message_sound_off,
    &message_vibro_off,
    NULL,
};

/* -------------------- Models & App State -------------------- */

typedef enum {
    BeaconStateSetup,
    BeaconStateHiding,
    BeaconStateActive,
    BeaconStateFound,
    BeaconStateTimeout,
} BeaconState;

typedef struct {
    BeaconState state;
    uint32_t hide_delay_sec;
    uint32_t round_limit_sec;
    uint32_t remaining_sec;
    uint32_t elapsed_sec;
    uint32_t next_chirp_sec;
} BeaconModel;

typedef enum {
    SeekerStateSetup,
    SeekerStateCounting,
    SeekerStateSeeking,
    SeekerStateVictory,
} SeekerState;

typedef struct {
    SeekerState state;
    uint32_t countdown_sec;
    uint32_t remaining_sec;
    uint32_t elapsed_sec;
    uint8_t total_hiders;
    uint8_t found_hiders;
} SeekerModel;

typedef enum {
    NfcStateRegister,
    NfcStatePlaying,
    NfcStateVictory,
} NfcGameState;

typedef struct {
    uint8_t uid[MAX_UID_LEN];
    size_t uid_len;
    bool tagged;
    uint32_t tag_time_sec;
} NfcPlayer;

typedef struct {
    NfcGameState state;
    NfcPlayer players[MAX_NFC_PLAYERS];
    uint8_t player_count;
    uint8_t tagged_count;
    uint32_t elapsed_sec;
    char status_line[36];
} NfcModel;

typedef struct {
    Gui* gui;
    NotificationApp* notifications;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;

    View* beacon_view;
    View* seeker_view;
    View* nfc_view;

    Nfc* nfc;
    NfcPoller* nfc_poller;
    uint8_t pending_uid[MAX_UID_LEN];
    size_t pending_uid_len;
    uint8_t nfc_rearm_ticks;

    ViewId current_view;
    uint32_t last_sec_tick;
} HideSeekApp;

/* -------------------- Helpers -------------------- */

static void draw_header(Canvas* canvas, const char* title) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 12);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 6, AlignCenter, AlignCenter, title);
    canvas_set_color(canvas, ColorBlack);
}

static uint32_t beacon_chirp_interval(uint32_t remaining_sec) {
    if(remaining_sec <= 20) return 3;
    if(remaining_sec <= 60) return 7;
    return 15;
}

static void format_mmss(char* buf, size_t size, uint32_t total_sec) {
    snprintf(
        buf,
        size,
        "%02lu:%02lu",
        (unsigned long)(total_sec / 60),
        (unsigned long)(total_sec % 60));
}

static bool uid_equals(const uint8_t* a, size_t a_len, const uint8_t* b, size_t b_len) {
    if(a_len != b_len) return false;
    return memcmp(a, b, a_len) == 0;
}

/* -------------------- NFC Lifecycle Helpers -------------------- */

static NfcCommand nfc_tag_poller_callback(NfcGenericEvent event, void* context) {
    HideSeekApp* app = context;
    if(event.protocol == NfcProtocolIso14443_3a) {
        const Iso14443_3aPollerEvent* iso_event = event.event_data;
        if(iso_event && iso_event->type == Iso14443_3aPollerEventTypeReady) {
            const Iso14443_3aData* data = nfc_poller_get_data(app->nfc_poller);
            if(data) {
                size_t uid_len = 0;
                const uint8_t* uid = iso14443_3a_get_uid(data, &uid_len);
                if(uid && uid_len > 0) {
                    if(uid_len > MAX_UID_LEN) uid_len = MAX_UID_LEN;
                    memcpy(app->pending_uid, uid, uid_len);
                    app->pending_uid_len = uid_len;
                    view_dispatcher_send_custom_event(
                        app->view_dispatcher, CustomEventNfcTagRead);
                    return NfcCommandStop;
                }
            }
        }
    }
    return NfcCommandContinue;
}

static void nfc_tag_stop_poller(HideSeekApp* app) {
    if(app->nfc_poller) {
        nfc_poller_stop(app->nfc_poller);
        nfc_poller_free(app->nfc_poller);
        app->nfc_poller = NULL;
    }
}

static void nfc_tag_start_poller(HideSeekApp* app) {
    nfc_tag_stop_poller(app);
    if(app->nfc) {
        app->nfc_poller = nfc_poller_alloc(app->nfc, NfcProtocolIso14443_3a);
        nfc_poller_start(app->nfc_poller, nfc_tag_poller_callback, app);
    }
}

/* -------------------- 1. Beacon View (Find the Flipper) -------------------- */

static void beacon_draw_callback(Canvas* canvas, void* _model) {
    BeaconModel* model = _model;
    char buf[48];

    canvas_clear(canvas);
    draw_header(canvas, "FIND THE FLIPPER");

    if(model->state == BeaconStateSetup) {
        canvas_set_font(canvas, FontSecondary);
        snprintf(
            buf, sizeof(buf), "Hide Delay (U/D): %lu s", (unsigned long)model->hide_delay_sec);
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, buf);

        snprintf(
            buf,
            sizeof(buf),
            "Round Time (L/R): %lu:%02lu",
            (unsigned long)(model->round_limit_sec / 60),
            (unsigned long)(model->round_limit_sec % 60));
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, buf);

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 58, AlignCenter, AlignBottom, "[OK] Arm Beacon");
    } else if(model->state == BeaconStateHiding) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignCenter, "Go hide the Flipper!");

        format_mmss(buf, sizeof(buf), model->remaining_sec);
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, buf);

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "[Back] Cancel");
    } else if(model->state == BeaconStateActive) {
        format_mmss(buf, sizeof(buf), model->remaining_sec);
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, buf);

        canvas_set_font(canvas, FontSecondary);
        snprintf(buf, sizeof(buf), "Next chirp in %lu s", (unsigned long)model->next_chirp_sec);
        canvas_draw_str_aligned(canvas, 64, 46, AlignCenter, AlignCenter, buf);

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "[OK] I Found It!");
    } else if(model->state == BeaconStateFound) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "BEACON DISARMED!");

        canvas_set_font(canvas, FontSecondary);
        snprintf(
            buf,
            sizeof(buf),
            "Found in %02lu:%02lu",
            (unsigned long)(model->elapsed_sec / 60),
            (unsigned long)(model->elapsed_sec % 60));
        canvas_draw_str_aligned(canvas, 64, 39, AlignCenter, AlignCenter, buf);

        canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "[OK] Play Again");
    } else {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignCenter, "TIME'S UP!");

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 64, 38, AlignCenter, AlignCenter, "Alarm active until found!");

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 60, AlignCenter, AlignBottom, "[OK] Silence & Reset");
    }
}

static bool beacon_input_callback(InputEvent* event, void* context) {
    HideSeekApp* app = context;
    bool consumed = false;
    const NotificationSequence* seq = NULL;
    bool reset_tick = false;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return false;
    }

    with_view_model(
        app->beacon_view,
        BeaconModel * model,
        {
            if(model->state == BeaconStateSetup) {
                if(event->key == InputKeyUp) {
                    if(model->hide_delay_sec < 60) model->hide_delay_sec += 5;
                    consumed = true;
                } else if(event->key == InputKeyDown) {
                    if(model->hide_delay_sec > 5) model->hide_delay_sec -= 5;
                    consumed = true;
                } else if(event->key == InputKeyRight) {
                    if(model->round_limit_sec < 600) model->round_limit_sec += 30;
                    consumed = true;
                } else if(event->key == InputKeyLeft) {
                    if(model->round_limit_sec > 60) model->round_limit_sec -= 30;
                    consumed = true;
                } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
                    model->state = BeaconStateHiding;
                    model->remaining_sec = model->hide_delay_sec;
                    model->elapsed_sec = 0;
                    reset_tick = true;
                    seq = &seq_tick;
                    consumed = true;
                }
            } else if(model->state == BeaconStateActive) {
                if(event->key == InputKeyOk && event->type == InputTypeShort) {
                    model->state = BeaconStateFound;
                    seq = &seq_victory;
                    consumed = true;
                }
            } else if(
                model->state == BeaconStateFound || model->state == BeaconStateTimeout) {
                if(event->key == InputKeyOk && event->type == InputTypeShort) {
                    model->state = BeaconStateSetup;
                    consumed = true;
                }
            }
        },
        consumed);

    if(reset_tick) {
        app->last_sec_tick = furi_get_tick();
    }
    if(seq) {
        notification_message(app->notifications, seq);
    }

    return consumed;
}

static void beacon_exit_callback(void* context) {
    HideSeekApp* app = context;
    with_view_model(
        app->beacon_view, BeaconModel * model, { model->state = BeaconStateSetup; }, false);
}

/* -------------------- 2. Seeker View (Command Deck) -------------------- */

static void seeker_draw_callback(Canvas* canvas, void* _model) {
    SeekerModel* model = _model;
    char buf[48];

    canvas_clear(canvas);
    draw_header(canvas, "SEEKER DECK");

    if(model->state == SeekerStateSetup) {
        canvas_set_font(canvas, FontSecondary);
        snprintf(buf, sizeof(buf), "Countdown (U/D): %lu s", (unsigned long)model->countdown_sec);
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, buf);

        snprintf(buf, sizeof(buf), "Hiders (L/R): %u", (unsigned int)model->total_hiders);
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, buf);

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 58, AlignCenter, AlignBottom, "[OK] Start Count");
    } else if(model->state == SeekerStateCounting) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignCenter, "Close eyes & count!");

        format_mmss(buf, sizeof(buf), model->remaining_sec);
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, buf);

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "[Back] Cancel");
    } else if(model->state == SeekerStateSeeking) {
        format_mmss(buf, sizeof(buf), model->elapsed_sec);
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, buf);

        canvas_set_font(canvas, FontPrimary);
        snprintf(
            buf,
            sizeof(buf),
            "Found: %u / %u",
            (unsigned int)model->found_hiders,
            (unsigned int)model->total_hiders);
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, buf);

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 64, 62, AlignCenter, AlignBottom, "[UP]+1  [DN]-1  [OK]Horn");
    } else {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "ALL HIDERS FOUND!");

        canvas_set_font(canvas, FontSecondary);
        snprintf(
            buf,
            sizeof(buf),
            "%u found in %02lu:%02lu",
            (unsigned int)model->total_hiders,
            (unsigned long)(model->elapsed_sec / 60),
            (unsigned long)(model->elapsed_sec % 60));
        canvas_draw_str_aligned(canvas, 64, 39, AlignCenter, AlignCenter, buf);

        canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "[OK] Next Round");
    }
}

static bool seeker_input_callback(InputEvent* event, void* context) {
    HideSeekApp* app = context;
    bool consumed = false;
    const NotificationSequence* seq = NULL;
    bool reset_tick = false;

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return false;
    }

    with_view_model(
        app->seeker_view,
        SeekerModel * model,
        {
            if(model->state == SeekerStateSetup) {
                if(event->key == InputKeyUp) {
                    if(model->countdown_sec < 120) model->countdown_sec += 5;
                    consumed = true;
                } else if(event->key == InputKeyDown) {
                    if(model->countdown_sec > 10) model->countdown_sec -= 5;
                    consumed = true;
                } else if(event->key == InputKeyRight) {
                    if(model->total_hiders < 10) model->total_hiders++;
                    consumed = true;
                } else if(event->key == InputKeyLeft) {
                    if(model->total_hiders > 1) model->total_hiders--;
                    consumed = true;
                } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
                    model->state = SeekerStateCounting;
                    model->remaining_sec = model->countdown_sec;
                    model->elapsed_sec = 0;
                    model->found_hiders = 0;
                    reset_tick = true;
                    seq = &seq_tick;
                    consumed = true;
                }
            } else if(model->state == SeekerStateSeeking) {
                if(event->key == InputKeyUp && event->type == InputTypeShort) {
                    if(model->found_hiders < model->total_hiders) {
                        model->found_hiders++;
                        if(model->found_hiders >= model->total_hiders) {
                            model->state = SeekerStateVictory;
                            seq = &seq_victory;
                        } else {
                            seq = &seq_found_chime;
                        }
                    }
                    consumed = true;
                } else if(event->key == InputKeyDown && event->type == InputTypeShort) {
                    if(model->found_hiders > 0) {
                        model->found_hiders--;
                    }
                    consumed = true;
                } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
                    seq = &seq_marco_horn;
                    consumed = true;
                }
            } else if(model->state == SeekerStateVictory) {
                if(event->key == InputKeyOk && event->type == InputTypeShort) {
                    model->state = SeekerStateSetup;
                    consumed = true;
                }
            }
        },
        consumed);

    if(reset_tick) {
        app->last_sec_tick = furi_get_tick();
    }
    if(seq) {
        notification_message(app->notifications, seq);
    }

    return consumed;
}

static void seeker_exit_callback(void* context) {
    HideSeekApp* app = context;
    with_view_model(
        app->seeker_view, SeekerModel * model, { model->state = SeekerStateSetup; }, false);
}

/* -------------------- 3. NFC Tag-Out / Home Base View -------------------- */

static void nfc_draw_callback(Canvas* canvas, void* _model) {
    NfcModel* model = _model;
    char buf[48];

    canvas_clear(canvas);
    draw_header(canvas, "NFC TAG-OUT");

    if(model->state == NfcStateRegister) {
        canvas_set_font(canvas, FontPrimary);
        snprintf(
            buf,
            sizeof(buf),
            "Registered: %u / %u",
            (unsigned int)model->player_count,
            (unsigned int)MAX_NFC_PLAYERS);
        canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignCenter, buf);

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignCenter, model->status_line);

        if(model->player_count > 0) {
            canvas_draw_str_aligned(
                canvas, 64, 62, AlignCenter, AlignBottom, "[OK] Start   [DN] Clear");
        } else {
            canvas_draw_str_aligned(
                canvas, 64, 62, AlignCenter, AlignBottom, "Tap NFC tags to add kids");
        }
    } else if(model->state == NfcStatePlaying) {
        format_mmss(buf, sizeof(buf), model->elapsed_sec);
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, buf);

        canvas_set_font(canvas, FontPrimary);
        snprintf(
            buf,
            sizeof(buf),
            "Tagged: %u / %u",
            (unsigned int)model->tagged_count,
            (unsigned int)model->player_count);
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, buf);

        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 51, AlignCenter, AlignCenter, model->status_line);
        canvas_draw_str_aligned(
            canvas, 64, 63, AlignCenter, AlignBottom, "Tap tag to check in | [OK]Horn");
    } else {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "ALL TAGS SCANNED!");

        canvas_set_font(canvas, FontSecondary);
        snprintf(
            buf,
            sizeof(buf),
            "%u players in %02lu:%02lu",
            (unsigned int)model->player_count,
            (unsigned long)(model->elapsed_sec / 60),
            (unsigned long)(model->elapsed_sec % 60));
        canvas_draw_str_aligned(canvas, 64, 39, AlignCenter, AlignCenter, buf);

        canvas_draw_str_aligned(
            canvas, 64, 62, AlignCenter, AlignBottom, "[OK] Rematch  [DN] Reset");
    }
}

static bool nfc_input_callback(InputEvent* event, void* context) {
    HideSeekApp* app = context;
    bool consumed = false;
    bool start_poller = false;
    bool reset_tick = false;
    const NotificationSequence* seq = NULL;

    if(event->type != InputTypeShort) {
        return false;
    }

    with_view_model(
        app->nfc_view,
        NfcModel * model,
        {
            if(model->state == NfcStateRegister) {
                if(event->key == InputKeyOk && model->player_count > 0) {
                    model->state = NfcStatePlaying;
                    model->elapsed_sec = 0;
                    model->tagged_count = 0;
                    for(uint8_t i = 0; i < model->player_count; i++) {
                        model->players[i].tagged = false;
                        model->players[i].tag_time_sec = 0;
                    }
                    snprintf(model->status_line, sizeof(model->status_line), "Ready for tag scan!");
                    seq = &seq_start_fanfare;
                    reset_tick = true;
                    start_poller = true;
                    consumed = true;
                } else if(event->key == InputKeyDown && model->player_count > 0) {
                    model->player_count = 0;
                    model->tagged_count = 0;
                    snprintf(model->status_line, sizeof(model->status_line), "Cleared! Tap tags...");
                    seq = &seq_tick;
                    consumed = true;
                }
            } else if(model->state == NfcStatePlaying) {
                if(event->key == InputKeyOk) {
                    seq = &seq_marco_horn;
                    consumed = true;
                }
            } else if(model->state == NfcStateVictory) {
                if(event->key == InputKeyOk) {
                    model->state = NfcStatePlaying;
                    model->elapsed_sec = 0;
                    model->tagged_count = 0;
                    for(uint8_t i = 0; i < model->player_count; i++) {
                        model->players[i].tagged = false;
                        model->players[i].tag_time_sec = 0;
                    }
                    snprintf(model->status_line, sizeof(model->status_line), "Rematch started!");
                    seq = &seq_start_fanfare;
                    reset_tick = true;
                    start_poller = true;
                    consumed = true;
                } else if(event->key == InputKeyDown) {
                    model->state = NfcStateRegister;
                    model->player_count = 0;
                    model->tagged_count = 0;
                    snprintf(model->status_line, sizeof(model->status_line), "Hold tag to back...");
                    seq = &seq_tick;
                    start_poller = true;
                    consumed = true;
                }
            }
        },
        consumed);

    if(reset_tick) {
        app->last_sec_tick = furi_get_tick();
    }
    if(start_poller) {
        app->nfc_rearm_ticks = 0;
        nfc_tag_start_poller(app);
    }
    if(seq) {
        notification_message(app->notifications, seq);
    }

    return consumed;
}

static void nfc_enter_callback(void* context) {
    HideSeekApp* app = context;
    app->nfc_rearm_ticks = 0;
    bool need_poller = false;
    with_view_model(
        app->nfc_view,
        NfcModel * model,
        {
            if(model->state == NfcStateRegister || model->state == NfcStatePlaying) {
                need_poller = true;
            }
        },
        false);
    if(need_poller) {
        nfc_tag_start_poller(app);
    }
}

static void nfc_exit_callback(void* context) {
    HideSeekApp* app = context;
    app->nfc_rearm_ticks = 0;
    nfc_tag_stop_poller(app);
}

static void handle_nfc_tag_read(HideSeekApp* app) {
    nfc_tag_stop_poller(app);

    const NotificationSequence* seq = NULL;
    bool rearm = false;

    with_view_model(
        app->nfc_view,
        NfcModel * model,
        {
            if(model->state == NfcStateRegister) {
                rearm = true;
                bool exists = false;
                for(uint8_t i = 0; i < model->player_count; i++) {
                    if(uid_equals(
                           model->players[i].uid,
                           model->players[i].uid_len,
                           app->pending_uid,
                           app->pending_uid_len)) {
                        exists = true;
                        snprintf(
                            model->status_line,
                            sizeof(model->status_line),
                            "Kid #%u already added",
                            (unsigned int)(i + 1));
                        seq = &seq_tick;
                        break;
                    }
                }
                if(!exists) {
                    if(model->player_count < MAX_NFC_PLAYERS) {
                        NfcPlayer* p = &model->players[model->player_count];
                        memcpy(p->uid, app->pending_uid, app->pending_uid_len);
                        p->uid_len = app->pending_uid_len;
                        p->tagged = false;
                        p->tag_time_sec = 0;
                        model->player_count++;
                        snprintf(
                            model->status_line,
                            sizeof(model->status_line),
                            "Added Kid #%u (%02X:%02X)",
                            (unsigned int)model->player_count,
                            (unsigned int)p->uid[0],
                            (unsigned int)(p->uid_len > 1 ? p->uid[1] : 0));
                        seq = &seq_found_chime;
                    } else {
                        snprintf(
                            model->status_line, sizeof(model->status_line), "Max 8 kids reached!");
                        seq = &seq_error;
                    }
                }
            } else if(model->state == NfcStatePlaying) {
                rearm = true;
                int found_idx = -1;
                for(uint8_t i = 0; i < model->player_count; i++) {
                    if(uid_equals(
                           model->players[i].uid,
                           model->players[i].uid_len,
                           app->pending_uid,
                           app->pending_uid_len)) {
                        found_idx = (int)i;
                        break;
                    }
                }
                if(found_idx < 0) {
                    snprintf(model->status_line, sizeof(model->status_line), "Unknown tag!");
                    seq = &seq_error;
                } else if(model->players[found_idx].tagged) {
                    snprintf(
                        model->status_line,
                        sizeof(model->status_line),
                        "Kid #%d already tagged",
                        found_idx + 1);
                    seq = &seq_tick;
                } else {
                    model->players[found_idx].tagged = true;
                    model->players[found_idx].tag_time_sec = model->elapsed_sec;
                    model->tagged_count++;
                    snprintf(
                        model->status_line,
                        sizeof(model->status_line),
                        "Kid #%d TAGGED! (%02lu:%02lu)",
                        found_idx + 1,
                        (unsigned long)(model->elapsed_sec / 60),
                        (unsigned long)(model->elapsed_sec % 60));
                    if(model->tagged_count >= model->player_count) {
                        model->state = NfcStateVictory;
                        rearm = false;
                        seq = &seq_victory;
                    } else {
                        seq = &seq_found_chime;
                    }
                }
            }
        },
        true);

    if(rearm) {
        app->nfc_rearm_ticks = 10;
    }
    if(seq) {
        notification_message(app->notifications, seq);
    }
}

/* -------------------- Tick, Custom Event & Navigation -------------------- */

static bool hide_seek_custom_event_callback(void* context, uint32_t event) {
    HideSeekApp* app = context;
    if(event == CustomEventNfcTagRead) {
        handle_nfc_tag_read(app);
        return true;
    }
    return false;
}

static void hide_seek_tick_callback(void* context) {
    HideSeekApp* app = context;
    uint32_t now = furi_get_tick();
    uint32_t ticks_per_sec = furi_kernel_get_tick_frequency();
    bool one_sec_elapsed = ((now - app->last_sec_tick) >= ticks_per_sec);
    if(one_sec_elapsed) {
        app->last_sec_tick = now;
    }

    const NotificationSequence* seq = NULL;

    /* 100ms cooldown re-arm for NFC Tag-Out */
    if(app->current_view == ViewIdNfc && app->nfc_rearm_ticks > 0) {
        app->nfc_rearm_ticks--;
        if(app->nfc_rearm_ticks == 0) {
            nfc_tag_start_poller(app);
        }
    }

    /* 1-second timer updates for Beacon, Seeker, and NFC round clocks */
    if(one_sec_elapsed) {
        if(app->current_view == ViewIdBeacon) {
            bool redraw = false;
            with_view_model(
                app->beacon_view,
                BeaconModel * model,
                {
                    if(model->state == BeaconStateHiding) {
                        redraw = true;
                        if(model->remaining_sec > 0) model->remaining_sec--;
                        if(model->remaining_sec == 0) {
                            model->state = BeaconStateActive;
                            model->remaining_sec = model->round_limit_sec;
                            model->elapsed_sec = 0;
                            model->next_chirp_sec =
                                beacon_chirp_interval(model->round_limit_sec);
                            seq = &seq_start_fanfare;
                        } else if(model->remaining_sec <= 3) {
                            seq = &seq_tick;
                        }
                    } else if(model->state == BeaconStateActive) {
                        redraw = true;
                        model->elapsed_sec++;
                        if(model->remaining_sec > 0) model->remaining_sec--;
                        if(model->remaining_sec == 0) {
                            model->state = BeaconStateTimeout;
                            model->next_chirp_sec = 2;
                            seq = &seq_timeout;
                        } else {
                            if(model->next_chirp_sec > 0) model->next_chirp_sec--;
                            if(model->next_chirp_sec == 0) {
                                if(model->remaining_sec <= 20) {
                                    seq = &seq_beacon_urgent;
                                } else {
                                    seq = &seq_beacon_chirp;
                                }
                                model->next_chirp_sec =
                                    beacon_chirp_interval(model->remaining_sec);
                            }
                        }
                    } else if(model->state == BeaconStateTimeout) {
                        if(model->next_chirp_sec > 0) model->next_chirp_sec--;
                        if(model->next_chirp_sec == 0) {
                            seq = &seq_beacon_urgent;
                            model->next_chirp_sec = 2;
                        }
                    }
                },
                redraw);
        } else if(app->current_view == ViewIdSeeker) {
            bool redraw = false;
            with_view_model(
                app->seeker_view,
                SeekerModel * model,
                {
                    if(model->state == SeekerStateCounting) {
                        redraw = true;
                        if(model->remaining_sec > 0) model->remaining_sec--;
                        if(model->remaining_sec == 0) {
                            model->state = SeekerStateSeeking;
                            model->elapsed_sec = 0;
                            seq = &seq_start_fanfare;
                        } else if(model->remaining_sec <= 5) {
                            seq = &seq_tick;
                        }
                    } else if(model->state == SeekerStateSeeking) {
                        redraw = true;
                        model->elapsed_sec++;
                    }
                },
                redraw);
        } else if(app->current_view == ViewIdNfc) {
            bool redraw = false;
            with_view_model(
                app->nfc_view,
                NfcModel * model,
                {
                    if(model->state == NfcStatePlaying) {
                        redraw = true;
                        model->elapsed_sec++;
                    }
                },
                redraw);
        }
    }

    if(seq) {
        notification_message(app->notifications, seq);
    }
}

static void menu_callback(void* context, uint32_t index) {
    HideSeekApp* app = context;
    if(index == MenuIndexBeacon) {
        app->current_view = ViewIdBeacon;
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdBeacon);
    } else if(index == MenuIndexSeeker) {
        app->current_view = ViewIdSeeker;
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdSeeker);
    } else if(index == MenuIndexNfc) {
        app->current_view = ViewIdNfc;
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdNfc);
    }
}

static uint32_t nav_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t nav_menu_callback(void* context) {
    HideSeekApp* app = context;
    app->current_view = ViewIdMenu;
    return ViewIdMenu;
}

/* -------------------- App Lifecycle -------------------- */

static HideSeekApp* hide_seek_app_alloc(void) {
    HideSeekApp* app = malloc(sizeof(HideSeekApp));
    memset(app, 0, sizeof(HideSeekApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->nfc = nfc_alloc();
    app->current_view = ViewIdMenu;
    app->last_sec_tick = furi_get_tick();

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, hide_seek_custom_event_callback);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, hide_seek_tick_callback, 100);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Main menu
    app->submenu = submenu_alloc();
    submenu_set_header(app->submenu, "Hide & Seek");
    submenu_add_item(
        app->submenu, "Find the Flipper (Beacon)", MenuIndexBeacon, menu_callback, app);
    submenu_add_item(
        app->submenu, "Seeker Deck (Timer/Horn)", MenuIndexSeeker, menu_callback, app);
    submenu_add_item(app->submenu, "NFC Tag-Out / Home Base", MenuIndexNfc, menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->submenu), nav_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, ViewIdMenu, submenu_get_view(app->submenu));

    // 1. Beacon view
    app->beacon_view = view_alloc();
    view_allocate_model(app->beacon_view, ViewModelTypeLocking, sizeof(BeaconModel));
    with_view_model(
        app->beacon_view,
        BeaconModel * model,
        {
            model->state = BeaconStateSetup;
            model->hide_delay_sec = 15;
            model->round_limit_sec = 180;
        },
        false);
    view_set_context(app->beacon_view, app);
    view_set_draw_callback(app->beacon_view, beacon_draw_callback);
    view_set_input_callback(app->beacon_view, beacon_input_callback);
    view_set_previous_callback(app->beacon_view, nav_menu_callback);
    view_set_exit_callback(app->beacon_view, beacon_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, ViewIdBeacon, app->beacon_view);

    // 2. Seeker view
    app->seeker_view = view_alloc();
    view_allocate_model(app->seeker_view, ViewModelTypeLocking, sizeof(SeekerModel));
    with_view_model(
        app->seeker_view,
        SeekerModel * model,
        {
            model->state = SeekerStateSetup;
            model->countdown_sec = 30;
            model->total_hiders = 4;
        },
        false);
    view_set_context(app->seeker_view, app);
    view_set_draw_callback(app->seeker_view, seeker_draw_callback);
    view_set_input_callback(app->seeker_view, seeker_input_callback);
    view_set_previous_callback(app->seeker_view, nav_menu_callback);
    view_set_exit_callback(app->seeker_view, seeker_exit_callback);
    view_dispatcher_add_view(app->view_dispatcher, ViewIdSeeker, app->seeker_view);

    // 3. NFC Tag-Out view
    app->nfc_view = view_alloc();
    view_allocate_model(app->nfc_view, ViewModelTypeLocking, sizeof(NfcModel));
    with_view_model(
        app->nfc_view,
        NfcModel * model,
        {
            model->state = NfcStateRegister;
            snprintf(model->status_line, sizeof(model->status_line), "Hold tag to back...");
        },
        false);
    view_set_context(app->nfc_view, app);
    view_set_draw_callback(app->nfc_view, nfc_draw_callback);
    view_set_input_callback(app->nfc_view, nfc_input_callback);
    view_set_enter_callback(app->nfc_view, nfc_enter_callback);
    view_set_exit_callback(app->nfc_view, nfc_exit_callback);
    view_set_previous_callback(app->nfc_view, nav_menu_callback);
    view_dispatcher_add_view(app->view_dispatcher, ViewIdNfc, app->nfc_view);

    return app;
}

static void hide_seek_app_free(HideSeekApp* app) {
    nfc_tag_stop_poller(app);

    view_dispatcher_remove_view(app->view_dispatcher, ViewIdNfc);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdSeeker);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdBeacon);
    view_dispatcher_remove_view(app->view_dispatcher, ViewIdMenu);

    view_free(app->nfc_view);
    view_free(app->seeker_view);
    view_free(app->beacon_view);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);

    nfc_free(app->nfc);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t hide_and_seek_app(void* p) {
    UNUSED(p);
    HideSeekApp* app = hide_seek_app_alloc();
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewIdMenu);
    view_dispatcher_run(app->view_dispatcher);
    hide_seek_app_free(app);
    return 0;
}