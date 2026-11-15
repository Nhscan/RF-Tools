#include "rf_tools_capture_view.h"
#include <gui/elements.h>
#include <furi.h>

// Layout mirrors applications/main/subghz/views/subghz_read_raw.c: freq/preset/device all on
// one header line at distinct x offsets so they can never collide, a sample counter
// right-aligned on the same line, then a separator and the live status beneath it.

struct RfToolsCaptureView {
    View* view;
    RfToolsCaptureViewCallback callback;
    void* context;
};

typedef struct {
    FuriString* frequency_str;
    FuriString* preset_str;
    bool is_external;
    bool raw_mode;
    bool idle; // raw mode only: waiting for OK to start recording
    uint32_t elapsed_s;
    size_t raw_edge_count;
    // Repeater Auto-TX: once a signal decodes, the view switches to showing what was decoded
    // plus a countdown to the actual retransmit instead of going blank/back to "Listening" -
    // the radio really has stopped listening at this point (capture is over), this is just
    // making that visible instead of leaving the screen looking unchanged.
    bool pending_tx;
    FuriString* decoded_text;
    uint32_t countdown_s;
} RfToolsCaptureViewModel;

static void rf_tools_capture_view_draw(Canvas* canvas, void* ctx) {
    RfToolsCaptureViewModel* model = ctx;

    canvas_clear(canvas);

    if(model->pending_tx) {
        canvas_set_font(canvas, FontSecondary);
        const char* line = furi_string_get_cstr(model->decoded_text);
        for(uint8_t row = 0; row < 3 && *line; row++) {
            const char* end = strchr(line, '\n');
            size_t length = end ? (size_t)(end - line) : strlen(line);
            FuriString* text = furi_string_alloc();
            furi_string_set_strn(text, line, length);
            furi_string_trim(text);
            elements_string_fit_width(canvas, text, 126);
            canvas_draw_str(canvas, 0, 8 + row * 10, furi_string_get_cstr(text));
            furi_string_free(text);
            if(!end) break;
            line = end + 1;
        }

        canvas_draw_line(canvas, 0, 32, 127, 32);
        canvas_set_font(canvas, FontPrimary);
        FuriString* countdown = furi_string_alloc();
        if(model->countdown_s > 0) {
            furi_string_printf(countdown, "TX in %lus", model->countdown_s);
        } else {
            furi_string_set(countdown, "Sending...");
        }
        canvas_draw_str_aligned(canvas, 64, 45, AlignCenter, AlignBottom, furi_string_get_cstr(countdown));
        furi_string_free(countdown);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "Back: Cancel");
        return;
    }

    canvas_set_font(canvas, FontSecondary);

    // Two header lines, each with just two short items - packing freq+preset+device+count
    // onto a single 128px-wide line was what actually caused the text to run together.
    canvas_draw_str(canvas, 0, 7, furi_string_get_cstr(model->frequency_str));
    canvas_draw_str_aligned(canvas, 126, 0, AlignRight, AlignTop, model->is_external ? "Ext" : "Int");

    canvas_draw_str(canvas, 0, 17, furi_string_get_cstr(model->preset_str));
    FuriString* count = furi_string_alloc();
    furi_string_printf(count, "%zu edges", model->raw_edge_count);
    canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignTop, furi_string_get_cstr(count));
    furi_string_free(count);

    canvas_draw_line(canvas, 0, 20, 127, 20);

    canvas_set_font(canvas, FontPrimary);
    const char* title = model->idle ? "Ready to record" : model->raw_mode ? "Recording (Raw)" : "Listening";
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, title);

    canvas_set_font(canvas, FontSecondary);
    if(!model->idle) {
        FuriString* elapsed = furi_string_alloc();
        furi_string_printf(elapsed, "%lus", model->elapsed_s);
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, furi_string_get_cstr(elapsed));
        furi_string_free(elapsed);
    }

    canvas_draw_line(canvas, 0, 50, 127, 50);
    elements_button_center(canvas, model->idle ? "Record" : "Stop");
}

static bool rf_tools_capture_view_input(InputEvent* event, void* ctx) {
    RfToolsCaptureView* instance = ctx;
    if(event->type != InputTypeShort) return false;

    if(event->key == InputKeyOk) {
        if(instance->callback) instance->callback(RfToolsCaptureViewEventStop, instance->context);
        return true;
    } else if(event->key == InputKeyBack) {
        if(instance->callback) instance->callback(RfToolsCaptureViewEventBack, instance->context);
        return true;
    }
    return false;
}

RfToolsCaptureView* rf_tools_capture_view_alloc(void) {
    RfToolsCaptureView* instance = malloc(sizeof(RfToolsCaptureView));
    instance->view = view_alloc();
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(RfToolsCaptureViewModel));
    view_set_context(instance->view, instance);
    view_set_draw_callback(instance->view, rf_tools_capture_view_draw);
    view_set_input_callback(instance->view, rf_tools_capture_view_input);

    with_view_model(
        instance->view,
        RfToolsCaptureViewModel * model,
        {
            model->frequency_str = furi_string_alloc();
            model->preset_str = furi_string_alloc();
            model->decoded_text = furi_string_alloc();
        },
        false);

    return instance;
}

void rf_tools_capture_view_free(RfToolsCaptureView* instance) {
    furi_assert(instance);
    with_view_model(
        instance->view,
        RfToolsCaptureViewModel * model,
        {
            furi_string_free(model->frequency_str);
            furi_string_free(model->preset_str);
            furi_string_free(model->decoded_text);
        },
        false);
    view_free(instance->view);
    free(instance);
}

View* rf_tools_capture_view_get_view(RfToolsCaptureView* instance) {
    return instance->view;
}

void rf_tools_capture_view_set_callback(
    RfToolsCaptureView* instance,
    RfToolsCaptureViewCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

void rf_tools_capture_view_reset(
    RfToolsCaptureView* instance,
    uint32_t frequency,
    const char* preset_name,
    bool is_external,
    bool raw_mode) {
    with_view_model(
        instance->view,
        RfToolsCaptureViewModel * model,
        {
            furi_string_printf(
                model->frequency_str, "%lu.%02lu MHz", frequency / 1000000, (frequency / 10000) % 100);
            furi_string_set(model->preset_str, preset_name);
            model->is_external = is_external;
            model->raw_mode = raw_mode;
            model->idle = false;
            model->elapsed_s = 0;
            model->raw_edge_count = 0;
            model->pending_tx = false;
        },
        true);
}

void rf_tools_capture_view_set_idle(RfToolsCaptureView* instance, bool idle) {
    with_view_model(instance->view, RfToolsCaptureViewModel * model, { model->idle = idle; }, true);
}

void rf_tools_capture_view_set_pending_tx(
    RfToolsCaptureView* instance,
    const char* decoded_text,
    uint32_t countdown_s) {
    with_view_model(
        instance->view,
        RfToolsCaptureViewModel * model,
        {
            model->pending_tx = true;
            furi_string_set(model->decoded_text, decoded_text);
            model->countdown_s = countdown_s;
        },
        true);
}

void rf_tools_capture_view_set_countdown(RfToolsCaptureView* instance, uint32_t countdown_s) {
    with_view_model(
        instance->view, RfToolsCaptureViewModel * model, { model->countdown_s = countdown_s; }, true);
}

void rf_tools_capture_view_update(
    RfToolsCaptureView* instance,
    uint32_t elapsed_s,
    size_t raw_edge_count) {
    with_view_model(
        instance->view,
        RfToolsCaptureViewModel * model,
        {
            model->elapsed_s = elapsed_s;
            model->raw_edge_count = raw_edge_count;
        },
        true);
}
