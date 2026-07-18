#include "PushToMeow.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "mewjector.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uintptr_t uptr;

#define CALL

static int resolve_mewjector(MewjectorAPI* api)
{
    return MJ_Resolve(api);
}

static void* game_image_base(void)
{
    return (void*)GetModuleHandleW(NULL);
}

static void zero(void* memory, uptr bytes)
{
    memset(memory, 0, (size_t)bytes);
}

typedef void (CALL *CharacterBeginTurnFn)(void* character, int turn_kind);
typedef void (CALL *CharacterEndTurnFn)(void* character);
typedef void (CALL *InputCoreSystemEventFn)(void* input_core, void* event);
typedef void (CALL *CombatCatVoiceUpdateFn)(void* voice_component);
typedef void* (CALL *FindCatComponentFn)(void* component_owner);
typedef void* (CALL *GetCombatCatVoiceFn)(void* character);
typedef void (CALL *PlayCatActionFn)(void* action_closure);

typedef struct MsvcString {
    union {
        char small[16];
        char* pointer;
    } storage;
    u64 size;
    u64 capacity;
} MsvcString;

typedef void (CALL *PlayCatVoiceFn)(void* cat_parts, MsvcString* voice, int force, double pitch, double volume);

typedef struct CatActionEntry {
    u8 unused[0x18];
    void* component_owner;
} CatActionEntry;

typedef struct CatActionData {
    u8 unused_before_entries[0x40];
    void** entries;
    u8 unused_before_animation[0x40];
    MsvcString animation;
    MsvcString voice;
} CatActionData;

typedef struct CatActionClosure {
    void* unused_vtable;
    CatActionData* data;
    u32 entry_index;
    u32 unused;
} CatActionClosure;

static MewjectorAPI g_mewjector;
static CharacterBeginTurnFn g_original_begin_turn;
static CharacterEndTurnFn g_original_end_turn;
static InputCoreSystemEventFn g_original_input_event;
static CombatCatVoiceUpdateFn g_original_combat_voice_update;
static FindCatComponentFn g_find_cat_movie_component;
static FindCatComponentFn g_find_cat_voice_component;
static GetCombatCatVoiceFn g_get_combat_cat_voice;
static PlayCatActionFn g_play_cat_action;
static PlayCatVoiceFn g_play_cat_voice;

static void* volatile g_active_player_cat;
static void* volatile g_m_press_cat;
static void* volatile g_talking_voice_component;
static volatile int g_ready;
static int g_m_was_down;
static int g_m_hold_fired;
static u64 g_m_down_timestamp_ns;
static double g_m_hold_seconds_remaining;
static double g_talking_seconds_remaining;

static void Log(const char* message)
{
    if (g_mewjector.Log) { g_mewjector.Log(MOD_NAME, message); }
}

static int is_player_cat(void* character)
{
    const volatile u8* bytes;
    if (!character) { return 0; }
    bytes = (const volatile u8*)character;
    return bytes[CHARACTER_IS_PLAYER_CAT_OFFSET] != 0;
}

static void make_sso_string(MsvcString* value, const char* text, uptr length)
{
    u8* bytes = (u8*)value;
    uptr i;
    for (i = 0; i < (uptr)sizeof(*value); ++i) bytes[i] = 0;
    for (i = 0; i < length && i < 15; ++i) value->storage.small[i] = text[i];
    value->size = length;
    value->capacity = 15;
}

static int get_active_cat(void** character_out, void** component_owner_out)
{
    void* character = (void*)g_active_player_cat;
    void* component_owner;

    if (!g_ready) { return 0; }
    if (!character) { return 0; }

    if (!is_player_cat(character)) 
    {
        g_active_player_cat = (void*)0;
        return 0;
    }

    if (character != (void*)g_active_player_cat) { return 0; }

    component_owner = *(void**)((u8*)character + CHARACTER_COMPONENT_OWNER_POINTER_OFFSET);

    if (!component_owner) { return 0; }

    if (character_out) *character_out = character;
    if (component_owner_out) *component_owner_out = component_owner;

    return 1;
}

static int set_native_talking(void* voice_component, int talking)
{
    void* cat_parts;
    void* visuals;

    if (!voice_component) { return 0; }

    cat_parts = *(void**)((u8*)voice_component + COMBAT_VOICE_CAT_PARTS_OFFSET);
    if (!cat_parts) { return 0; }

    visuals = *(void**)((u8*)cat_parts + CAT_PARTS_VISUALS_OFFSET);
    if (!visuals) { return 0; }

    *(volatile u8*)((u8*)visuals + CAT_VISUALS_TALKING_OFFSET) = talking ? 1 : 0;

    return 1;
}

static void stop_talking_override(void)
{
    void* voice_component = (void*)g_talking_voice_component;
    g_talking_voice_component = (void*)0;
    g_talking_seconds_remaining = 0.0;
    if (voice_component) (void)set_native_talking(voice_component, 0);
}

static double combat_voice_delta_seconds(void* voice_component)
{
    void* time_source;
    void* time_scale;
    double delta = 1.0 / 60.0;
    double candidate;

    if (!voice_component) { return delta; }

    time_source = *(void**)((u8*)voice_component + COMBAT_VOICE_TIME_SOURCE_OFFSET);
    time_scale = *(void**)((u8*)voice_component + COMBAT_VOICE_TIME_SCALE_OFFSET);

    if (!time_source || !time_scale) { return delta; }

    candidate = *(double*)((u8*)time_source + 0x10ULL);
    candidate *= *(double*)((u8*)voice_component + COMBAT_VOICE_LOCAL_SCALE_OFFSET);
    candidate *= *(double*)((u8*)time_scale + 0x28ULL);
    
    if (candidate > 0.0 && candidate < 0.25) delta = candidate;

    return delta;
}

static void try_tap_meow(void)
{
    void* character;
    void* component_owner;
    void* cat_parts;
    void* voice_component;
    MsvcString voice;

    if (!get_active_cat(&character, &component_owner)) 
    {
        Log("Meow tap: No active player cat!");
        return;
    }

    if (!g_find_cat_voice_component || !g_get_combat_cat_voice || !g_play_cat_voice) 
    {
        Log("Meow tap: Native cat voice functions unavailable!");
        return;
    }

    cat_parts = g_find_cat_voice_component(component_owner);

    if (!cat_parts) 
    {
        Log("Meow tap: Player cat has no voice component?");
        return;
    }

    voice_component = g_get_combat_cat_voice(character);

    if (!voice_component) 
    {
        Log("Meow tap: Player cat has no combat voice controller?");
        return;
    }

    if (!set_native_talking(voice_component, 1)) 
    {
        Log("Meow tap: Player cat has no talking-mouth visuals?");
        return;
    }

    g_talking_voice_component = voice_component;
    g_talking_seconds_remaining = TALKING_DURATION_SECONDS;
    make_sso_string(&voice, "Normal", 6);
    Log("M tap: Playing regular meow + talking mouth");
    g_play_cat_voice(cat_parts, &voice, 0, 1.0, 1.0);
}

static void try_hold_meow(void)
{
    void* character;
    void* component_owner;
    CatActionEntry entry;
    void* entries[1];
    CatActionData data;
    CatActionClosure closure;

    if (!get_active_cat(&character, &component_owner)) 
    {
        Log("Meow hold: no active player cat");
        return;
    }

    if (!g_play_cat_action || !g_find_cat_movie_component || !g_find_cat_voice_component) 
    {
        Log("Meow hold: native cat action functions are unavailable");
        return;
    }

    if (!g_find_cat_movie_component(component_owner)) 
    {
        Log("Meow hold: player cat has no movie component");
        return;
    }

    if (!g_find_cat_voice_component(component_owner)) 
    {
        Log("Meow hold: player cat has no voice component");
        return;
    }

    stop_talking_override();

    zero(&entry, (uptr)sizeof(entry));
    zero(&data, (uptr)sizeof(data));
    zero(&closure, (uptr)sizeof(closure));

    entry.component_owner = component_owner;
    entries[0] = (void*)&entry;
    data.entries = entries;
    make_sso_string(&data.animation, "StartTurn", 9);
    make_sso_string(&data.voice, "Normal", 6);
    closure.data = &data;
    closure.entry_index = 0;

    Log("Meow hold: Playing StartTurn meow + animation");
    g_play_cat_action(&closure);
}

static void CALL hook_begin_turn(void* character, int turn_kind)
{
    g_m_press_cat = (void*)0;

    if (g_ready && is_player_cat(character)) 
    {
        g_active_player_cat = character;
        Log("Player cat turn is active!");
    } 
    else 
    {
        // Enemy, familiar, prop, and non-character turns disable the key...
        g_active_player_cat = (void*)0;
    }

    if (g_original_begin_turn) g_original_begin_turn(character, turn_kind);
}

static void CALL hook_end_turn(void* character)
{
    if ((void*)g_m_press_cat == character) g_m_press_cat = (void*)0;

    if ((void*)g_talking_voice_component) 
    {
        void* talking_character = *(void**)((u8*)(void*)g_talking_voice_component + COMBAT_VOICE_CHARACTER_OFFSET);
        if (talking_character == character) stop_talking_override();
    }

    if ((void*)g_active_player_cat == character) 
    {
        // Clear first... (BeginTurn can synchronously end a skipped turn)..
        g_active_player_cat = (void*)0;
    }

    if (g_original_end_turn) g_original_end_turn(character);
}

static void handle_sdl_event(void* event)
{
    if (event) 
    {
        const u8* bytes = (const u8*)event;
        u32 type = *(const u32*)bytes;

        if (type == SDL_EVENT_KEY_DOWN || type == SDL_EVENT_KEY_UP) 
        {
            u32 scancode = *(const u32*)(bytes + SDL_KEYBOARD_SCANCODE_OFFSET);

            if (scancode == SDL_SCANCODE_M) 
            {
                if (type == SDL_EVENT_KEY_DOWN && !g_m_was_down) 
                {
                    g_m_was_down = 1;
                    g_m_hold_fired = 0;
                    g_m_down_timestamp_ns = *(const u64*)(bytes + SDL_EVENT_TIMESTAMP_OFFSET);
                    g_m_hold_seconds_remaining = HOLD_THRESHOLD_SECONDS;
                    g_m_press_cat = (void*)g_active_player_cat;
                } 
                else if (type == SDL_EVENT_KEY_UP) 
                {
                    u64 up_timestamp_ns = *(const u64*)(bytes + SDL_EVENT_TIMESTAMP_OFFSET);
                    int held_long_enough = 0;

                    if (up_timestamp_ns >= g_m_down_timestamp_ns) 
                    {
                        held_long_enough = (up_timestamp_ns - g_m_down_timestamp_ns) >= HOLD_THRESHOLD_NS;
                    }

                    if (g_m_was_down && (void*)g_m_press_cat == (void*)g_active_player_cat) 
                    {
                        if (!g_m_hold_fired && held_long_enough) 
                        {
                            g_m_hold_fired = 1;
                            try_hold_meow();
                        } 
                        else if (!g_m_hold_fired) 
                        {
                            try_tap_meow();
                        }
                    }

                    g_m_was_down = 0;
                    g_m_hold_fired = 0;
                    g_m_press_cat = (void*)0;
                }
            }
        }
    }
}

static void CALL hook_input_event(void* input_core, void* event)
{
    handle_sdl_event(event);
    if (g_original_input_event) g_original_input_event(input_core, event);
}

static void CALL hook_combat_voice_update(void* voice_component)
{
    void* character;
    double delta;

    if (g_original_combat_voice_update) g_original_combat_voice_update(voice_component);
    if (!g_ready || !voice_component) { return; }

    character = *(void**)((u8*)voice_component + COMBAT_VOICE_CHARACTER_OFFSET);
    delta = combat_voice_delta_seconds(voice_component);

    if (g_m_was_down && !g_m_hold_fired && character == (void*)g_m_press_cat && character == (void*)g_active_player_cat && is_player_cat(character)) 
    {
        g_m_hold_seconds_remaining -= delta;

        if (g_m_hold_seconds_remaining <= 0.0) 
        {
            g_m_hold_fired = 1;
            try_hold_meow();
        }
    }

    if (voice_component == (void*)g_talking_voice_component) 
    {
        if (character != (void*)g_active_player_cat || !is_player_cat(character)) 
        {
            stop_talking_override();
            return;
        }

        if (g_talking_seconds_remaining > 0.0) 
        {
            (void)set_native_talking(voice_component, 1);
            g_talking_seconds_remaining -= delta;
        } 
        else 
        {
            stop_talking_override();
        }
    }
}

static int install(void)
{
    uptr game_base;
    int ok;

    if (!resolve_mewjector(&g_mewjector)) { return 0; }

    game_base = g_mewjector.GetGameBase();

    if (!game_base) game_base = (uptr)game_image_base();
    if (!game_base) { return 0; }

    g_find_cat_movie_component = (FindCatComponentFn)(game_base + RVA_FIND_CAT_MOVIE_COMPONENT);
    g_find_cat_voice_component = (FindCatComponentFn)(game_base + RVA_FIND_CAT_VOICE_COMPONENT);
    g_get_combat_cat_voice = (GetCombatCatVoiceFn)(game_base + RVA_GET_COMBAT_CAT_VOICE);
    g_play_cat_action = (PlayCatActionFn)(game_base + RVA_PLAY_CAT_ACTION);
    g_play_cat_voice = (PlayCatVoiceFn)(game_base + RVA_PLAY_CAT_VOICE);

    ok = g_mewjector.InstallHook((uptr)RVA_CHARACTER_BEGIN_TURN, STOLEN_BEGIN_TURN, (void*)hook_begin_turn, (void**)&g_original_begin_turn, HOOK_PRIORITY, MOD_NAME);

    if (!ok || !g_original_begin_turn) 
    {
        g_mewjector.Log(MOD_NAME, "Failed to hook Character::BeginTurn");
        return 0;
    }

    ok = g_mewjector.InstallHook((uptr)RVA_CHARACTER_END_TURN, STOLEN_END_TURN, (void*)hook_end_turn, (void**)&g_original_end_turn, HOOK_PRIORITY, MOD_NAME);

    if (!ok || !g_original_end_turn) 
    {
        return 0;
    }

    ok = g_mewjector.InstallHook((uptr)RVA_INPUT_CORE_SYSTEM_EVENT, STOLEN_INPUT_EVENT, (void*)hook_input_event, (void**)&g_original_input_event, HOOK_PRIORITY, MOD_NAME);

    if (!ok || !g_original_input_event) 
    {
        Log("Failed to hook InputCore::OnSystemEvent");
        return 0;
    }

    ok = g_mewjector.InstallHook((uptr)RVA_COMBAT_CAT_VOICE_UPDATE, STOLEN_COMBAT_VOICE_UPDATE, (void*)hook_combat_voice_update, (void**)&g_original_combat_voice_update, HOOK_PRIORITY, MOD_NAME);

    if (!ok || !g_original_combat_voice_update) 
    {
        Log("Failed to hook CombatCatVoice_Emotions::update");
        return 0;
    }

    g_ready = 1;
    return 1;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    (void)module;
    (void)reserved;

    if (reason == 1) 
    {
        (void)install();
    } 
    else if (reason == 0) 
    { 
        g_ready = 0;
        g_active_player_cat = (void*)0;
        g_m_press_cat = (void*)0;
        g_talking_voice_component = (void*)0;
    }

    return 1;
}