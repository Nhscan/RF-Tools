#include "rf_tools_analyzer_view.h"
#include <gui/elements.h>
#include <furi.h>

// Layout ported from applications/main/subghz/plugins/frequency_analyzer/
// subghz_frequency_analyzer.c's draw callback - same coordinates, so a user coming from the
// stock Frequency Analyzer sees a familiar screen.

struct RfToolsAnalyzerView {
    View* view;
    RfToolsAnalyzerViewCallback callback;
    void* context;
};

typedef struct {
    uint32_t frequency;
    float rssi;
    float trigger;
    bool signal_locked;
    bool is_external;
    uint32_t history[RF_TOOLS_ANALYZER_HISTORY];
    size_t history_count;
} RfToolsAnalyzerViewModel;

static void rf_tools_analyzer_draw_rssi(Canvas* canvas, float rssi, float trigger, int x, int y) {
    int w = (int)((rssi + 110.0f) * (60.0f / 80.0f));
    if(w < 0) w = 0;
    if(w > 60) w = 60;
    canvas_draw_frame(canvas, x, y, 62, 5);
    canvas_draw_box(canvas, x + 1, y + 1, w, 3);

    int tx = (int)((trigger + 110.0f) * (60.0f / 80.0f));
    if(tx < 0) tx = 0;
    if(tx > 60) tx = 60;
    canvas_draw_line(canvas, x + 1 + tx, y - 2, x + 1 + tx, y + 7);
}

static void rf_tools_analyzer_draw_history(Canvas* canvas, RfToolsAnalyzerViewModel* model) {
    // Single row, most-recent-2 only - a full 2x2 grid left no room for the RSSI bar above
    // the bottom button band without the two overlapping (elements_button_left/right claim
    // the whole y=52..64 strip - see rf_tools_analyzer_view_draw).
    const int x1 = 2, x2 = 66, y = 40;
    for(size_t i = 0; i < 2; i++) {
        int x = (i == 0) ? x1 : x2;
        FuriString* s = furi_string_alloc();
        if(i < model->history_count) {
            uint32_t f = model->history[i];
            furi_string_printf(s, "%03lu.%03lu MHz", f / 1000000 % 1000, f / 1000 % 1000);
        } else {
            furi_string_set(s, "---.--- MHz");
        }
        canvas_draw_str(canvas, x, y, furi_string_get_cstr(s));
        furi_string_free(s);
    }
}

static void rf_tools_analyzer_view_draw(Canvas* canvas, void* ctx) {
    RfToolsAnalyzerViewModel* model = ctx;

    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 7, "Freq Analyzer");
    canvas_draw_str_aligned(canvas, 126, 0, AlignRight, AlignTop, model->is_external ? "Ext" : "Int");

    canvas_set_font(canvas, FontBigNumbers);
    FuriString* freq = furi_string_alloc();
    furi_string_printf(
        freq, "%03lu.%03lu", model->frequency / 1000000 % 1000, model->frequency / 1000 % 1000);
    if(model->signal_locked) {
        canvas_draw_box(canvas, 4, 10, 121, 19);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_draw_str(canvas, 8, 26, furi_string_get_cstr(freq));
    furi_string_free(freq);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 98, 22, "MHz");
    canvas_set_color(canvas, ColorBlack);

    rf_tools_analyzer_draw_history(canvas, model);

    // RSSI bar sits in the one remaining free strip (y=43..51) - everything from y=52 down is
    // elements_button_left/right's own 12px-tall button band (button_height=12, drawn flush
    // to canvas_height), so nothing else may be drawn there or it overlaps the buttons.
    canvas_draw_str(canvas, 2, 50, "RSSI");
    rf_tools_analyzer_draw_rssi(canvas, model->rssi, model->trigger, 28, 44);

    elements_button_left(canvas, "T-");
    elements_button_right(canvas, "+T");
}

static bool rf_tools_analyzer_view_input(InputEvent* event, void* ctx) {
    RfToolsAnalyzerView* instance = ctx;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    switch(event->key) {
    case InputKeyLeft:
        if(instance->callback) instance->callback(RfToolsAnalyzerViewEventTriggerDown, instance->context);
        return true;
    case InputKeyRight:
        if(instance->callback) instance->callback(RfToolsAnalyzerViewEventTriggerUp, instance->context);
        return true;
    case InputKeyOk:
        if(instance->callback) instance->callback(RfToolsAnalyzerViewEventSelectSignal, instance->context);
        return true;
    case InputKeyBack:
        if(instance->callback) instance->callback(RfToolsAnalyzerViewEventBack, instance->context);
        return true;
    default:
        return false;
    }
}

RfToolsAnalyzerView* rf_tools_analyzer_view_alloc(void) {
    RfToolsAnalyzerView* instance = malloc(sizeof(RfToolsAnalyzerView));
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(RfToolsAnalyzerViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, rf_tools_analyzer_view_draw);
    view_set_input_callback(instance->view, rf_tools_analyzer_view_input);
    return instance;
}

void rf_tools_analyzer_view_free(RfToolsAnalyzerView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* rf_tools_analyzer_view_get_view(RfToolsAnalyzerView* instance) {
    return instance->view;
}

void rf_tools_analyzer_view_set_callback(
    RfToolsAnalyzerView* instance,
    RfToolsAnalyzerViewCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

void rf_tools_analyzer_view_reset(RfToolsAnalyzerView* instance, bool is_external) {
    with_view_model(
        instance->view,
        RfToolsAnalyzerViewModel * model,
        {
            model->frequency = 0;
            model->rssi = -127.0f;
            model->signal_locked = false;
            model->history_count = 0;
            model->is_external = is_external;
        },
        true);
}

void rf_tools_analyzer_view_update(
    RfToolsAnalyzerView* instance,
    uint32_t frequency,
    float rssi,
    float trigger,
    bool signal_locked) {
    with_view_model(
        instance->view,
        RfToolsAnalyzerViewModel * model,
        {
            model->frequency = frequency;
            model->rssi = rssi;
            model->trigger = trigger;
            model->signal_locked = signal_locked;
        },
        true);
}

void rf_tools_analyzer_view_push_history(RfToolsAnalyzerView* instance, uint32_t frequency) {
    with_view_model(
        instance->view,
        RfToolsAnalyzerViewModel * model,
        {
            for(size_t i = RF_TOOLS_ANALYZER_HISTORY - 1; i > 0; i--) {
                model->history[i] = model->history[i - 1];
            }
            model->history[0] = frequency;
            if(model->history_count < RF_TOOLS_ANALYZER_HISTORY) model->history_count++;
        },
        true);
}

uint32_t rf_tools_analyzer_view_get_locked_frequency(RfToolsAnalyzerView* instance) {
    uint32_t freq = 0;
    with_view_model(
        instance->view, RfToolsAnalyzerViewModel * model, { freq = model->frequency; }, false);
    return freq;
}
