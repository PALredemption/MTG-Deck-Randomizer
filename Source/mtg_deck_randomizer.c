#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>
#include <storage/storage.h>
#include <furi_hal.h>

#define MAX_DECKS 10
#define MAX_NAME_LENGTH 20
#define SPIN_DURATION 3000
#define BLINK_INTERVAL 500
#define SPIN_SPEED 8
#define BLINK_DURATION 3000
#define KEYBOARD_ROW_COUNT 3

typedef struct {
    char name[MAX_NAME_LENGTH];
} Deck;

typedef enum {
    StateMainMenu,
    StateSpinning,
    StateSelected,
    StateDeckList,
    StateKeyboard,
    StateEditDeletePopup
} AppState;

typedef struct {
    char text;
    uint8_t x;
    uint8_t y;
} KeyboardKey;

typedef struct {
    Deck decks[MAX_DECKS];
    int deck_count;
    int current_deck;
    int selected_deck;
    AppState state;
    uint32_t spin_start_time;
    char edit_buffer[MAX_NAME_LENGTH];
    int spin_offset;
    uint32_t blink_start_time;
    bool is_blinking;
    uint8_t selected_row;
    uint8_t selected_column;
    int scroll_position;
} MTGDeckRandomizer;

static const uint8_t keyboard_origin_x = 1;
static const uint8_t keyboard_origin_y = 29;

#define ENTER_KEY '\r'
#define BACKSPACE_KEY '\b'

static const KeyboardKey keyboard_keys_row_1[] = {
    {'q', 1, 8},  {'w', 10, 8}, {'e', 19, 8}, {'r', 28, 8},  {'t', 37, 8},  {'y', 46, 8},
    {'u', 55, 8}, {'i', 64, 8}, {'o', 73, 8}, {'p', 82, 8},  {'1', 91, 8},  {'2', 100, 8},
    {'3', 109, 8}
};

static const KeyboardKey keyboard_keys_row_2[] = {
    {'a', 1, 20},  {'s', 10, 20}, {'d', 19, 20}, {'f', 28, 20}, {'g', 37, 20}, {'h', 46, 20},
    {'j', 55, 20}, {'k', 64, 20}, {'l', 73, 20}, {BACKSPACE_KEY, 82, 20},
    {'4', 100, 20}, {'5', 109, 20}, {'6', 118, 20}
};

static const KeyboardKey keyboard_keys_row_3[] = {
    {'z', 1, 32},  {'x', 10, 32}, {'c', 19, 32}, {'v', 28, 32}, {'b', 37, 32}, {'n', 46, 32},
    {'m', 55, 32}, {'_', 64, 32}, {ENTER_KEY, 73, 32},
    {'7', 100, 32}, {'8', 109, 32}, {'9', 118, 32}
};

static void save_decks(MTGDeckRandomizer* mtg) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* path = furi_string_alloc_set("/ext/apps/MTG/mtg_decks.txt");
    File* file = storage_file_alloc(storage);

    FURI_LOG_I("MTG", "Saving decks to %s", furi_string_get_cstr(path));

    if (storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        for (int i = 0; i < mtg->deck_count; i++) {
            storage_file_write(file, mtg->decks[i].name, strlen(mtg->decks[i].name));
            storage_file_write(file, "\n", 1);
            FURI_LOG_I("MTG", "Saved deck: %s", mtg->decks[i].name);
        }
        FURI_LOG_I("MTG", "Successfully saved %d decks", mtg->deck_count);
    } else {
        FURI_LOG_E("MTG", "Failed to open file for writing");
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);
}

static void load_decks(MTGDeckRandomizer* mtg) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* path = furi_string_alloc_set("/ext/apps/MTG/mtg_decks.txt");
    File* file = storage_file_alloc(storage);

    FURI_LOG_I("MTG", "Loading decks from %s", furi_string_get_cstr(path));

    mtg->deck_count = 0;

    if (storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buffer[MAX_NAME_LENGTH];
        size_t bytes_read;
        size_t line_start = 0;

        while ((bytes_read = storage_file_read(file, buffer + line_start, sizeof(buffer) - line_start - 1)) > 0) {
            buffer[line_start + bytes_read] = '\0';
            char* line_end;
            char* line = buffer;

            while ((line_end = strchr(line, '\n')) != NULL && mtg->deck_count < MAX_DECKS) {
                *line_end = '\0';
                if (line_end > line) {  // Ignore empty lines
                    strncpy(mtg->decks[mtg->deck_count].name, line, MAX_NAME_LENGTH - 1);
                    mtg->decks[mtg->deck_count].name[MAX_NAME_LENGTH - 1] = '\0';
                    FURI_LOG_I("MTG", "Loaded deck: %s", mtg->decks[mtg->deck_count].name);
                    mtg->deck_count++;
                }
                line = line_end + 1;
            }

            line_start = strlen(line);
            memmove(buffer, line, line_start);
        }

        // Handle last line if file doesn't end with a newline
        if (line_start > 0 && mtg->deck_count < MAX_DECKS) {
            strncpy(mtg->decks[mtg->deck_count].name, buffer, MAX_NAME_LENGTH - 1);
            mtg->decks[mtg->deck_count].name[MAX_NAME_LENGTH - 1] = '\0';
            FURI_LOG_I("MTG", "Loaded deck: %s", mtg->decks[mtg->deck_count].name);
            mtg->deck_count++;
        }

        FURI_LOG_I("MTG", "Successfully loaded %d decks", mtg->deck_count);
        for (int i = 0; i < mtg->deck_count; i++) {
            FURI_LOG_I("MTG", "Loaded deck %d: %s", i, mtg->decks[i].name);
        }
    } else {
        FURI_LOG_W("MTG", "Failed to open file for reading, creating default decks");
        strcpy(mtg->decks[0].name, "Red Aggro");
        strcpy(mtg->decks[1].name, "Blue Control");
        strcpy(mtg->decks[2].name, "Green Ramp");
        mtg->deck_count = 3;
        FURI_LOG_I("MTG", "Created default decks");
        save_decks(mtg);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);

    if (mtg->deck_count == 0) {
        FURI_LOG_E("MTG", "No decks loaded or created. This should not happen.");
    }
}

static uint8_t get_row_size(uint8_t row_index) {
    switch(row_index) {
        case 0: return sizeof(keyboard_keys_row_1) / sizeof(keyboard_keys_row_1[0]);
        case 1: return sizeof(keyboard_keys_row_2) / sizeof(keyboard_keys_row_2[0]);
        case 2: return sizeof(keyboard_keys_row_3) / sizeof(keyboard_keys_row_3[0]);
        default: return 0;
    }
}

static const KeyboardKey* get_row(uint8_t row_index) {
    switch(row_index) {
        case 0: return keyboard_keys_row_1;
        case 1: return keyboard_keys_row_2;
        case 2: return keyboard_keys_row_3;
        default: return NULL;
    }
}

static void draw_keyboard(Canvas* canvas, MTGDeckRandomizer* mtg) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 10, "Add your deckname:");
    canvas_draw_str(canvas, 2, 22, mtg->edit_buffer);

    canvas_set_font(canvas, FontKeyboard);

    for(uint8_t row = 0; row < KEYBOARD_ROW_COUNT; row++) {
        const uint8_t column_count = get_row_size(row);
        const KeyboardKey* keys = get_row(row);

        for(size_t column = 0; column < column_count; column++) {
            int x = keyboard_origin_x + keys[column].x;
            int y = keyboard_origin_y + keys[column].y;

            if(keys[column].text == ENTER_KEY) {
                canvas_set_font(canvas, FontSecondary);
                canvas_draw_str(canvas, x, y, "save");
                if(mtg->selected_row == row && mtg->selected_column == column) {
                    canvas_draw_frame(canvas, x - 1, y - 8, 24, 11);
                }
            } else if(keys[column].text == BACKSPACE_KEY) {
                canvas_set_font(canvas, FontSecondary);
                canvas_draw_str(canvas, x, y, "bck");
                if(mtg->selected_row == row && mtg->selected_column == column) {
                    canvas_draw_frame(canvas, x - 1, y - 8, 18, 11);
                }
            } else {
                canvas_set_font(canvas, FontKeyboard);
                if(mtg->selected_row == row && mtg->selected_column == column) {
                    canvas_draw_frame(canvas, x - 1, y - 8, 7, 10);
                }
                canvas_draw_glyph(canvas, x, y, keys[column].text);
            }
        }
    }
}

static void mtg_deck_randomizer_draw_callback(Canvas* canvas, void* ctx) {
    MTGDeckRandomizer* mtg = ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    switch (mtg->state) {
        case StateMainMenu:
            canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignTop, "MTG Deck Randomizer");
            canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, mtg->decks[mtg->current_deck].name);
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str_aligned(canvas, 64, 60, AlignCenter, AlignBottom, "OK: Spin | Down: Edit Decks");
            break;
        case StateSpinning:
            {
                uint32_t elapsed = furi_get_tick() - mtg->spin_start_time;
                if (elapsed < SPIN_DURATION) {
                    mtg->spin_offset = (mtg->spin_offset + SPIN_SPEED) % 128;
                    for (int i = 0; i < mtg->deck_count; i++) {
                        int y = 32 + (i * 16 - mtg->spin_offset + 128) % 128 - 64;
                        canvas_draw_str_aligned(canvas, 64, y, AlignCenter, AlignCenter, mtg->decks[i].name);
                    }
                } else {
                    mtg->state = StateSelected;
                    mtg->blink_start_time = furi_get_tick();
                }
            }
            break;
        case StateSelected:
            {
                uint32_t elapsed = furi_get_tick() - mtg->blink_start_time;
                if (elapsed < BLINK_DURATION) {
                    if ((elapsed / BLINK_INTERVAL) % 2 == 0) {
                        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, mtg->decks[mtg->current_deck].name);
                    }
                } else {
                    mtg->state = StateMainMenu;
                }
            }
            break;
        case StateDeckList:
            canvas_draw_str_aligned(canvas, 64, 0, AlignCenter, AlignTop, "Deck List");
            int visible_items = 6;
            int start_index = MAX(0, mtg->selected_deck - visible_items / 2);
            int end_index = MIN(start_index + visible_items, mtg->deck_count + 1);

            for (int i = start_index; i < end_index; i++) {
                int y = 10 + (i - start_index) * 9;
                if (i < mtg->deck_count) {
                    canvas_draw_str_aligned(canvas, 5, y, AlignLeft, AlignTop, mtg->decks[i].name);
                } else {
                    canvas_draw_str_aligned(canvas, 5, y, AlignLeft, AlignTop, "Add New Deck");
                }
                if (i == mtg->selected_deck) {
                    canvas_draw_str_aligned(canvas, 0, y, AlignLeft, AlignTop, ">");
                }
            }

            if (start_index > 0) {
                canvas_draw_str_aligned(canvas, 124, 10, AlignRight, AlignTop, "^");
            }
            if (end_index < mtg->deck_count + 1) {
                canvas_draw_str_aligned(canvas, 124, 62, AlignRight, AlignBottom, "v");
            }
            break;
        case StateKeyboard:
            draw_keyboard(canvas, mtg);
            break;
        case StateEditDeletePopup: {
            // Draw the deck list in the background
            mtg->state = StateDeckList;
            mtg_deck_randomizer_draw_callback(canvas, ctx);
            mtg->state = StateEditDeletePopup;

            // Draw the popup
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_box(canvas, 22, 17, 84, 32);
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_frame(canvas, 22, 17, 84, 32);
            canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignTop, "Left: Delete");
            canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignTop, "Right: Edit");
            canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignTop, "Back: Cancel");
            break;
        }
        default:
            FURI_LOG_E("MTG", "Unknown state in draw callback");
            break;
    }
}

static void handle_keyboard_input(MTGDeckRandomizer* mtg, InputEvent input) {
    if (input.type == InputTypeShort || input.type == InputTypeLong) {
        switch (input.key) {
            case InputKeyUp:
                if (mtg->selected_row > 0) mtg->selected_row--;
                break;
            case InputKeyDown:
                if (mtg->selected_row < KEYBOARD_ROW_COUNT - 1) mtg->selected_row++;
                break;
            case InputKeyLeft:
                if (mtg->selected_column > 0) mtg->selected_column--;
                break;
            case InputKeyRight:
                if (mtg->selected_column < get_row_size(mtg->selected_row) - 1) mtg->selected_column++;
                break;
            case InputKeyOk:
                {
                    const KeyboardKey* keys = get_row(mtg->selected_row);
                    char key = keys[mtg->selected_column].text;
                    size_t len = strlen(mtg->edit_buffer);
                    if (key == BACKSPACE_KEY) {
                        if (len > 0) mtg->edit_buffer[len - 1] = '\0';
                    } else if (key == ENTER_KEY) {
                        if (len > 0) {
                            if (mtg->selected_deck < mtg->deck_count) {
                                // Editing existing deck
                                strcpy(mtg->decks[mtg->selected_deck].name, mtg->edit_buffer);
                            } else if (mtg->deck_count < MAX_DECKS) {
                                // Adding new deck
                                strcpy(mtg->decks[mtg->deck_count].name, mtg->edit_buffer);
                                mtg->deck_count++;
                            }
                            save_decks(mtg);
                            mtg->state = StateDeckList;
                        }
                    } else if (len < MAX_NAME_LENGTH - 1) {
                        if (input.type == InputTypeLong && key >= 'a' && key <= 'z') {
                            key = key - 'a' + 'A';
                        }
                        mtg->edit_buffer[len] = key;
                        mtg->edit_buffer[len + 1] = '\0';
                    }
                }
                break;
            default:
                break;
        }
    }
}

static void mtg_deck_randomizer_input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

static void mtg_deck_randomizer_update_state(MTGDeckRandomizer* mtg, InputEvent input) {
    switch (mtg->state) {
        case StateMainMenu:
            if (input.key == InputKeyOk) {
                mtg->state = StateSpinning;
                mtg->spin_start_time = furi_get_tick();
                mtg->spin_offset = 0;
                mtg->current_deck = rand() % mtg->deck_count;
            } else if (input.key == InputKeyDown) {
                mtg->state = StateDeckList;
                mtg->selected_deck = 0;
            }
            break;
        case StateSpinning:
            // Handle spinning state if needed
            break;
        case StateSelected:
            if (input.key == InputKeyOk) {
                mtg->state = StateMainMenu;
            }
            break;
        case StateDeckList:
            if (input.type == InputTypeShort) {
                if (input.key == InputKeyOk) {
                    if (mtg->selected_deck == mtg->deck_count) {
                        mtg->state = StateKeyboard;
                        memset(mtg->edit_buffer, 0, sizeof(mtg->edit_buffer));
                        mtg->selected_row = 0;
                        mtg->selected_column = 0;
                    } else {
                        mtg->current_deck = mtg->selected_deck;
                        mtg->state = StateMainMenu;
                    }
                } else if (input.key == InputKeyUp) {
                    if (mtg->selected_deck > 0) {
                        mtg->selected_deck--;
                        if (mtg->selected_deck < mtg->scroll_position) {
                            mtg->scroll_position = mtg->selected_deck;
                        }
                    }
                } else if (input.key == InputKeyDown) {
                    if (mtg->selected_deck < mtg->deck_count) {
                        mtg->selected_deck++;
                        if (mtg->selected_deck >= mtg->scroll_position + 4) {
                            mtg->scroll_position = mtg->selected_deck - 3;
                        }
                    }
                }
            } else if (input.type == InputTypeLong && input.key == InputKeyOk && mtg->selected_deck < mtg->deck_count) {
                mtg->state = StateEditDeletePopup;
            }
            break;
        case StateKeyboard:
            handle_keyboard_input(mtg, input);
            break;
        case StateEditDeletePopup:
            if (input.type == InputTypeShort) {
                if (input.key == InputKeyLeft) {
                    // Delete deck
                    for (int i = mtg->selected_deck; i < mtg->deck_count - 1; i++) {
                        strcpy(mtg->decks[i].name, mtg->decks[i + 1].name);
                    }
                    mtg->deck_count--;
                    save_decks(mtg);
                    mtg->state = StateDeckList;
                } else if (input.key == InputKeyRight) {
                    // Edit deck
                    strcpy(mtg->edit_buffer, mtg->decks[mtg->selected_deck].name);
                    mtg->state = StateKeyboard;
                } else if (input.key == InputKeyBack) {
                    mtg->state = StateDeckList;
                }
            }
            break;
    }

    if (input.key == InputKeyBack) {
        switch (mtg->state) {
            case StateMainMenu:
                // Exit the app
                break;
            case StateSpinning:
            case StateSelected:
            case StateDeckList:
                mtg->state = StateMainMenu;
                break;
            case StateKeyboard:
            case StateEditDeletePopup:
                mtg->state = StateDeckList;
                break;
        }
    }
}

int32_t mtg_deck_randomizer_app(void* p) {
    UNUSED(p);
    MTGDeckRandomizer* mtg = malloc(sizeof(MTGDeckRandomizer));

    // Initialize MTGDeckRandomizer
    mtg->current_deck = 0;
    mtg->selected_deck = 0;
    mtg->state = StateMainMenu;
    mtg->spin_start_time = 0;
    mtg->spin_offset = 0;
    mtg->blink_start_time = 0;
    mtg->is_blinking = false;
    mtg->selected_row = 0;
    mtg->selected_column = 0;
    mtg->scroll_position = 0;
    
    // Load decks from storage
    load_decks(mtg);

    // Create GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, mtg_deck_randomizer_draw_callback, mtg);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    // Create event queue
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    view_port_input_callback_set(view_port, mtg_deck_randomizer_input_callback, event_queue);

    // Main loop
    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            if(event.key == InputKeyBack && event.type == InputTypeLong && mtg->state == StateMainMenu) {
                running = false;
            } else {
                mtg_deck_randomizer_update_state(mtg, event);
            }
        }
        view_port_update(view_port);
    }

    // Cleanup
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_record_close(RECORD_GUI);
    free(mtg);

    return 0;
}