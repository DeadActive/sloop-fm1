/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE (after Hortator's smc.c) */
/* FELUCCA_BLE (felucca.c includes it after bt_set.c, before bt_midi.c): the M-VAVE SMC-Mixer in its DAW (Mackie
 * Control) mode on SLOOP's four tracks (smc-seq's mapping, as Felucca's).
 * In (bt_midi.c, SMC DAW mode):
 *   faders = pitch bend on channels 1..8: 1..4 the tracks' LEVEL (the drum track's: GLO > DRUMS LVL), 5..8 their REV
 *     send (the drums': GLO > DRUMS REV), with pickup (9, the master: nothing);
 *   knobs 1..4 = CC 16..19 on channel 1, sign-magnitude clicks: the selected track's four HOME parameters (ui.c
 *     home_param); knobs 5..8 = CC 20..23: MIDI LEARN's (smc_learnable: learned, a click steps the learned parameter;
 *     else nothing);
 *   buttons = notes on channel 1, presses only: REC 0x00.. (the track selected, then REC as on the FM-1), SOLO 0x08..,
 *     MUTE 0x10.., SELECT 0x18.. for strips 1..4 (5..8: nothing); STOP 0x5D, PLAY 0x5E, REC 0x5F; BANK / CHANNEL < >
 *     0x2E 0x30 / 0x2F 0x31 the previous / next track.
 * Out (smc_frame, ble_midi_send): the LEDs as notes 127 / 0, only changes, all of them on a new link; each fader's
 *   position as E0+n v v, which the mixer blinks the fader's LED against.
 * SOLO is SLOOP's own (song.solo, as GLO + a key: the other tracks silent, their MUTE untouched); what the mixer
 * soloed ends when the mixer goes or MODE leaves SMC DAW. Main loop only. */
#define SMC_C 1
#define SMC_FADERS 8u
#define SMC_LEDS 35u                                  /* notes 0x00..0x1F, then 0x5D..0x5F */
typedef char smc_four_tracks[NTRK == 4u ? 1 : -1];
static uint32_t bt_mode_now(void);                    /* bt_midi.c */
static struct {
    uint32_t lit[2], known[2];      /* the LEDs as sent: [0] bit = note 0x00..0x1F (REC SOLO MUTE SELECT rows), [1]
                                     * bits 0..2 = STOP PLAY REC (0x5D..0x5F); known 0 = to send */
    uint8_t val[SMC_FADERS];        /* each fader's value as the fader set it or its position was sent */
    uint8_t last[SMC_FADERS];       /* each fader's last report (crossing; valid with its seen bit) */
    uint8_t up, on;                 /* linked; refreshed for SMC mode since the link (0: smc_fresh first) */
    uint8_t dark;                   /* SMC mode ended while linked: LEDs still to turn off (SMC_LEDS -> 0) */
    uint8_t picked, seen, pend;     /* bits per fader: picked up; last[] valid; position to send */
    uint8_t solo;                   /* the tracks the mixer soloed (song.solo's bits it set) */
} smc __attribute__((section(".pool")));

/* fader n's value: track n's LEVEL (faders 1..4), track n - 4's REV send (5..8); the drum track's are GLO > DRUMS LVL
 * and REV (ui.c home_param) */
UI_SMALL static int16_t *smc_fptr(uint32_t n)
{
    if ((n & 3u) == TRK_DRUM)
        return &song.g[n < 4u ? G_DRLVL : G_DRREV];
    return &trk[n & 3u].p[n < 4u ? P_LEVEL : P_REV];
}
UI_SMALL static uint32_t smc_fval(uint32_t n) { return (uint32_t)clamp(*smc_fptr(n), 0, 127); }
UI_SMALL static void smc_unsolo(void)                          /* what the mixer soloed: off */
{
    song.solo &= (uint8_t)~smc.solo;
    smc.solo = 0;
}

/* a new link, or SMC mode again: every LED unknown, every fader unpicked, every position to send */
UI_SMALL static void smc_fresh(void)
{
    uint32_t i;
    smc.known[0] = smc.known[1] = 0;
    smc.picked = smc.seen = 0;
    smc.pend = 0xFFu;
    smc.dark = 0;
    for (i = 0; i < SMC_FADERS; i++)
        smc.val[i] = (uint8_t)smc_fval(i);
    smc.on = 1;
}

/* fader n's value changed by other means (the FM-1, a load, automation, the editor): unpicked, the new position sent */
UI_SMALL static void smc_notice(uint32_t n)
{
    uint32_t v = smc_fval(n);
    if (v != smc.val[n]) {
        smc.val[n] = (uint8_t)v;
        smc.picked &= (uint8_t)~(1u << n);
        smc.pend |= (uint8_t)(1u << n);
    }
}

/* fader n at v: an unpicked fader acts once within 2 of the value or across it (smc-seq's soft takeover) */
UI_SMALL static void smc_fader(uint32_t n, uint32_t v)
{
    uint32_t b = 1u << n, s, crossed;
    smc_notice(n);
    s = smc.val[n];
    crossed = (smc.seen & b) && ((smc.last[n] < s) != (v < s));
    smc.last[n] = (uint8_t)v;                         /* every report: a later unpick crosses from here */
    smc.seen |= (uint8_t)b;
    if (!(smc.picked & b)) {
        if (!crossed && (v + 2u < s || v > s + 2u))
            return;
        smc.picked |= (uint8_t)b;
    }
    if (v == s)
        return;
    smc.val[n] = (uint8_t)v;
    smc.pend |= (uint8_t)b;                           /* echoed: the mixer's pickup LED follows (as smc-seq) */
    *smc_fptr(n) = (int16_t)v;
}

/* "T3 CUTOFF 64": a parameter's new value on the top bar */
UI_SMALL static void smc_say(uint32_t trk_i, const param_desc_t *d, int32_t v)
{
    char m[24], val[12];
    const char *unit = "";
    uint32_t n;
    param_format(d, v, val, &unit);
    m[0] = 'T';
    m[1] = (char)('1' + trk_i);
    m[2] = ' ';
    str_cpy(m + 3, d->label ? d->label : "", sizeof m - 3u);
    n = str_len(m);
    str_cpy(m + n, " ", sizeof m - n);
    n = str_len(m);
    str_cpy(m + n, val, sizeof m - n);
    ui_say(m, unit ? unit : "");
}
/* a value delta clicks on (a list or a named value: a name a click), within the range */
UI_SMALL static int32_t smc_turn(const param_desc_t *d, int32_t v, int32_t delta)
{
    if (d->names && (d->names == ESYNC_NAMES || d->names == N_IBITS || d->names == N_IRATE))
        return names_turn(d, v, delta);
    return clamp(v + delta, d->min, d->max);
}

/* knob k (0..3) turned delta clicks: the selected track's HOME parameter k (ui.c home_param), a value a click */
UI_SMALL static void smc_knob(uint32_t k, int32_t delta)
{
    int16_t *vp;
    const param_desc_t *d = home_param(k & 3u, &vp);
    if (!d || !d->label || !d->label[0] || d->max <= d->min)
        return;                                       /* the engine has no such knob */
    *vp = (int16_t)smc_turn(d, *vp, delta);
    smc_say(song.sel, d, *vp);
}

/* the CC value (0..127) at which seq.c midi_cc_set gives parameter value v: its inverse, rounded */
UI_SMALL static uint32_t smc_cc_at(const param_desc_t *d, int32_t v)
{
    int32_t r = d->max - d->min;
    return (uint32_t)clamp(((v - d->min) * 127 + r / 2) / r, 0, 127);
}

/* knob k (4..7: CC 20..23) turned delta clicks: MIDI LEARN's. Learned, a click steps the parameter it is learned to
 * by one value from where it is (no jump when the FM-1 changed it meanwhile), as the CC value midi_learned sets it
 * from; while MIDI LEARN waits for a CC, it hears this one (midi_learned: ml_arm); else nothing */
UI_SMALL static void smc_learnable(uint32_t k, int32_t delta)
{
    uint32_t cc = 16u + k, i, e = 0, id = P_COUNT, c0, c1;
    track_t *t;
    const param_desc_t *d;
    for (i = 0; i < ML_N && id >= P_COUNT; i++)
        if (((e = ml_tab[i]) >> 9) && (e & 127u) == cc)
            id = ml_id(e >> 9);
    if (ml_arm || id >= P_COUNT) {
        (void)midi_learned(cc, 64u);                  /* (learning: heard; not learned: nothing) */
        return;
    }
    t = &trk[(e >> 7) & 3u];
    d = is_drum(t) && id == P_E0 ? &DRUM_KIT_DESC     /* (as midi_cc_set reads it) */
        : id >= P_E0 ? &ENGINES[t->eng_req % NENGINES]->edit[(id - P_E0) & 7u] : &TP[id % P_COUNT];
    if (d->max <= d->min)
        return;
    c0 = smc_cc_at(d, t->p[id]);
    c1 = smc_cc_at(d, smc_turn(d, t->p[id], delta));
    if (c1 == c0)                                     /* (a range wider than 127: a value step is less than a CC step) */
        c1 = (uint32_t)clamp((int32_t)c0 + delta, 0, 127);
    (void)midi_learned(cc, c1);
    smc_say((e >> 7) & 3u, d, t->p[id]);
}

/* a button pressed (note n): the rows REC SOLO MUTE SELECT 0x00 0x08 0x10 0x18 + strip, the transport, track < > */
UI_SMALL static void smc_button(uint32_t n)
{
    uint32_t k = n & 7u;
    if (n < 0x20u) {
        if (k >= NTRK)                                /* strips 5..8: nothing */
            return;
        if (n < 0x08u) {                              /* REC n: that track, then REC as on the FM-1 (rec_toggle) */
            track_select(k);
            rec_toggle();
        } else if (n < 0x10u) {                       /* SOLO n: SLOOP's solo (GLO + a key's) */
            song.solo ^= (uint8_t)(1u << k);
            smc.solo = (uint8_t)((smc.solo ^ (1u << k)) & song.solo);
        } else if (n < 0x18u) {
            trk[k].p[P_MUTE] = (int16_t)!trk[k].p[P_MUTE];
        } else {
            track_select(k);
        }
        ui.force = 1;
        return;
    }
    switch (n) {
    case 0x5Eu:                                       /* PLAY: stopped -> play */
        if (!song.playing && !ci_on)
            transport_req = 1;
        break;
    case 0x5Du:                                       /* STOP: playing or a count-in */
        if (song.playing || ci_on)
            transport_req = 2;
        break;
    case 0x5Fu:                                       /* REC: the selected track */
        rec_toggle();
        break;
    case 0x2Eu:
    case 0x30u:                                       /* BANK / CHANNEL <: the previous track (wrapping) */
        track_select((song.sel + NTRK - 1u) % NTRK);
        break;
    case 0x2Fu:
    case 0x31u:                                       /* BANK / CHANNEL >: the next */
        track_select((song.sel + 1u) % NTRK);
        break;
    default:                                          /* REW FF, the arrows: nothing */
        break;
    }
}

/* bt_midi_in in SMC mode: one USB-MIDI packet (CIN | status << 8 | d1 << 16 | d2 << 24) */
UI_SMALL static void smc_in(uint32_t pkt)
{
    uint32_t st = (pkt >> 8) & 0xFFu, ch = st & 15u, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
    int busy = ui.confirm || ui.hold_kind;            /* a dialog, a hold to confirm: the buttons and knobs wait */
    if (!smc.on)
        smc_fresh();
    switch (st >> 4) {
    case 0xEu:                                        /* fader ch + 1 (9: the master, ignored) */
        if (ch < SMC_FADERS)
            smc_fader(ch, (d2 << 7 | d1) >> 7);
        break;
    case 0xBu:                                        /* knobs 1..4, 5..8 MIDI LEARN's; bit 6 = counter-clockwise */
        if (!busy && !ch && d1 >= 16u && d1 <= 23u && (d2 & 0x3Fu)) {
            int32_t clicks = d2 & 0x40u ? -(int32_t)(d2 & 0x3Fu) : (int32_t)(d2 & 0x3Fu);
            if (d1 <= 19u)
                smc_knob(d1 - 16u, clicks);
            else
                smc_learnable(d1 - 16u, clicks);
        }
        break;
    case 0x9u:                                        /* velocity 0 = a release */
        if (!busy && !ch && d2)
            smc_button(d1);
        break;
    default:                                          /* note-offs (releases), the rest: nothing */
        break;
    }
}

/* core_ble_link: the link up / down (bt_link first: the mode). Up: everything sent again, the faders unpicked.
 * Down: solo off (no FM-1 control unsolos); nothing is cut, the tracks it silenced become audible */
UI_SMALL static void smc_link(int up)
{
    smc.up = (uint8_t)(up != 0);
    if (!up)
        smc_unsolo();
    smc.on = smc.dark = 0;
}

UI_SMALL static int smc_send(uint32_t a, uint32_t b, uint32_t c)   /* 1 = queued (ble_midi_send); 0: try again next frame */
{
    uint8_t m[3];
    m[0] = (uint8_t)a;
    m[1] = (uint8_t)b;
    m[2] = (uint8_t)c;
    return ble_midi_room() && ble_midi_send(m, 3u) == 1;   /* only what fits: out_drops = real losses */
}

/* bt_service, after ble_service (may run several times a frame): linked in SMC mode, the LED changes and the fader
 * positions waiting; a refused message stays waiting. Nothing changed: no message. */
UI_SMALL static void smc_frame(void)
{
    uint32_t w[2], i, j, b, m = 0;
    if (!smc.up || !bt_mode_now()) {
        if (smc.on && smc.up) {                       /* SMC mode ended while linked: unsolo, the LEDs off once */
            smc_unsolo();
            smc.dark = SMC_LEDS;
        }
        smc.on = 0;                                   /* SMC mode again: everything again */
        for (; smc.dark; smc.dark--)                  /* notes 0x00..0x1F, 0x5D..0x5F at 0 */
            if (!smc_send(0x90u, SMC_LEDS - smc.dark < 32u ? SMC_LEDS - smc.dark : 0x5Du + 3u - smc.dark, 0))
                return;
        return;
    }
    if (!smc.on)
        smc_fresh();
    for (i = 0; i < SMC_FADERS; i++)
        smc_notice(i);
    for (i = 0; i < NTRK; i++)
        m |= (uint32_t)(trk[i].p[P_MUTE] != 0) << i;
    w[0] = ((uint32_t)song.rec & 15u) | ((uint32_t)song.solo & 15u) << 8 | m << 16 | 1u << (24u + (song.sel & 3u));
    w[1] = (song.playing ? 2u : 1u) | (song.rec ? 4u : 0u);
    for (j = 0; j < 2u; j++) {
        uint32_t dirty = ((w[j] ^ smc.lit[j]) | ~smc.known[j]) & (j ? 7u : 0xFFFFFFFFu);
        for (i = 0; dirty && i < 32u; i++) {
            b = 1u << i;
            if (!(dirty & b))
                continue;
            if (!smc_send(0x90u, j ? 0x5Du + i : i, (w[j] & b) ? 127u : 0u))
                return;
            smc.lit[j] = (smc.lit[j] & ~b) | (w[j] & b);
            smc.known[j] |= b;
            dirty &= ~b;
        }
    }
    for (i = 0; smc.pend && i < SMC_FADERS; i++)
        if ((smc.pend >> i) & 1u) {
            if (!smc_send(0xE0u + i, smc.val[i], smc.val[i]))
                return;
            smc.pend &= (uint8_t)~(1u << i);
        }
}
