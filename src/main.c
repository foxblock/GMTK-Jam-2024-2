#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <float.h>

#include "raylib.h"
#include "raymath.h"

typedef enum EquationType 
{
    ET_ADD,
    ET_SUB,
    ET_MULT,
    ET_DIV,
    ET_SQRT,
    ET_LOG_E,

    ET_EOL,
} EquationType;
const char* SIGNS[ET_EOL] = {
    "+%d", "-%d", "*%d", "/%d", "root()", "log_e()"
};

#define TOWER_SIZE 50
#define TOWER_RANGE 150
typedef struct Tower
{
    Rectangle rect;
    Vector2 center;
    EquationType type;
    int scale;
    int range;
    unsigned int lastShot; // in frames
    unsigned int cooldown; // in frames
} Tower;

typedef struct Home
{
    Rectangle rect;
    int health;
} Home;

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
#define HEALTH_ROUNDING 100
typedef struct Shot
{
    int tower;
    int target;
    EquationType type;
    int scale;
    int life;
} Shot;

#define FONT_SIZE 20
#define MIN_FONT_SIZE 5

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
            return enemy > 0;
        case ET_LOG_E:
            return enemy > 0;
        default:
            assert(tower);
    }
}

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

    unsigned int frame = 0;

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

    const int MAX_SHOTS = 1024;
    Shot *shots = calloc(MAX_SHOTS, sizeof(shots[0]));
    unsigned int shotHead = 0;
    unsigned int shotTail = 0;

    // Main game loop
    while (!WindowShouldClose()) // Detect window close button or ESC key
    {
        // Input
        if (IsKeyPressed(KEY_R))
        {
            towerLen = 0;
            home.health = 10;
            shotHead = shotTail = 0;
            enemiesLen = 0;
        }

        int tileX = GetMouseX() / TOWER_SIZE;
        int tileY = GetMouseY() / TOWER_SIZE;

        if (GetMouseWheelMove() < 0)
        {
            --currentType;
            if (currentType < 0)
                currentType = ET_EOL-1;
        }
        else if (GetMouseWheelMove() > 0)
        {
            ++currentType;
            if (currentType >= ET_EOL)
                currentType = 0;
        }
        if (IsKeyPressed(KEY_UP))
            ++currentScale;
        else if (IsKeyPressed(KEY_DOWN) && currentScale > 0)
            --currentScale;

        if (IsKeyPressed(KEY_SPACE))
        {
            assert(enemiesLen < MAX_ENEMIES);

            enemies[enemiesLen++] = (Enemy){
                .pos = {screenWidth + 50, screenHeight / 2},
                .speed = {-0.5, 0},
                .health = 10,
                .alive = true,
            };
        }

        bool posValid = !CheckCollisionPointRec(GetMousePosition(), path);
        posValid &= !CheckCollisionPointRec(GetMousePosition(), home.rect);
        for (int i = 0; i < towerLen; ++i) {
            posValid &= !CheckCollisionPointRec(GetMousePosition(), towers[i].rect);
        }

        if (IsMouseButtonPressed(1) && towerLen < MAX_TOWERS && posValid)
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

        // Logic
        for (int i_enemy = 0; i_enemy < enemiesLen; ++i_enemy)
        {
            Enemy *e = enemies + i_enemy;
            if (!e->alive)
                continue;

            // check for =0 with 2 decimals
            if (fabs(e->health) < FLT_EPSILON)
            {
                e->alive = false;
                continue;
            }

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

                shots[shotHead % MAX_SHOTS] = (Shot){
                    .tower = i_tower,
                    .target = i_enemy, 
                    .type = t->type,
                    .scale = t->scale,
                    .life = 0,
                };
                ++shotHead;
                t->lastShot = frame;
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
        for (int i_shot = shotTail; i_shot < shotHead; ++i_shot)
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

            switch (s->type)
            {
                case ET_ADD: e->health += s->scale; break;
                case ET_SUB: e->health -= s->scale; break;
                case ET_MULT: e->health *= s->scale; break;
                case ET_DIV: e->health /= s->scale; break;
                case ET_SQRT: e->health = sqrtf(e->health); break;
                case ET_LOG_E: e->health = logf(e->health); break;
            }
            e->health = roundf(e->health * HEALTH_ROUNDING) / HEALTH_ROUNDING;
            ++shotTail;
        }

        // Draw
        BeginDrawing();

        ClearBackground(LIGHTGRAY);

        BeginMode2D(camera);

        DrawRectangleRec(path, WHITE);

        // placement preview
        DrawRectangle(tileX * TOWER_SIZE, tileY * TOWER_SIZE, TOWER_SIZE, TOWER_SIZE, posValid ? GRAY : MAROON);
        if (posValid)
        {
            DrawCircleLines((tileX + 0.5) * TOWER_SIZE, (tileY + 0.5) * TOWER_SIZE, TOWER_RANGE, BLACK);
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
        for (int i = 0; i < enemiesLen; ++i)
        {
            Enemy e = enemies[i];
            if (!e.alive)
                continue;

            ++aliveCount;
            DrawCircleV(e.pos, ENEMY_SIZE, SKYBLUE);
            snprintf(text, sizeof(text), "%.2g", e.health);
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
            Shot s = shots[i];

            Vector2 pos = Vector2Lerp(towers[s.tower].center, enemies[s.target].pos, s.life / SHOT_LIFETIME);
            DrawCircleV(pos, SHOT_SIZE, GREEN);
        }

        EndMode2D();

        // GUI
        int yPos = 4;
        snprintf(text, sizeof(text), "Towers: %d / %d", towerLen, MAX_TOWERS);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Enemies: %d", aliveCount);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Current sign: %s", SIGNS[currentType]);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Current scale: %d", currentScale);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);

        DrawFPS(screenWidth - 80, 0);

        EndDrawing();

        ++frame;
    }

    // De-Initialization
    CloseWindow();

    return 0;
}
