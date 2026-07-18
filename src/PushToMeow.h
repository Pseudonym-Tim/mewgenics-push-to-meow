#ifndef PUSH_TO_MEOW_H
#define PUSH_TO_MEOW_H

#define RVA_CHARACTER_BEGIN_TURN 0x00109FA0ULL // Called when a character begins its combat turn...
#define RVA_CHARACTER_END_TURN 0x0010B250ULL // Called when a character finishes its combat turn...
#define RVA_INPUT_CORE_SYSTEM_EVENT 0x00A21380ULL // Processes core input-system events such as key presses...
#define RVA_FIND_CAT_MOVIE_COMPONENT 0x0006BE10ULL // Finds a character's cat movie/animation component...
#define RVA_FIND_CAT_VOICE_COMPONENT 0x00090090ULL // Finds a character's cat voice component...
#define RVA_PLAY_CAT_ACTION 0x000917F0ULL // Plays a cat action or associated animation...
#define RVA_COMBAT_CAT_VOICE_UPDATE 0x000EF6A0ULL // Updates combat cat-voice state each tick...
#define RVA_GET_COMBAT_CAT_VOICE 0x00121690ULL // Retrieves the combat cat-voice component for a character...
#define RVA_PLAY_CAT_VOICE 0x00743250ULL // Plays a selected cat voice sound...

#define CHARACTER_IS_PLAYER_CAT_OFFSET 0x489ULL
#define CHARACTER_COMPONENT_OWNER_POINTER_OFFSET 0x18ULL
#define COMBAT_VOICE_CHARACTER_OFFSET   0x20ULL
#define COMBAT_VOICE_TIME_SOURCE_OFFSET 0x18ULL
#define COMBAT_VOICE_TIME_SCALE_OFFSET  0x28ULL
#define COMBAT_VOICE_LOCAL_SCALE_OFFSET 0x30ULL
#define COMBAT_VOICE_CAT_PARTS_OFFSET 0x50ULL
#define CAT_PARTS_VISUALS_OFFSET 0x48ULL
#define CAT_VISUALS_TALKING_OFFSET 0x48ULL
#define SDL_SCANCODE_M 16
#define SDL_EVENT_KEY_DOWN 0x300U
#define SDL_EVENT_KEY_UP 0x301U
#define SDL_EVENT_TIMESTAMP_OFFSET 0x08U
#define SDL_KEYBOARD_SCANCODE_OFFSET 0x18U
#define HOLD_THRESHOLD_NS 350000000ULL
#define HOLD_THRESHOLD_SECONDS 0.35
#define TALKING_DURATION_SECONDS 0.85

#define STOLEN_BEGIN_TURN 14
#define STOLEN_END_TURN 14
#define STOLEN_INPUT_EVENT 15
#define STOLEN_COMBAT_VOICE_UPDATE 20

#define HOOK_PRIORITY 20
#define MOD_NAME "Push To Meow"

#endif