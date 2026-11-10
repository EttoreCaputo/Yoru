#include "wake_mww.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "frontend.h"
#include "frontend_util.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/micro/schema/schema_generated.h"

static const char *TAG = "wake_mww";

/* The spectrogram every microWakeWord model is trained on: 40 mel bands of
 * a 30 ms window every 10 ms. These must stay as the trainer has them. */
#define FEATURES 40
#define WINDOW_MS 30
#define STEP_MS 10
#define SAMPLE_RATE 16000

/* The model keeps its streaming state in resource variables, in an arena of
 * their own. */
#define VARIABLE_ARENA 1024
#define VARIABLES 20
/* Steps ignored after a start or a reset: the model's state is still empty
 * and its first guesses mean nothing (one second). */
#define WARM_UP_STEPS 100
/* The probabilities averaged for a decision: at most this many. */
#define WINDOW_MAX 10

typedef struct {
    const char *id;
    const char *word;
    const uint8_t *start;
    const uint8_t *end;
    /* As in an ESPHome model's manifest: the average of the last `window`
     * probabilities (0-255) must pass `cutoff`. */
    uint8_t cutoff;
    uint8_t window;
    /* The tensor arena to start with; tried at 1.5 and 2 times too,
     * since esp-nn's kernels want more on some chips. */
    size_t arena;
} model_t;

extern const uint8_t hey_yoru_start[] asm("_binary_hey_yoru_tflite_start");
extern const uint8_t hey_yoru_end[] asm("_binary_hey_yoru_tflite_end");

/* «Hey Yoru», trained with tools/wakeword/train.py. The cutoff is 0.90: in
 * the training's test that misses about one word in six and wakes by
 * mistake less than once in five hours of people talking at a table. */
static const model_t MODELS[] = {
    {"heyyoru", "Hey Yoru", hey_yoru_start, hey_yoru_end, 230, 5, 26000},
};
#define MODEL_COUNT ((int) (sizeof(MODELS) / sizeof(MODELS[0])))

typedef tflite::MicroMutableOpResolver<20> resolver_t;

static struct {
    const model_t *model;
    struct FrontendState frontend;
    bool frontend_up;
    resolver_t *resolver;
    uint8_t *variable_arena;
    uint8_t *arena;
    tflite::MicroInterpreter *interpreter;
    int stride;      /* spectrogram slices the model takes at a time */
    int stride_step; /* how many of them are in its input now */
    uint8_t recent[WINDOW_MAX];
    int recent_at;
    int ignore; /* steps left before a detection counts */
    /* debug/perf */
    uint8_t peak, peak_next;
    int peak_steps;
    int64_t step_us_sum;
    int step_count;
    int step_us;
} s;

int wake_mww_count(void)
{
    return MODEL_COUNT;
}

const char *wake_mww_id(int i)
{
    return i >= 0 && i < MODEL_COUNT ? MODELS[i].id : NULL;
}

const char *wake_mww_word(int i)
{
    return i >= 0 && i < MODEL_COUNT ? MODELS[i].word : NULL;
}

int wake_mww_find(const char *id)
{
    for (int i = 0; id != NULL && i < MODEL_COUNT; i++) {
        if (strcmp(id, MODELS[i].id) == 0) {
            return i;
        }
    }
    return -1;
}

/* The operations a streaming microWakeWord model is made of. */
static bool add_ops(resolver_t *r)
{
    return r->AddCallOnce() == kTfLiteOk && r->AddVarHandle() == kTfLiteOk && r->AddReshape() == kTfLiteOk
           && r->AddReadVariable() == kTfLiteOk && r->AddStridedSlice() == kTfLiteOk
           && r->AddConcatenation() == kTfLiteOk && r->AddAssignVariable() == kTfLiteOk && r->AddConv2D() == kTfLiteOk
           && r->AddMul() == kTfLiteOk && r->AddAdd() == kTfLiteOk && r->AddMean() == kTfLiteOk
           && r->AddFullyConnected() == kTfLiteOk && r->AddLogistic() == kTfLiteOk && r->AddQuantize() == kTfLiteOk
           && r->AddDepthwiseConv2D() == kTfLiteOk && r->AddAveragePool2D() == kTfLiteOk
           && r->AddMaxPool2D() == kTfLiteOk && r->AddPad() == kTfLiteOk && r->AddPack() == kTfLiteOk
           && r->AddSplitV() == kTfLiteOk;
}

static bool frontend_start(void)
{
    struct FrontendConfig cfg;
    FrontendFillConfigWithDefaults(&cfg);
    cfg.window.size_ms = WINDOW_MS;
    cfg.window.step_size_ms = STEP_MS;
    cfg.filterbank.num_channels = FEATURES;
    cfg.filterbank.lower_band_limit = 125.0f;
    cfg.filterbank.upper_band_limit = 7500.0f;
    cfg.noise_reduction.smoothing_bits = 10;
    cfg.noise_reduction.even_smoothing = 0.025f;
    cfg.noise_reduction.odd_smoothing = 0.06f;
    cfg.noise_reduction.min_signal_remaining = 0.05f;
    cfg.pcan_gain_control.enable_pcan = 1;
    cfg.pcan_gain_control.strength = 0.95f;
    cfg.pcan_gain_control.offset = 80.0f;
    cfg.pcan_gain_control.gain_bits = 21;
    cfg.log_scale.enable_log = 1;
    cfg.log_scale.scale_shift = 6;
    s.frontend_up = FrontendPopulateState(&cfg, &s.frontend, SAMPLE_RATE) != 0;
    return s.frontend_up;
}

/* A new interpreter over `arena`; NULL when the model doesn't fit in it or
 * isn't a microWakeWord one. The variables start again with it. */
static tflite::MicroInterpreter *interpreter_new(const model_t *m, uint8_t *arena, size_t size)
{
    tflite::MicroAllocator *allocator = tflite::MicroAllocator::Create(s.variable_arena, VARIABLE_ARENA);
    tflite::MicroResourceVariables *variables = tflite::MicroResourceVariables::Create(allocator, VARIABLES);
    tflite::MicroInterpreter *it =
        new tflite::MicroInterpreter(tflite::GetModel(m->start), *s.resolver, arena, size, variables);
    if (it->AllocateTensors() != kTfLiteOk) {
        delete it;
        return NULL;
    }
    const TfLiteTensor *in = it->input(0);
    const TfLiteTensor *out = it->output(0);
    if (in->dims->size != 3 || in->dims->data[0] != 1 || in->dims->data[2] != FEATURES || in->type != kTfLiteInt8
        || out->dims->size != 2 || out->dims->data[0] != 1 || out->dims->data[1] != 1 || out->type != kTfLiteUInt8) {
        ESP_LOGE(TAG, "%s isn't a streaming microWakeWord model", m->id);
        delete it;
        return NULL;
    }
    return it;
}

void wake_mww_reset(void)
{
    memset(s.recent, 0, sizeof(s.recent));
    s.recent_at = 0;
    s.ignore = WARM_UP_STEPS;
}

void wake_mww_stop(void)
{
    delete s.interpreter;
    s.interpreter = NULL;
    heap_caps_free(s.arena);
    s.arena = NULL;
    heap_caps_free(s.variable_arena);
    s.variable_arena = NULL;
    delete s.resolver;
    s.resolver = NULL;
    if (s.frontend_up) {
        FrontendFreeStateContents(&s.frontend);
        s.frontend_up = false;
    }
    s.model = NULL;
}

bool wake_mww_start(int i)
{
    wake_mww_stop();
    if (i < 0 || i >= MODEL_COUNT) {
        return false;
    }
    const model_t *m = &MODELS[i];
    if (tflite::GetModel(m->start)->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "%s: unsupported model schema", m->id);
        return false;
    }
    s.resolver = new resolver_t();
    // PSRAM: internal RAM is for the WiFi and the screen (CLAUDE.md).
    s.variable_arena = (uint8_t *) heap_caps_malloc(VARIABLE_ARENA, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s.variable_arena == NULL || !add_ops(s.resolver) || !frontend_start()) {
        ESP_LOGE(TAG, "%s: out of memory", m->id);
        wake_mww_stop();
        return false;
    }
    const size_t sizes[] = {m->arena, m->arena * 3 / 2, m->arena * 2};
    size_t size = 0;
    for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]) && s.interpreter == NULL; k++) {
        size = (sizes[k] + 15) & ~(size_t) 15;
        s.arena = (uint8_t *) heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s.arena == NULL) {
            break;
        }
        s.interpreter = interpreter_new(m, s.arena, size);
        if (s.interpreter == NULL) {
            heap_caps_free(s.arena);
            s.arena = NULL;
        }
    }
    if (s.interpreter == NULL) {
        ESP_LOGE(TAG, "%s didn't load", m->id);
        wake_mww_stop();
        return false;
    }
    s.model = m;
    s.stride = s.interpreter->input(0)->dims->data[1];
    s.stride_step = 0;
    s.peak = s.peak_next = 0;
    s.peak_steps = 0;
    s.step_us_sum = 0;
    s.step_count = 0;
    s.step_us = 0;
    wake_mww_reset();
    ESP_LOGI(TAG, "\"%s\": %u KB model, %u of %u bytes of arena, %d slices a step, cutoff %d over %d", m->word,
             (unsigned) ((m->end - m->start) / 1024), (unsigned) s.interpreter->arena_used_bytes(), (unsigned) size,
             s.stride, m->cutoff, m->window);
    return true;
}

/* One spectrogram slice into the model; true when it ran and has a new
 * probability. */
static bool model_step(const struct FrontendOutput *slice)
{
    int8_t *in = tflite::GetTensorData<int8_t>(s.interpreter->input(0)) + FEATURES * s.stride_step;
    for (size_t i = 0; i < FEATURES; i++) {
        // The frontend gives about 0 to 670; the trainer divides it by 25.6
        // and the quantized model takes 0.0-26.0 as -128..127, so:
        // in = slice * 256 / (25.6 * 26) - 128.
        int32_t v = ((int32_t) slice->values[i] * 256 + 333) / 666 - 128;
        in[i] = (int8_t) (v < -128 ? -128 : v > 127 ? 127 : v);
    }
    if (++s.stride_step < s.stride) {
        return false;
    }
    s.stride_step = 0;
    const int64_t t0 = esp_timer_get_time();
    if (s.interpreter->Invoke() != kTfLiteOk) {
        ESP_LOGW(TAG, "invoke failed");
        return false;
    }
    s.step_us_sum += esp_timer_get_time() - t0;
    if (++s.step_count >= 64) {
        s.step_us = (int) (s.step_us_sum / s.step_count);
        s.step_us_sum = 0;
        s.step_count = 0;
    }
    const uint8_t p = s.interpreter->output(0)->data.uint8[0];
    s.recent_at = (s.recent_at + 1) % s.model->window;
    s.recent[s.recent_at] = p;
    if (p > s.peak_next) {
        s.peak_next = p;
    }
    if (++s.peak_steps >= 1000 / (STEP_MS * s.stride)) {
        s.peak = s.peak_next;
        s.peak_next = 0;
        s.peak_steps = 0;
    }
    return true;
}

bool wake_mww_feed(const int16_t *samples, size_t count)
{
    if (s.model == NULL) {
        return false;
    }
    bool heard = false;
    while (count > 0) {
        size_t read = 0;
        struct FrontendOutput slice = FrontendProcessSamples(&s.frontend, samples, count, &read);
        samples += read;
        count -= read;
        if (slice.size != FEATURES) {
            if (read == 0) {
                break;
            }
            continue; // not a whole window yet
        }
        if (s.ignore > 0) {
            s.ignore--;
        }
        if (!model_step(&slice) || s.ignore > 0) {
            continue;
        }
        uint32_t sum = 0;
        for (int i = 0; i < s.model->window; i++) {
            sum += s.recent[i];
        }
        if (sum > (uint32_t) s.model->cutoff * s.model->window) {
            ESP_LOGI(TAG, "\"%s\" heard: average %u of 255", s.model->word, (unsigned) (sum / s.model->window));
            // One wake a word: the same sounds mustn't count twice.
            wake_mww_reset();
            heard = true;
        }
    }
    return heard;
}

int wake_mww_peak(void)
{
    return s.model != NULL ? s.peak : 0;
}

int wake_mww_step_us(void)
{
    return s.model != NULL ? s.step_us : 0;
}
