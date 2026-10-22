#include "ui_voice.h"

#include "behaviour.h"
#include "eyes.h"

void ui_voice_show(voice_state_t state)
{
    if (state == VOICE_IDLE) {
        eyes_set_level(0);
    }
    behaviour_voice(state);
}

void ui_voice_set_level(float level)
{
    eyes_set_level(level);
}

void ui_voice_countdown(uint32_t ms)
{
    eyes_countdown(ms);
}
