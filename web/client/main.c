// raylib in the browser for an APL program: draws the frames web/Remote.aplc sends over a WebSocket and reports input.
// Everything the player sees is decided in APL; this only replays draw calls and collects mouse and keyboard input.
// The protocol is described at the top of web/Remote.aplc.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten/emscripten.h>
#include <emscripten/websocket.h>
#include <emscripten/html5.h>
#include "raylib.h"

static EMSCRIPTEN_WEBSOCKET_T ws;
static int state = 0;                      // 0 connecting, 1 open, 2 closed by APL, 3 connection lost
static char *frame = NULL;                 // the last frame, drawn on every browser frame
static int buttons[16], nbuttons = 0;      // pressed since the last input report
static int keys[64], nkeys = 0;

EM_JS(char *, socket_url, (void), {
    return stringToNewUTF8((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
});

static void send_text(const char *s) {
    if (state == 1) emscripten_websocket_send_utf8_text(ws, s);
}

static void send_hello(void) {
    char msg[2048] = "H", c[8];
    for (int i = 32; i < 256; i++) {        // the default font's glyphs: ASCII and Latin-1
        int n;
        memcpy(c, CodepointToUTF8(i, &n), n);
        c[n] = 0;
        snprintf(msg + strlen(msg), sizeof msg - strlen(msg), " %d", MeasureText(c, 10));
    }
    send_text(msg);
}

static void send_input(void) {
    char msg[1024];
    Vector2 m = GetMousePosition();
    int n = snprintf(msg, sizeof msg, "I %g %g %d %d %d %d|", m.x, m.y,
                     GetScreenWidth(), GetScreenHeight(), GetRenderWidth(), GetRenderHeight());
    for (int i = 0; i < nbuttons; i++) n += snprintf(msg + n, sizeof msg - n, " %d", buttons[i]);
    n += snprintf(msg + n, sizeof msg - n, "|");
    for (int i = 0; i < nkeys; i++) n += snprintf(msg + n, sizeof msg - n, " %d", keys[i]);
    nbuttons = nkeys = 0;
    send_text(msg);
}

// Presses come from the browser's events rather than IsMouseButtonPressed, which compares the button between frames
// and so misses a click whose press and release fall within one frame.
static EM_BOOL on_mousedown(int type, const EmscriptenMouseEvent *e, void *user) {
    static const int raylib[] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_MIDDLE, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_BACK, MOUSE_BUTTON_FORWARD };
    if (e->button < 5 && nbuttons < 16) buttons[nbuttons++] = raylib[e->button];
    return EM_FALSE;
}

static void collect_input(void) {
    for (int k; (k = GetKeyPressed()) != 0;)
        if (nkeys < 64) keys[nkeys++] = k;
}

static Color rgba(const float *v) { return (Color){ v[0], v[1], v[2], v[3] }; }

// One draw call: a letter, numbers, and for text the string after them. Numbers are read as floats, since APL may send any.
static void draw_line(const char *s) {
    float v[8];
    int n = 0, k = sscanf(s + 1, "%f %f %f %f %f %f %f %f%n", v, v + 1, v + 2, v + 3, v + 4, v + 5, v + 6, v + 7, &n);
    switch (s[0]) {
    case 'C': if (k >= 4) ClearBackground(rgba(v)); break;
    case 'R': if (k == 8) DrawRectangle(v[0], v[1], v[2], v[3], rgba(v + 4)); break;
    case 'L': if (k == 8) DrawLine(v[0], v[1], v[2], v[3], rgba(v + 4)); break;
    case 'O': if (k >= 7) DrawCircle(v[0], v[1], v[2], rgba(v + 3)); break;
    case 'o': if (k >= 7) DrawCircleLines(v[0], v[1], v[2], rgba(v + 3)); break;
    case 'T':                                // T x y size r g b a text: the text starts after one space
        if (sscanf(s + 1, "%f %f %f %f %f %f %f %n", v, v + 1, v + 2, v + 3, v + 4, v + 5, v + 6, &n) == 7)
            DrawText(s + 1 + n, v[0], v[1], v[2], rgba(v + 3));
        break;
    }
}

static void draw_frame(void) {
    char *f = strdup(frame);
    for (char *line = strtok(f, "\n"); line; line = strtok(NULL, "\n")) draw_line(line);
    free(f);
}

static void notice(const char *s) {
    int w = MeasureText(s, 24);
    DrawRectangle(0, GetScreenHeight() / 2 - 30, GetScreenWidth(), 60, (Color){ 16, 18, 22, 220 });
    DrawText(s, (GetScreenWidth() - w) / 2, GetScreenHeight() / 2 - 12, 24, (Color){ 226, 229, 233, 255 });
}

static void tick(void) {
    collect_input();
    BeginDrawing();
    ClearBackground((Color){ 28, 32, 38, 255 });
    if (frame) draw_frame();
    if (state == 0) notice("Connecting...");
    if (state == 3) notice("Connection lost - reload to play again");
    if (state == 2) notice("Game closed - reload to play again");
    EndDrawing();
}

static EM_BOOL on_message(int type, const EmscriptenWebSocketMessageEvent *e, void *user) {
    if (!e->isText) return EM_TRUE;
    char *s = malloc(e->numBytes + 1);
    memcpy(s, e->data, e->numBytes);
    s[e->numBytes] = 0;
    if (s[0] == 'F') {                       // a frame: keep it, and answer with the input collected meanwhile
        free(frame);
        frame = s;
        send_input();
        return EM_TRUE;
    }
    if (s[0] == 'W') {                       // InitWindow: size the canvas, then say hello
        int w, h, n = 0;
        if (sscanf(s + 1, "%d %d %n", &w, &h, &n) >= 2) {
            SetWindowSize(w, h);
            SetWindowTitle(s + 1 + n);
        }
        send_hello();
        send_input();
    }
    if (s[0] == 'Q') state = 2;
    free(s);
    return EM_TRUE;
}

static EM_BOOL on_open(int type, const EmscriptenWebSocketOpenEvent *e, void *user) { state = 1; return EM_TRUE; }
static EM_BOOL on_close(int type, const EmscriptenWebSocketCloseEvent *e, void *user) {
    if (state != 2) state = 3;
    return EM_TRUE;
}
static EM_BOOL on_error(int type, const EmscriptenWebSocketErrorEvent *e, void *user) {
    if (state != 2) state = 3;
    return EM_TRUE;
}

int main(void) {
    InitWindow(800, 800, "APL Othello");
    SetExitKey(KEY_NULL);                    // Esc goes to APL, which decides when to close
    emscripten_set_mousedown_callback("#canvas", NULL, EM_FALSE, on_mousedown);
    char *url = socket_url();
    EmscriptenWebSocketCreateAttributes a = { url, NULL, EM_TRUE };
    ws = emscripten_websocket_new(&a);
    free(url);
    emscripten_websocket_set_onopen_callback(ws, NULL, on_open);
    emscripten_websocket_set_onmessage_callback(ws, NULL, on_message);
    emscripten_websocket_set_onclose_callback(ws, NULL, on_close);
    emscripten_websocket_set_onerror_callback(ws, NULL, on_error);
    emscripten_set_main_loop(tick, 0, 1);
    return 0;
}
