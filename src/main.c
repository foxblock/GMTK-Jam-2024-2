#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <float.h>

#include "raylib.h"
#include "raymath.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#undef RAYGUI_IMPLEMENTATION

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

typedef enum EquationType 
{
    ET_NONE = 0,
    ET_ADD,
    ET_SUB,
    ET_MULT,
    ET_DIV,
    ET_SQR,
    ET_SQRT,
    ET_LOG_E,
    ET_LOG_2,
    ET_LOG_10,

    ET_EOL
} EquationType;
const char* SIGNS[ET_EOL] = {
    "none", "+%d", "-%d", "*%d", "/%d", "x²", "sqrt()", "log_e()", "log_2()", "log_10"
};

#define HEALTH_DEFAULT 10
// TODO: Split this more sensibly into "LevelParams" struct or something
typedef struct Home // also level parameters
{
    Rectangle rect;
    int health;
    unsigned int allowedTowers; // bit mask of EquationType entries
    int minTowers;
    int roundingFactor;
    int score;
    int levelIndex;
} Home;

#define ENEMY_SIZE 20
typedef struct Enemy
{
    Vector2 pos; // center
    Vector2 speed;
    float health;
    bool alive;
} Enemy;

#define QUEUE_SPACING_DEFAULT 120
typedef struct EnemyQueue
{
    unsigned int spawnFrame;
    float health;
} EnemyQueue;

#define SHOT_SIZE 4
#define SHOT_LIFETIME 12
typedef struct Shot
{
    int tower;
    int target;
    EquationType type;
    int scale;
    int shotLife;
} Shot;

#define TOWER_SIZE 50
#define TOWER_RANGE 150
#define TOWER_LIST_SIZE 64
typedef struct Tower
{
    Rectangle rect;
    Vector2 center;
    EquationType type;
    int scale;
    int range;
    unsigned int lastShot; // in frames
    unsigned int cooldown; // in frames
    int enemiesShot[TOWER_LIST_SIZE];
    unsigned int shotIndex;
} Tower;

typedef struct GameState
{
    Home home;

    Tower *towers;
    unsigned int towerLen;

    Enemy *enemies;
    unsigned int enemiesLen;

    EnemyQueue *queue; // needs to be ordered by spawnFrame (lowest first)
    unsigned int queueHead;
    unsigned int queueTail;

    Shot *shots;
    unsigned int shotHead;
    unsigned int shotTail;
} GameState;

#define MAX_TOWERS 32
#define MAX_ENEMIES 1024
#define QUEUE_SIZE 64
#define MAX_SIMUL_SHOTS MAX_TOWERS
void state_init(GameState *s)
{
    s->home = (Home){
        .rect = {50, 200, TOWER_SIZE, TOWER_SIZE},
        .health = 10,
        .allowedTowers = -1, // all by default
    };

    s->towers = calloc(MAX_TOWERS, sizeof(s->towers[0]));
    s->towerLen = 0;

    s->enemies = calloc(MAX_ENEMIES, sizeof(s->enemies[0]));
    s->enemiesLen = 0;

    s->queue = calloc(QUEUE_SIZE, sizeof(s->queue[0]));
    s->queueHead = 0;
    s->queueTail = 0;

    // rolling buffer, we do not check for overwrites, so this has to be big enough
    // Equal to max towers, because every tower can only shoot once simultaniously
    s->shots = calloc(MAX_SIMUL_SHOTS, sizeof(s->shots[0]));
    s->shotHead = 0;
    s->shotTail = 0;
}

void state_free(GameState *s)
{
    free(s->towers);
    free(s->enemies);
    free(s->queue);
    free(s->shots);
}

void state_reset(GameState *s)
{
    s->towerLen = 0;
    s->home.health = HEALTH_DEFAULT;
    s->home.score = 0;
    s->enemiesLen = 0;
    s->queueHead = s->queueTail = 0;
    s->shotHead = s->shotTail = 0;
}

void state_addTower(GameState *s, int tileX, int tileY, int type, int scale)
{
    s->towers[s->towerLen++] = (Tower){
        .rect = {tileX * TOWER_SIZE, tileY * TOWER_SIZE, TOWER_SIZE, TOWER_SIZE},
        .center = {(tileX + 0.5) * TOWER_SIZE, (tileY + 0.5) * TOWER_SIZE},
        .type = type,
        .scale = scale,
        .range = TOWER_RANGE,
        .cooldown = 60,
    };
}

// returns true if all entries were added
bool state_addQueueFromString(GameState *s, unsigned int startFrame, const char *queue, unsigned int count, unsigned int spacing)
{
    unsigned int spawnFrame;
    if (s->queueHead == s->queueTail) // queue is empty -> spawn immediately
        spawnFrame = startFrame;
    else
        spawnFrame = s->queue[(s->queueHead - 1) % QUEUE_SIZE].spawnFrame + spacing;
    while (count > 0)
    {
        char *buffer = strdup(queue);
        char *prev = buffer;
        char *pos = strtok(prev, ",;");
        while (pos != NULL)
        {
            bool queueIsFull = (s->queueHead - s->queueTail >= QUEUE_SIZE);
            if (queueIsFull)
                return false;

            float value = atof(pos);
            if (value == 0 || !isfinite(value))
                continue;

            s->queue[s->queueHead % QUEUE_SIZE] = (EnemyQueue){
                .spawnFrame = spawnFrame,
                .health = atof(pos),
            };
            ++s->queueHead;
            spawnFrame += spacing;
            prev = pos;
            pos = strtok(NULL, ",;");
        }

        free(buffer);
        --count;
    }

    return true;
}

bool canTarget(EquationType tower, float enemy)
{
    switch (tower)
    {
        case ET_ADD:
        case ET_SUB:
        case ET_MULT:
        case ET_DIV:
        case ET_SQR:
            return true;
        case ET_SQRT:
        case ET_LOG_E:
        case ET_LOG_2:
        case ET_LOG_10:
            return enemy > 0;
        default:
            printf("ERROR: Type of tower unknown: %d\n", tower);
            assert(false); // always assert
    }
    return false;
}

// takes health and returns whether enemy is alive after hit
bool takeHealth(Enemy *e, Tower *t, int rounding)
{
    switch (t->type)
    {
        case ET_ADD: e->health += t->scale; break;
        case ET_SUB: e->health -= t->scale; break;
        case ET_MULT: e->health *= t->scale; break;
        case ET_DIV: e->health /= t->scale; break;
        case ET_SQR: e->health = e->health * e->health; break;
        case ET_SQRT: e->health = sqrtf(e->health); break;
        case ET_LOG_E: e->health = logf(e->health); break;
        case ET_LOG_2: e->health = log2f(e->health); break;
        case ET_LOG_10: e->health = log10f(e->health); break;
        default:
            printf("ERROR: Type of tower unknown: %d\n", t->type);
            assert(false);
    }
    e->health = roundf(e->health * rounding) / rounding;

    // check for =0 with 2 decimals
    if (fabs(e->health) < FLT_EPSILON)
    {
        return false;
    }
    return true;
}

bool hasAlreadyTargeted(int *list, int len, int index)
{
    for (int i = 0; i < len; ++i)
    {
        if (list[i] == index)
            return true;
    }
    return false;
}

#define FONT_SIZE 20
#define MIN_FONT_SIZE 10

Color enemyColor(float health)
{
    if (health >= 1)
        return SKYBLUE;
    if (health <= -1)
        return (Color){ 135, 255, 105, 255 };
    return PINK;
}

#define BUTTON_SIZE 40
#define GUI_SPACING 4
typedef enum EditBox 
{
    EB_NONE = -1,
    EB_COUNT,
    EB_HEALTH,
    EB_SPACING,
} EditBox;

typedef enum Scene
{
    SC_MENU,
    SC_TURORIAL,
    SC_LEVEL_SELECT,
    SC_LEVEL,
    SC_PLAYGROUND,
    SC_EXIT,
} Scene;

typedef enum LevelCat
{
    LC_NATURAL,
    LC_INTEGER,
    LC_RATIONAL,
    LC_REAL,

    LC_EOL
} LevelCat;
const char* CATEGORY[LC_EOL] = {
    "Natural Numbers N", "Integers Z", "Rational Numbers Q (0.1 precision)", "Real Numbers R (0.01 precision)",
};

typedef struct LevelDef
{
    const char *name;
    LevelCat cat;
    const char *health;
    int count;
    int spacing;
    unsigned int towersAllowed;
    int minSolution;
    int roundingFactor;
} LevelDef;

const LevelDef LEVELS[] = {
    {
        .name = "Learning to count",
        .cat = LC_NATURAL,
        .health = "1,2,3,4,5",
        .count = 3,
        .spacing = QUEUE_SPACING_DEFAULT,
        .towersAllowed = (1 << ET_NONE) | (1 << ET_ADD) | (1 << ET_SUB),
        .minSolution = 5, // [-1] * 2
        .roundingFactor = 1,
    },
    {
        .name = "Kingmaker",
        .cat = LC_NATURAL,
        .health = "5,10,20,40,80",
        .count = 3,
        .spacing = QUEUE_SPACING_DEFAULT,
        .towersAllowed = (1 << ET_NONE) | (1 << ET_ADD) | (1 << ET_SUB) | (1 << ET_MULT) | (1 << ET_DIV),
        .minSolution = 8, // [/2] * 5, [-1] * 3
        .roundingFactor = 1,
    },
    {
        .name = "Terror from the depths",
        .cat = LC_INTEGER,
        .health = "1,-1,2,-2",
        .count = 5,
        .spacing = QUEUE_SPACING_DEFAULT,
        .towersAllowed = (1 << ET_NONE) | (1 << ET_ADD) | (1 << ET_SUB) | (1 << ET_MULT) | (1 << ET_DIV),
        .minSolution = 4, // [/2], [+1], [-1] * 2
        .roundingFactor = 1,
    },
    {
        .name = "We have to go back",
        .cat = LC_INTEGER,
        .health = "-1,-2,-3",
        .count = 5,
        .spacing = QUEUE_SPACING_DEFAULT,
        .towersAllowed = (1 << ET_NONE) | (1 << ET_SUB) | (1 << ET_MULT) | (1 << ET_DIV) | (1 << ET_SQR) | (1 << ET_SQRT),
        .minSolution = 5, // [²], [sqrt], [-1] * 3
        .roundingFactor = 1,
    },
    {
        .name = "Prime time",
        .cat = LC_INTEGER,
        .health = "2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97,101,103,107,109,113,127,131",
        .count = 1,
        .spacing = QUEUE_SPACING_DEFAULT,
        .towersAllowed = (1 << ET_NONE) | (1 << ET_ADD) | (1 << ET_SUB) | (1 << ET_MULT) | (1 << ET_DIV) | (1 << ET_SQR) | (1 << ET_SQRT),
        .minSolution = 5, // [sqrt] * 4, [-1]
        .roundingFactor = 1,
    },
    {
        .name = "Primer time",
        .cat = LC_RATIONAL,
        .health = "2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97,101,103,107,109,113,127,131",
        .count = 1,
        .spacing = QUEUE_SPACING_DEFAULT,
        .towersAllowed = (1 << ET_NONE) | (1 << ET_ADD) | (1 << ET_SUB) | (1 << ET_MULT) | (1 << ET_DIV) | (1 << ET_SQR) | (1 << ET_SQRT),
        .minSolution = 7, // [sqrt] * 6, [-1]
        .roundingFactor = 10,
    },
    {
        .name = "Potential",
        .cat = LC_RATIONAL,
        .health = "1,10,100,1e4,1e5,1e6,1e7,1e8,1e9,1e10",
        .count = 2,
        .spacing = QUEUE_SPACING_DEFAULT,
        .towersAllowed = (1 << ET_NONE) | (1 << ET_ADD) | (1 << ET_SUB) | (1 << ET_MULT) | (1 << ET_DIV) | (1 << ET_SQR) | (1 << ET_SQRT) | (1 << ET_LOG_10),
        .minSolution = 8, // [log_10] * 2, [+1], [sqrt] * 4, [-1]
        .roundingFactor = 10,
    },
    {
        .name = "Plus / minus",
        .cat = LC_RATIONAL,
        .health = "1,-2,3,-4,5,-6,7,-8,9,-10,11,-12,13,-14,15,-16,17,-18,19,-20,21,-22,23,-24,25,-26,27,-28,29,-30,31,-32",
        .count = 1,
        .spacing = QUEUE_SPACING_DEFAULT,
        .towersAllowed = (1 << ET_NONE) | (1 << ET_ADD) | (1 << ET_SUB) | (1 << ET_MULT) | (1 << ET_DIV) | (1 << ET_SQR) | (1 << ET_SQRT) | (1 << ET_LOG_10),
        .minSolution = 9, // [²], [log_10], [+1], [sqrt]*5, [-1]
        .roundingFactor = 10,
    },
};

typedef struct Savegame
{
    int progress;
    int scores[ARRAY_SIZE(LEVELS)];
} Savegame;

#define SAVE_FILE "save.me"
bool load_progress(Savegame *data, const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL)
        return false;
    
    int res = fread(&data->progress, sizeof(data->progress), 1, f);
    if (res == 0)
        return false;
    res = fread(data->scores, sizeof(data->scores[0]), ARRAY_SIZE(data->scores), f);
    if (res != ARRAY_SIZE(data->scores))
    {
        memset(data->scores, 0, sizeof(data->scores));
        return false;
    }

    return true;
}

bool save_progress(Savegame *data, const char *filename)
{
    FILE *f = fopen(filename, "wb");
    if (f == NULL)
        return false;
    
    int res = fwrite(data, 1, sizeof(*data), f);
    if (res != sizeof(*data))
        return false;

    return true;
}

const int screenWidth = 800;
const int screenHeight = 450;
Scene scene;
Savegame save;

void menu(void);
void tutorial(void);
void level_select(GameState *state);
void level(GameState *state);
void playground(GameState *state);

void level_logic(GameState *state, unsigned int frame);
void level_draw(GameState *state);

int main(void)
{
    InitWindow(screenWidth, screenHeight, "Scale TD");

    SetTargetFPS(60);
    SetExitKey(0); // disable close on ESC

    load_progress(&save, SAVE_FILE);

    GameState state;
    state_init(&state);

    scene = SC_MENU;
    bool shouldClose = false;

    while (!WindowShouldClose() && !shouldClose)
    {
        switch (scene)
        {
            case SC_MENU:
                menu();
                break;
            case SC_TURORIAL:
                tutorial();
                break;
            case SC_LEVEL_SELECT:
                state_reset(&state);
                state.home.allowedTowers = -1;
                state.home.roundingFactor = 100;
                level_select(&state);
                break;
            case SC_LEVEL:
                level(&state);
                break;
            case SC_PLAYGROUND:
                state_reset(&state);
                playground(&state);
                break;
            case SC_EXIT:
                shouldClose = true;
                break;
            default:
                printf("ERROR: Unknown scene: %d\n", scene);
                assert(false);
        }
    }

    state_free(&state);

    // De-Initialization
    CloseWindow();

    return 0;
}

void menu(void)
{
    bool sceneChange = false;

    // Main game loop
    while (!WindowShouldClose() && !sceneChange) // Detect window close button or ESC key
    {
        BeginDrawing();

        ClearBackground(LIGHTGRAY);

        int textW = MeasureText("A puzzling tower defense game", 40);
        DrawText("A puzzling tower defense game", (screenWidth - textW) / 2, 40, 40, BLACK);
        textW = MeasureText("for beautiful math nerds.", 40);
        DrawText("for beautiful math nerds.", (screenWidth - textW) / 2, 90, 40, BLACK);

        int yPos = 300;
        if (GuiButton((Rectangle){screenWidth / 2 - 100, yPos, 200, 24}, "Tutorial"))
        {
            scene = SC_TURORIAL;
            sceneChange = true;
        }
        yPos += 32;
        if (GuiButton((Rectangle){screenWidth / 2 - 100, yPos, 200, 24}, "Level select"))
        {
            scene = SC_LEVEL_SELECT;
            sceneChange = true;
        }
        yPos += 32;
        if (GuiButton((Rectangle){screenWidth / 2 - 100, yPos, 200, 24}, "Playground"))
        {
            scene = SC_PLAYGROUND;
            sceneChange = true;
        }
        yPos += 32;
        if (GuiButton((Rectangle){screenWidth / 2 - 100, yPos, 200, 24}, "Exit"))
        {
            scene = SC_EXIT;
            sceneChange = true;
        }
        yPos += 32;

        GuiUnlock();

        EndDrawing();
    }
}

void state_loadFromLevelDef(GameState *state, LevelDef l, int index)
{
    state_addQueueFromString(state, 0, l.health, l.count, l.spacing);
    state->home.allowedTowers = l.towersAllowed;
    state->home.minTowers = l.minSolution;
    state->home.roundingFactor = l.roundingFactor;
    state->home.levelIndex = index;
}

void tutorial(void)
{
    bool sceneChange = false;

    // Main game loop
    while (!WindowShouldClose() && !sceneChange) // Detect window close button or ESC key
    {
        if (IsKeyPressed(KEY_ESCAPE))
        {
            scene = SC_MENU;
            sceneChange = true;
            break;
        }

        BeginDrawing();

        ClearBackground(LIGHTGRAY);

        const int spacing = 8;
        int yPos = 16;
        DrawText("- GOAL: Reduce enemy HP to 0 exactly!", 16, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- Health can go negative, only 0 is death.", 16, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- The towers function is applied to the enemies health on shot.", 16, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- Towers shoot each enemy only once.", 16, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- Towers only execute valid math.", 16, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("(i.e. sqrt() towers cannot target negative health enemies)", 40, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- Towers cannot be sold/deleted, but pressing R will reset the level.", 16, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- Gold stars are awarded for:", 16, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- completing the level", 40, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- not losing health", 40, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- placing the least amount of towers possible", 40, yPos, FONT_SIZE, BLACK);

        if (GuiButton((Rectangle){screenWidth - 124, screenHeight - 28, 120, 24}, "Got it!"))
        {
            scene = SC_MENU;
            sceneChange = true;
        }

        GuiUnlock();

        EndDrawing();
    }
}

void level_select(GameState *state)
{
    bool sceneChange = false;
    int arraySize = sizeof(LEVELS) / sizeof(LEVELS[0]);
    bool unlockAll = false;

    // Main game loop
    while (!WindowShouldClose() && !sceneChange) // Detect window close button or ESC key
    {
        if (IsKeyPressed(KEY_ESCAPE))
        {
            scene = SC_MENU;
            sceneChange = true;
            break;
        }

        BeginDrawing();

        ClearBackground(LIGHTGRAY);

        DrawText("Level select", 16, 16, FONT_SIZE * 2, BLACK);

        int xPos = 16;
        int yPos = 48;
        int currentCat = -1;
        char text[128] = "";
        for (int i = 0; i < arraySize; ++i)
        {
            LevelDef l = LEVELS[i];
            if (l.cat != currentCat)
            {
                assert(l.cat < LC_EOL);

                yPos += 24 + GUI_SPACING * 2;
                DrawText(CATEGORY[l.cat], 16, yPos, FONT_SIZE, BLACK);
                xPos = 16;
                yPos += FONT_SIZE + GUI_SPACING;
                currentCat = l.cat;
            }

            if (xPos + 200 > screenWidth)
            {
                xPos = 16;
                yPos += 24 + GUI_SPACING;
            }

            if (i < save.progress)
                snprintf(text, sizeof(text), "%s (%d/3)", l.name, save.scores[i]);
            else 
            {
                snprintf(text, sizeof(text), "%s", l.name);
                if (i > save.progress)
                    GuiSetState(STATE_DISABLED);
            }
            if (GuiButton((Rectangle){xPos, yPos, 200, 24}, text))
            {
                state_loadFromLevelDef(state, l, i);
                scene = SC_LEVEL;
                sceneChange = true;
            }
            xPos += 200 + GUI_SPACING;
        }

        GuiSetState(STATE_NORMAL);
        if (GuiButton((Rectangle){screenWidth - 124, screenHeight - 28, 120, 24}, "Back"))
        {
            scene = SC_MENU;
            sceneChange = true;
        }
        if (GuiButton((Rectangle){screenWidth - 248, screenHeight - 28, 120, 24}, unlockAll ? "You sure?" : "Unlock all"))
        {
            if (!unlockAll)
                unlockAll = true;
            else
                save.progress = INT_MAX;
        }

        GuiUnlock();

        EndDrawing();
    }
}

void level(GameState *state)
{
    assert(state);
    assert(state->queueHead != state->queueTail);

    Camera2D camera = { 0 };
    camera.target = (Vector2){ screenWidth / 2, screenHeight / 2 };
    camera.offset = (Vector2){ screenWidth/2.0f, screenHeight/2.0f };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    unsigned int frame = -300; // test rollover robustness

    Rectangle path = {100, 200, screenWidth - 100, TOWER_SIZE};
    Rectangle guiArea = {0, screenHeight - TOWER_SIZE - 1, screenWidth, TOWER_SIZE + 1};
    Rectangle guiAreaTop = {0, 0, screenWidth, TOWER_SIZE + 1};

    int currentType = ET_NONE;
    bool paused = false;
    bool sceneChange = false;
    int speedLevel = 1;
    int aliveCount = 0;
    bool gameEnded = false;

    // TODO: Save queue for reset
    EnemyQueue *queueBackup = calloc(QUEUE_SIZE, sizeof(queueBackup[0]));
    memcpy(queueBackup, state->queue, QUEUE_SIZE * sizeof(queueBackup[0]));
    unsigned int queueBackupHead = state->queueHead;

    // Main game loop
    while (!WindowShouldClose() && !sceneChange)
    {
        // ------------------ Input ------------------
        if (IsKeyPressed(KEY_ESCAPE))
        {
            scene = SC_LEVEL_SELECT;
            sceneChange = true;
            break;
        }
        if (IsKeyPressed(KEY_R))
        {
            state_reset(state);
            memcpy(state->queue, queueBackup, QUEUE_SIZE * sizeof(queueBackup[0]));
            state->queueHead = queueBackupHead;
            frame = 0;
        }
        if (IsKeyPressed(KEY_SPACE))
        {
            paused = !paused;
        }

        bool canPlaceTower = !gameEnded;

        canPlaceTower &= !CheckCollisionPointRec(GetMousePosition(), path);
        canPlaceTower &= !CheckCollisionPointRec(GetMousePosition(), state->home.rect);
        canPlaceTower &= !CheckCollisionPointRec(GetMousePosition(), guiArea);
        canPlaceTower &= !CheckCollisionPointRec(GetMousePosition(), guiAreaTop);

        if (canPlaceTower)
        {
            for (int i = 0; i < state->towerLen; ++i) {
                canPlaceTower &= !CheckCollisionPointRec(GetMousePosition(), state->towers[i].rect);
            }
        }

        int tileX = GetMouseX() / TOWER_SIZE;
        int tileY = GetMouseY() / TOWER_SIZE;
        if (IsMouseButtonPressed(0) && state->towerLen < MAX_TOWERS && canPlaceTower && currentType != ET_NONE)
        {
            int scale = 1;
            if (currentType == ET_MULT || currentType == ET_DIV)
                scale = 2;
            state_addTower(state, tileX, tileY, currentType, scale);
        }

        // ------------------ Logic ------------------
        if (!paused)
        {
            for (int i = 0; i < speedLevel; ++i)
            {
                level_logic(state, frame);

                ++frame;
            }
            aliveCount = 0;
            for (int i = state->enemiesLen-1; i >= 0; --i)
            {
                if (!state->enemies[i].alive)
                    continue;

                ++aliveCount;
            }

            if (state->queueHead == state->queueTail && aliveCount == 0)
            {
                // win
                gameEnded = true;
                if (state->home.health == HEALTH_DEFAULT)
                {
                    if (state->towerLen < state->home.minTowers)
                        state->home.score = 4;
                    else if (state->towerLen == state->home.minTowers)
                        state->home.score = 3;
                    else
                        state->home.score = 2;
                }
                else
                    state->home.score = 1;
                
                if (state->home.levelIndex >= save.progress)
                    save.progress = state->home.levelIndex + 1;
                if (save.scores[state->home.levelIndex] < state->home.score)
                    save.scores[state->home.levelIndex] = state->home.score;
                save_progress(&save, SAVE_FILE);
            }
            else if (state->home.health <= 0)
            {
                // lose
                gameEnded = true;
            }
        }

        // ------------------ Draw ------------------
        BeginDrawing();

        ClearBackground(LIGHTGRAY);

        BeginMode2D(camera);

        DrawRectangleRec(path, WHITE);

        // placement preview
        if (currentType != ET_NONE && !gameEnded)
        {
            DrawRectangle(tileX * TOWER_SIZE, tileY * TOWER_SIZE, TOWER_SIZE, TOWER_SIZE, canPlaceTower ? GRAY : MAROON);
            if (canPlaceTower)
            {
                DrawCircleLines((tileX + 0.5) * TOWER_SIZE, (tileY + 0.5) * TOWER_SIZE, TOWER_RANGE, BLACK);
            }
        }

        level_draw(state);

        // queue preview
        int ePosX = 60;
        const int ePosY = 4 + ENEMY_SIZE;
        char text[64] = "";
        DrawText("Queue:", 4, ePosY - 4, 10, BLACK);
        for (int i = state->queueTail; i < state->queueHead; ++i)
        {
            EnemyQueue *q = state->queue + i;
            
            DrawCircle(ePosX, ePosY, ENEMY_SIZE, enemyColor(q->health));
            snprintf(text, sizeof(text), "%.3g", q->health);
            int fontSize = FONT_SIZE;
            int textWidthPixels = MeasureText(text, fontSize);
            while (textWidthPixels > ENEMY_SIZE && fontSize > MIN_FONT_SIZE)
            {
                fontSize /= 2;
                textWidthPixels = MeasureText(text, fontSize);
            }
            DrawText(text, 
                ePosX - textWidthPixels / 2,
                ePosY - fontSize / 2,
                fontSize,
                BLACK);

            ePosX += ENEMY_SIZE * 2 + GUI_SPACING;
            if (ePosX > screenWidth - 140)
                break;
        }

        EndMode2D();

        // GUI
        if (gameEnded)
            GuiLock();

        int btnPos = screenWidth - (GUI_SPACING + 24) * 4;
        bool speedBtnActive = paused;
        GuiToggle((Rectangle){btnPos, 4, 24, 24}, GuiIconText(ICON_PLAYER_PAUSE, NULL), &speedBtnActive);
        paused = speedBtnActive;
        btnPos += 24 + GUI_SPACING;
        speedBtnActive = (speedLevel == 1 && !paused);
        GuiToggle((Rectangle){btnPos, 4, 24, 24}, GuiIconText(ICON_PLAYER_PLAY, NULL), &speedBtnActive);
        if (speedBtnActive)
        {
            speedLevel = 1;
            paused = false;
        }
        btnPos += 24 + GUI_SPACING;
        speedBtnActive = (speedLevel == 3 && !paused);
        GuiToggle((Rectangle){btnPos, 4, 24, 24}, GuiIconText(ICON_ARROW_RIGHT, NULL), &speedBtnActive);
        if (speedBtnActive)
        {
            speedLevel = 4;
            paused = false;
        }
        btnPos += 24 + GUI_SPACING;
        speedBtnActive = (speedLevel == 6 && !paused);
        GuiToggle((Rectangle){btnPos, 4, 24, 24}, GuiIconText(ICON_ARROW_RIGHT_FILL, NULL), &speedBtnActive);
        if (speedBtnActive)
        {
            speedLevel = 12;
            paused = false;
        }
        btnPos += 24 + GUI_SPACING;
        if (paused)
        {
            int textW = MeasureText("PAUSED", FONT_SIZE * 2);
            DrawText("PAUSED", (screenWidth - textW) / 2, 60, FONT_SIZE * 2, BLACK);
        }

        int xPos = 4;
        int yPos = screenHeight - BUTTON_SIZE - GUI_SPACING;
        for (int i = 0; i < ET_EOL; ++i)
        {
            if ((state->home.allowedTowers & 1 << i) == 0)
                continue;

            int scale = 1;
            if (i == ET_MULT || i == ET_DIV)
                scale = 2;
            snprintf(text, sizeof(text), SIGNS[i], scale);
            bool active = currentType == i;
            GuiToggle((Rectangle){ xPos, yPos, BUTTON_SIZE, BUTTON_SIZE}, text, &active);
            if (active)
            {
                currentType = i;
            }
            xPos += BUTTON_SIZE + GUI_SPACING;
        }
        
        // yPos = 4;
        // snprintf(text, sizeof(text), "Frame: %u", frame);
        // DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        // yPos += 24;
        // snprintf(text, sizeof(text), "Towers: %d / %d", state->towerLen, MAX_TOWERS);
        // DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        // yPos += 24;
        // snprintf(text, sizeof(text), "Enemies: %d - (%d / %d)", aliveCount, state->enemiesLen, MAX_ENEMIES);
        // DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        // yPos += 24;
        // snprintf(text, sizeof(text), "Queue: %d: %d -> %d", 
        //     state->queueHead - state->queueTail, state->queueTail, state->queueHead);
        // DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        // yPos += 24;
        // snprintf(text, sizeof(text), "Shots: %d: %d -> %d", 
        //     state->shotHead - state->shotTail, state->shotTail, state->shotHead);
        // DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        // yPos += 24;

        GuiUnlock();

        if (gameEnded)
        {
            DrawRectangle(0, 0, screenWidth, screenHeight, (Color){255, 255, 255, 128});

            if (state->home.health > 0)
            {
                int textW = MeasureText("You win!", 40);
                DrawText("You win!", (screenWidth - textW) / 2, 80, 40, BLACK);
                snprintf(text, sizeof(text), "Score: %d / 3", state->home.score);
                textW = MeasureText(text, 40);
                DrawText(text, (screenWidth - textW) / 2, 120, 40, BLACK);
            }
            else
            {
                int textW = MeasureText("You lose :(", 40);
                DrawText("You lose :(", (screenWidth - textW) / 2, 100, 40, BLACK);
            }

            if (GuiButton((Rectangle){screenWidth / 2 - 120, 212, 116, 24}, "Try again"))
            {
                state_reset(state);
                memcpy(state->queue, queueBackup, QUEUE_SIZE * sizeof(queueBackup[0]));
                state->queueHead = queueBackupHead;
                frame = 0;
                gameEnded = false;
            }
            if (GuiButton((Rectangle){screenWidth / 2 + 4, 212, 116, 24}, "Go to level select"))
            {
                scene = SC_LEVEL_SELECT;
                sceneChange = true;
            }
        }

        EndDrawing();
    }
}

void level_logic(GameState *state, unsigned int frame)
{
    for (int i_enemy = 0; i_enemy < state->enemiesLen; ++i_enemy)
    {
        Enemy *e = state->enemies + i_enemy;
        if (!e->alive)
            continue;

        // tower in range -> shoot
        for (int i_tower = 0; i_tower < state->towerLen; ++i_tower)
        {
            Tower *t = state->towers + i_tower;
            if (frame - t->lastShot < t->cooldown)
                continue;
            if (!CheckCollisionCircles(e->pos, ENEMY_SIZE, t->center, t->range))
                continue;
            if (!canTarget(t->type, e->health))
                continue;
            if (hasAlreadyTargeted(t->enemiesShot, TOWER_LIST_SIZE, i_enemy+1))
                continue;

            state->shots[state->shotHead % MAX_SIMUL_SHOTS] = (Shot){
                .tower = i_tower,
                .target = i_enemy, 
                .type = t->type,
                .scale = t->scale,
                .shotLife = SHOT_LIFETIME,
            };
            ++state->shotHead;
            t->lastShot = frame;
            t->enemiesShot[t->shotIndex % TOWER_LIST_SIZE] = (i_enemy + 1);
            t->shotIndex++;

            if (!takeHealth(e, t, state->home.roundingFactor))
            {
                e->alive = false;
            }
        }

        // touch home -> remove itself + health
        if (CheckCollisionPointRec(e->pos, state->home.rect))
        {
            --state->home.health;
            e->alive = false;
            continue;
        }

        e->pos = Vector2Add(e->pos, e->speed);
    }
    for (int i_shot = state->shotTail; i_shot != state->shotHead; ++i_shot)
    {
        Shot *s = state->shots + (i_shot % MAX_SIMUL_SHOTS);

        if (s->shotLife == 0)
        {
            ++state->shotTail;
            continue;
        }

        --s->shotLife;
    }
    // spawn new enemies
    for (int i_queue = state->queueTail; i_queue != state->queueHead; ++i_queue)
    {
        EnemyQueue e = state->queue[i_queue % QUEUE_SIZE];
        // check if frame is in the future (with rollover)
        if (e.spawnFrame - frame < frame - e.spawnFrame)
            break;

        assert(state->enemiesLen < MAX_ENEMIES);
        state->enemies[state->enemiesLen++] = (Enemy){
            .pos = {screenWidth + 50, screenHeight / 2},
            .speed = {-0.5, 0},
            .health = e.health,
            .alive = true,
        };
        ++state->queueTail;
    }
}

void level_draw(GameState *state)
{
    // Towers
    char text[64] = "";
    int textWidthPixels = 0;
    for (int i = 0; i < state->towerLen; ++i) 
    {
        Tower t = state->towers[i];
        DrawRectangleRec(t.rect, DARKGRAY);
        snprintf(text, sizeof(text), SIGNS[t.type], t.scale);
        int fontSize = FONT_SIZE;
        textWidthPixels = MeasureText(text, fontSize);
        while (textWidthPixels > TOWER_SIZE && fontSize > MIN_FONT_SIZE)
        {
            fontSize /= 2;
            textWidthPixels = MeasureText(text, fontSize);
        }
        DrawText(text, 
            t.rect.x + (TOWER_SIZE - textWidthPixels) / 2,
            t.rect.y + (TOWER_SIZE - fontSize) / 2,
            fontSize,
            WHITE);
    }

    // Home
    DrawRectangleRec(state->home.rect, RED);
    snprintf(text, sizeof(text), "%d", state->home.health);
    textWidthPixels = MeasureText(text, FONT_SIZE);
    DrawText(text, 
        state->home.rect.x + (TOWER_SIZE - textWidthPixels) / 2,
        state->home.rect.y + (TOWER_SIZE - FONT_SIZE) / 2,
        FONT_SIZE,
        BLACK);

    // Enemies
    for (int i = state->enemiesLen-1; i >= 0; --i)
    {
        Enemy e = state->enemies[i];
        if (!e.alive)
            continue;

        DrawCircleV(e.pos, ENEMY_SIZE, enemyColor(e.health));
        // %g is confusing. the precision option seems to specify the max total number of
        // significant digits (%.3g of 10.555 prints 10.6, while 0.555 prints 0.555).
        // Sometimes it will round, sometimes it won't (%.3g of 1.555 prints 1.55).
        snprintf(text, sizeof(text), "%.3g", e.health);
        int fontSize = FONT_SIZE;
        textWidthPixels = MeasureText(text, fontSize);
        while (textWidthPixels > ENEMY_SIZE && fontSize > MIN_FONT_SIZE)
        {
            fontSize /= 2;
            textWidthPixels = MeasureText(text, fontSize);
        }
        DrawText(text, 
            e.pos.x - textWidthPixels / 2,
            e.pos.y - fontSize / 2,
            fontSize,
            BLACK);
        snprintf(text, sizeof(text), "%.4f", e.health);
        textWidthPixels = MeasureText(text, 10);
        DrawText(text, 
            e.pos.x - textWidthPixels / 2,
            e.pos.y + fontSize / 2,
            10,
            BLACK);
    }

    // Shots
    for (int i = state->shotTail; i < state->shotHead; ++i)
    {
        Shot s = state->shots[i % MAX_SIMUL_SHOTS];

        Vector2 varTower = {rand() % 4 - 2, rand() % 4 - 2};
        Vector2 varTarget = {rand() % 8 - 4, rand() % 8 - 4};

        DrawLineV(
            Vector2Add(state->towers[s.tower].center, varTower), 
            Vector2Add(state->enemies[s.target].pos, varTarget),
            RED);
    }
}

void playground(GameState *state)
{
    Camera2D camera = { 0 };
    camera.target = (Vector2){ screenWidth / 2, screenHeight / 2 };
    camera.offset = (Vector2){ screenWidth/2.0f, screenHeight/2.0f };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    unsigned int frame = -600; // test rollover robustness
    
    Rectangle path = {100, 200, screenWidth - 100, TOWER_SIZE};
    int currentType = ET_SUB;
    int currentScale = 1;

    Rectangle guiArea = {0, screenHeight - BUTTON_SIZE - GUI_SPACING * 2, screenWidth, BUTTON_SIZE + GUI_SPACING * 2};
    Rectangle countBox = {screenWidth - 124, 32, 120, 24};
    char countText[16] = "1";
    Rectangle healthBox = {screenWidth - 124, 60, 120, 24};
    char healthText[256] = "10";
    Rectangle spacingBox = {screenWidth - 124, 88, 120, 24};
    char spacingText[16] = "120";
    int editBoxActive = EB_NONE;
    Rectangle queueButton = {screenWidth - 124, 116, 120, 24};

    bool paused = false;
    bool sceneChange = false;
    int speedLevel = 1;

    // Main game loop
    while (!WindowShouldClose() && !sceneChange)
    {
        // ------------------ Input ------------------
        if (IsKeyPressed(KEY_ESCAPE))
        {
            scene = SC_MENU;
            sceneChange = true;
            break;
        }

        if (CheckCollisionPointRec(GetMousePosition(), countBox) && IsMouseButtonPressed(0))
        {
            editBoxActive = EB_COUNT;
        }
        if (CheckCollisionPointRec(GetMousePosition(), healthBox) && IsMouseButtonPressed(0))
        {
            editBoxActive = EB_HEALTH;
        }
        if (CheckCollisionPointRec(GetMousePosition(), spacingBox) && IsMouseButtonPressed(0))
        {
            editBoxActive = EB_SPACING;
        }
        bool canPlaceTower = editBoxActive == EB_NONE;

        if (IsKeyPressed(KEY_R))
        {
            state_reset(state);
        }

        int tileX = GetMouseX() / TOWER_SIZE;
        int tileY = GetMouseY() / TOWER_SIZE;

        if (IsKeyPressed(KEY_SPACE))
        {
            paused = !paused;
        }

        if (canPlaceTower)
        {
            canPlaceTower = !CheckCollisionPointRec(GetMousePosition(), queueButton);
            canPlaceTower &= !CheckCollisionPointRec(GetMousePosition(), (Rectangle){0, 0, screenWidth, 30});
            canPlaceTower &= !CheckCollisionPointRec(GetMousePosition(), path);
            canPlaceTower &= !CheckCollisionPointRec(GetMousePosition(), state->home.rect);
            canPlaceTower &= !CheckCollisionPointRec(GetMousePosition(), guiArea);
            for (int i = 0; i < state->towerLen; ++i) {
                canPlaceTower &= !CheckCollisionPointRec(GetMousePosition(), state->towers[i].rect);
            }
        }

        if (IsMouseButtonPressed(0) && state->towerLen < MAX_TOWERS && canPlaceTower && currentType != ET_NONE)
        {
            state_addTower(state, tileX, tileY, currentType, currentScale);
        }

        // ------------------ Logic ------------------
        if (!paused)
        {
            for (int i = 0; i < speedLevel; ++i)
            {
                level_logic(state, frame);

                ++frame;
            }
        }

        // ------------------ Draw ------------------
        BeginDrawing();

        ClearBackground(LIGHTGRAY);

        BeginMode2D(camera);

        DrawRectangleRec(path, WHITE);

        // placement preview
        if (currentType != ET_NONE)
        {
            DrawRectangle(tileX * TOWER_SIZE, tileY * TOWER_SIZE, TOWER_SIZE, TOWER_SIZE, canPlaceTower ? GRAY : MAROON);
            if (canPlaceTower)
            {
                DrawCircleLines((tileX + 0.5) * TOWER_SIZE, (tileY + 0.5) * TOWER_SIZE, TOWER_RANGE, BLACK);
            }
        }

        level_draw(state);

        EndMode2D();

        // GUI
        int btnPos = (screenWidth - 3 * 24 - 2 * GUI_SPACING) / 2;
        bool speedBtnActive = paused;
        GuiToggle((Rectangle){btnPos, 4, 24, 24}, GuiIconText(ICON_PLAYER_PAUSE, NULL), &speedBtnActive);
        paused = speedBtnActive;
        btnPos += 24 + GUI_SPACING;
        speedBtnActive = (speedLevel == 1 && !paused);
        GuiToggle((Rectangle){btnPos, 4, 24, 24}, GuiIconText(ICON_PLAYER_PLAY, NULL), &speedBtnActive);
        if (speedBtnActive)
        {
            speedLevel = 1;
            paused = false;
        }
        btnPos += 24 + GUI_SPACING;
        speedBtnActive = (speedLevel > 1 && !paused);
        GuiToggle((Rectangle){btnPos, 4, 24, 24}, GuiIconText(ICON_PLAYER_NEXT, NULL), &speedBtnActive);
        if (speedBtnActive)
        {
            speedLevel = 4;
            paused = false;
        }
        btnPos += 24 + GUI_SPACING;
        if (paused)
        {
            int textW = MeasureText("PAUSED", FONT_SIZE * 2);
            DrawText("PAUSED", (screenWidth - textW) / 2, 40, FONT_SIZE * 2, BLACK);
        }

        btnPos = screenWidth - GUI_SPACING - 60;
        if (GuiButton((Rectangle){btnPos, 4, 60, 24}, "Primes"))
        {
            healthText[0] = 0;
            strncat(healthText, "2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97,101,103,107,109,113,127,131", sizeof(healthText) - 1);
        }
        btnPos -= 60 + GUI_SPACING;
        if (GuiButton((Rectangle){btnPos, 4, 60, 24}, "Log 10"))
        {
            healthText[0] = 0;
            strncat(healthText, "1,10,100,1000,1e5,1e6,1e7,1e8,1e9,1e10", sizeof(healthText) - 1);
        }
        btnPos -= 60 + GUI_SPACING;
        if (GuiButton((Rectangle){btnPos, 4, 60, 24}, "+/-"))
        {
            healthText[0] = 0;
            strncat(healthText, "1,-2,3,-4,5,-6,7,-8,9,-10,11,-12,13,-14,15,-16,17,-18,19,-20,21,-22,23,-24,25,-26,27,-28,29,-30,31,-32", sizeof(healthText) - 1);
        }
        btnPos -= 60 + GUI_SPACING;

        GuiLabel((Rectangle){countBox.x - 60, countBox.y, 60, countBox.height}, "Count:");
        if (GuiTextBox(countBox, countText, sizeof(countText), editBoxActive == EB_COUNT))
        {
            int value = atoi(countText);
            if (value <= 0)
                value = 1;
            snprintf(countText, sizeof(countText), "%d", value);

            editBoxActive = EB_NONE;
        }
        if (editBoxActive == EB_COUNT) { GuiLock(); }
        GuiLabel((Rectangle){healthBox.x - 60, healthBox.y, 60, healthBox.height}, "Health:");
        if (GuiTextBox(healthBox, healthText, sizeof(healthText), editBoxActive == EB_HEALTH))
        {
            editBoxActive = EB_NONE;
        }
        if (editBoxActive == EB_HEALTH) { GuiLock(); }
        GuiLabel((Rectangle){spacingBox.x - 60, spacingBox.y, 60, spacingBox.height}, "Spacing:");
        if (GuiTextBox(spacingBox, spacingText, sizeof(spacingText), editBoxActive == EB_SPACING))
        {
            int value = atoi(spacingText);
            if (value <= 0)
                value = 120;
            snprintf(spacingText, sizeof(spacingText), "%d", value);

            editBoxActive = EB_NONE;
        }
        if (editBoxActive == EB_SPACING) { GuiLock(); }
        if (GuiButton(queueButton, "Queue Spawn"))
        {
            int count = atoi(countText);
            int spacing = atoi(spacingText);
            assert(count > 0);
            assert(spacing > 0);

            state_addQueueFromString(state, frame, healthText, count, spacing);
        }

        int xPos = 4;
        int yPos = screenHeight - BUTTON_SIZE - GUI_SPACING;
        if (GuiButton((Rectangle){xPos, yPos, BUTTON_SIZE, (BUTTON_SIZE - GUI_SPACING) / 2}, 
            GuiIconText(ICON_ARROW_UP, NULL)))
        {
            ++currentScale;
        }
        if (GuiButton((Rectangle){xPos, yPos + (BUTTON_SIZE + GUI_SPACING) / 2, BUTTON_SIZE, (BUTTON_SIZE - GUI_SPACING) / 2}, 
            GuiIconText(ICON_ARROW_DOWN, NULL)))
        {
            if (currentScale > 1)
                --currentScale;
        }
        xPos += BUTTON_SIZE + GUI_SPACING;
        char text[64] = "";
        for (int i = 0; i < ET_EOL; ++i)
        {
            if ((state->home.allowedTowers & 1 << i) == 0)
                continue;

            snprintf(text, sizeof(text), SIGNS[i], currentScale);
            bool active = currentType == i;
            GuiToggle((Rectangle){ xPos, yPos, BUTTON_SIZE, BUTTON_SIZE}, text, &active);
            if (active)
            {
                currentType = i;
            }
            xPos += BUTTON_SIZE + GUI_SPACING;
        }

        yPos = 4;
        snprintf(text, sizeof(text), "Frame: %u", frame);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Towers: %d / %d", state->towerLen, MAX_TOWERS);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;
        int aliveCount = 0;
        for (int i = state->enemiesLen-1; i >= 0; --i)
        {
            if (!state->enemies[i].alive)
                continue;

            ++aliveCount;
        }
        snprintf(text, sizeof(text), "Enemies: %d - (%d / %d)", aliveCount, state->enemiesLen, MAX_ENEMIES);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Queue: %d: %d -> %d", 
            state->queueHead - state->queueTail, state->queueTail, state->queueHead);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Shots: %d: %d -> %d", 
            state->shotHead - state->shotTail, state->shotTail, state->shotHead);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;

        DrawFPS(screenWidth - 80, 0);

        GuiUnlock();
        EndDrawing();
    }
}
