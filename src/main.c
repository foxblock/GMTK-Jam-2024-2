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

typedef enum EquationType 
{
    ET_NONE = 0,
    ET_ADD,
    ET_SUB,
    ET_MULT,
    ET_DIV,
    ET_SQRT,
    ET_LOG_E,
    ET_LOG_2,
    ET_LOG_10,

    ET_EOL,
} EquationType;
const char* SIGNS[ET_EOL] = {
    "none", "+%d", "-%d", "*%d", "/%d", "root()", "log_e()", "log_2()", "log_10"
};

#define ENEMY_SIZE 20
typedef struct Enemy
{
    Vector2 pos; // center
    Vector2 speed;
    float health;
    bool alive;
} Enemy;

#define SHOT_SIZE 4
#define SHOT_LIFETIME 60.0f
typedef struct Shot
{
    int tower;
    int target;
    EquationType type;
    int scale;
    int life;
} Shot;

bool canTarget(EquationType tower, float enemy)
{
    switch (tower)
    {
        case ET_ADD:
        case ET_SUB:
        case ET_MULT:
        case ET_DIV:
            return true;
        case ET_SQRT:
        case ET_LOG_E:
        case ET_LOG_2:
        case ET_LOG_10:
            return enemy > 0;
        default:
            assert(tower < 0);
            return false; // suppress compiler warning
    }
}

#define HEALTH_ROUNDING 100
// takes health and returns whether enemy is alive after hit
bool takeHealth(Enemy *e, Shot *s)
{
    switch (s->type)
    {
        case ET_ADD: e->health += s->scale; break;
        case ET_SUB: e->health -= s->scale; break;
        case ET_MULT: e->health *= s->scale; break;
        case ET_DIV: e->health /= s->scale; break;
        case ET_SQRT: e->health = sqrtf(e->health); break;
        case ET_LOG_E: e->health = logf(e->health); break;
        case ET_LOG_2: e->health = log2f(e->health); break;
        case ET_LOG_10: e->health = log10f(e->health); break;
    }
    e->health = roundf(e->health * HEALTH_ROUNDING) / HEALTH_ROUNDING;

    // check for =0 with 2 decimals
    if (fabs(e->health) < FLT_EPSILON)
    {
        return false;
    }
    return true;
}

#define TOWER_SIZE 50
#define TOWER_RANGE 150
#define TOWER_LIST_SIZE 16
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

typedef struct Home
{
    Rectangle rect;
    int health;
} Home;

typedef struct EnemyQueue
{
    unsigned int spawnFrame;
    float health;
} EnemyQueue;

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

Color enemyColor(const Enemy e)
{
    if (e.health >= 1)
        return SKYBLUE;
    if (e.health <= -1)
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

int main(void)
{
    // Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "Scale TD");

    Camera2D camera = { 0 };
    camera.target = (Vector2){ screenWidth / 2, screenHeight / 2 };
    camera.offset = (Vector2){ screenWidth/2.0f, screenHeight/2.0f };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    SetTargetFPS(60);

    unsigned int frame = -600; // test rollover robustness

    const int MAX_TOWERS = 32;
    Tower *towers = calloc(MAX_TOWERS, sizeof(towers[0]));
    int towerLen = 0;
    int currentType = ET_SUB;
    int currentScale = 1;
    Home home = (Home){
        .rect = {50, 200, TOWER_SIZE, TOWER_SIZE},
        .health = 10,
    };
    Rectangle path = {100, 200, screenWidth - 100, TOWER_SIZE};

    const int MAX_ENEMIES = 1024;
    Enemy *enemies = calloc(MAX_ENEMIES, sizeof(enemies[0]));
    int enemiesLen = 0;

    const int QUEUE_SIZE = 32;
    EnemyQueue *queue = calloc(QUEUE_SIZE, sizeof(queue[0]));
    unsigned int queueHead = 0;
    unsigned int queueTail = 0;

    const int MAX_SHOTS = 1024;
    Shot *shots = calloc(MAX_SHOTS, sizeof(shots[0]));
    unsigned int shotHead = 0;
    unsigned int shotTail = 0;

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

    // Main game loop
    while (!WindowShouldClose()) // Detect window close button or ESC key
    {
        // ------------------ Input ------------------
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
            towerLen = 0;
            home.health = 10;
            shotHead = shotTail = 0;
            enemiesLen = 0;
            queueHead = queueTail = 0;
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
            canPlaceTower &= !CheckCollisionPointRec(GetMousePosition(), home.rect);
            canPlaceTower &= !CheckCollisionPointRec(GetMousePosition(), guiArea);
            for (int i = 0; i < towerLen; ++i) {
                canPlaceTower &= !CheckCollisionPointRec(GetMousePosition(), towers[i].rect);
            }
        }

        if (IsMouseButtonPressed(0) && towerLen < MAX_TOWERS && canPlaceTower && currentType != ET_NONE)
        {
            towers[towerLen++] = (Tower){
                .rect = {tileX * TOWER_SIZE, tileY * TOWER_SIZE, TOWER_SIZE, TOWER_SIZE},
                .center = {(tileX + 0.5) * TOWER_SIZE, (tileY + 0.5) * TOWER_SIZE},
                .type = currentType,
                .scale = currentScale,
                .range = TOWER_RANGE,
                .cooldown = 60,
            };
        }

        if (paused)
        {
            goto afterLogic;
        }

        // ------------------ Logic ------------------
        for (int i_enemy = 0; i_enemy < enemiesLen; ++i_enemy)
        {
            Enemy *e = enemies + i_enemy;
            if (!e->alive)
                continue;

            // tower in range -> shoot
            for (int i_tower = 0; i_tower < towerLen; ++i_tower)
            {
                Tower *t = towers + i_tower;
                if (frame - t->lastShot < t->cooldown)
                    continue;
                if (!CheckCollisionCircles(e->pos, ENEMY_SIZE, t->center, t->range))
                    continue;
                if (!canTarget(t->type, e->health))
                    continue;
                if (hasAlreadyTargeted(t->enemiesShot, TOWER_LIST_SIZE, i_enemy+1))
                    continue;

                shots[shotHead % MAX_SHOTS] = (Shot){
                    .tower = i_tower,
                    .target = i_enemy, 
                    .type = t->type,
                    .scale = t->scale,
                    .life = 0,
                };
                ++shotHead;
                t->lastShot = frame;
                t->enemiesShot[t->shotIndex % TOWER_LIST_SIZE] = (i_enemy + 1);
                t->shotIndex++;
            }

            // touch home -> remove itself + health
            if (CheckCollisionPointRec(e->pos, home.rect))
            {
                --home.health;
                e->alive = false;
                continue;
            }

            e->pos = Vector2Add(e->pos, e->speed);
        }
        for (int i_shot = shotTail; i_shot != shotHead; ++i_shot)
        {
            Shot *s = shots + (i_shot % MAX_SHOTS);

            if (s->life != SHOT_LIFETIME)
            {
                s->life += 1;
                continue;
            }

            // shot has reached target
            assert(s->target < enemiesLen);
            Enemy *e = enemies + s->target;
            if (!canTarget(s->type, e->health)) // prevent NaN
            {
                ++shotTail;
                continue;
            }

            if (!takeHealth(e, s))
            {
                e->alive = false;
            }

            ++shotTail;
        }
        // spawn new enemies
        for (int i_queue = queueTail; i_queue != queueHead; ++i_queue)
        {
            EnemyQueue e = queue[i_queue % QUEUE_SIZE];
            if (e.spawnFrame - frame < frame - e.spawnFrame)
                break;

            assert(enemiesLen < MAX_ENEMIES);
            enemies[enemiesLen++] = (Enemy){
                .pos = {screenWidth + 50, screenHeight / 2},
                .speed = {-0.5, 0},
                .health = e.health,
                .alive = true,
            };
            ++queueTail;
        }

        ++frame;

afterLogic:
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

        // Towers
        char text[64] = "";
        int textWidthPixels = 0;
        for (int i = 0; i < towerLen; ++i) {
            assert(currentType < ET_EOL);

            Tower t = towers[i];
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
        DrawRectangleRec(home.rect, RED);
        snprintf(text, sizeof(text), "%d", home.health);
        textWidthPixels = MeasureText(text, FONT_SIZE);
        DrawText(text, 
            home.rect.x + (TOWER_SIZE - textWidthPixels) / 2,
            home.rect.y + (TOWER_SIZE - FONT_SIZE) / 2,
            FONT_SIZE,
            BLACK);

        // Enemies
        int aliveCount = 0;
        for (int i = enemiesLen-1; i >= 0; --i)
        {
            Enemy e = enemies[i];
            if (!e.alive)
                continue;

            ++aliveCount;
            DrawCircleV(e.pos, ENEMY_SIZE, enemyColor(e));
            // %g option sometimes seems to hide decimals
            snprintf(text, sizeof(text), "%.2f", e.health);
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
        }

        // Shots
        for (int i = shotTail; i < shotHead; ++i)
        {
            Shot s = shots[i % MAX_SHOTS];

            Vector2 pos = Vector2Lerp(towers[s.tower].center, enemies[s.target].pos, s.life / SHOT_LIFETIME);
            DrawCircleV(pos, SHOT_SIZE, RED);
        }

        EndMode2D();

        // GUI
        int btnPos = screenWidth - GUI_SPACING - 60;
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

            unsigned int spawnFrame = frame;
            while (count > 0)
            {
                char *buffer = strdup(healthText);
                char *prev = buffer;
                char *pos = strtok(prev, ",;");
                while (pos != NULL)
                {
                    bool queueIsFull = (queueHead - queueTail >= QUEUE_SIZE);
                    if (queueIsFull)
                        break;

                    float value = atof(pos);
                    if (value == 0 || !isfinite(value))
                        continue;

                    queue[queueHead % QUEUE_SIZE] = (EnemyQueue){
                        .spawnFrame = spawnFrame,
                        .health = atof(pos),
                    };
                    ++queueHead;
                    spawnFrame += spacing;
                    prev = pos;
                    pos = strtok(NULL, ",;");
                }

                free(buffer);
                --count;
            }
        }

        int xPos = 4;
        int yPos = screenHeight - BUTTON_SIZE - GUI_SPACING;
        if (GuiButton((Rectangle){xPos, yPos, BUTTON_SIZE, (BUTTON_SIZE - GUI_SPACING) / 2}, 
            GuiIconText(ICON_ARROW_UP, NULL)))
        {
            ++currentScale;
        }
        if (GuiButton((Rectangle){xPos, yPos + (BUTTON_SIZE + GUI_SPACING) / 2, BUTTON_SIZE, (BUTTON_SIZE - GUI_SPACING) / 2}, 
            GuiIconText(ICON_ARROW_UP, NULL)))
        {
            if (currentScale > 1)
                --currentScale;
        }
        xPos += BUTTON_SIZE + GUI_SPACING;
        for (int i = 0; i < ET_EOL; ++i)
        {
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
        snprintf(text, sizeof(text), "Towers: %d / %d", towerLen, MAX_TOWERS);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Enemies: %d - (%d / %d)", aliveCount, enemiesLen, MAX_ENEMIES);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Queue: %d: %d -> %d", queueHead - queueTail, queueTail, queueHead);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Shots: %d: %d -> %d", shotHead - shotTail, shotTail, shotHead);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;

        if (paused)
        {
            DrawText("PAUSED", screenWidth / 2, screenHeight / 2, FONT_SIZE, BLACK);
        }

        DrawFPS(screenWidth - 80, 0);

        GuiUnlock();
        EndDrawing();
    }

    // De-Initialization
    CloseWindow();

    return 0;
}
