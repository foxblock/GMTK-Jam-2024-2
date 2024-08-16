#include "raylib.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

typedef enum EquationType 
{
    ET_ADD,
    ET_SUB,
    ET_MULT,
    ET_DIV,
    ET_NEG_EXP,
    ET_SQRT,
    ET_LOG,

    ET_EOL,
} EquationType;
const char* SIGNS[ET_EOL] = {
    "+%d", "-%d", "*%d", "/%d", "^(-%d)", "root_%d()", "log_%d()"
};

#define TOWER_SIZE 50
#define TOWER_RANGE 150
typedef struct Tower
{
    Rectangle rect;
    EquationType type;
    int scale;
    int range;
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

#define FONT_SIZE 20

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

    // Main game loop
    while (!WindowShouldClose()) // Detect window close button or ESC key
    {
        // Input
        if (IsKeyPressed(KEY_R))
        {
            towerLen = 0;
            home.health = 10;
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
                .type = currentType,
                .scale = currentScale,
                .range = TOWER_RANGE,
            };
        }

        // Logic
        for (int i = 0; i < enemiesLen; ++i)
        {
            Enemy *e = enemies + i;
            if (!e->alive)
                continue;

            if (CheckCollisionPointRec(e->pos, home.rect))
            {
                --home.health;
                e->alive = false;
                continue;
            }

            e->pos.x += e->speed.x;
            e->pos.y += e->speed.y;
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
            textWidthPixels = MeasureText(text, 20);
            DrawText(text, 
                t.rect.x + (TOWER_SIZE - textWidthPixels) / 2,
                t.rect.y + (TOWER_SIZE - FONT_SIZE) / 2,
                FONT_SIZE,
                WHITE);
        }

        // Home
        DrawRectangleRec(home.rect, RED);
        snprintf(text, sizeof(text), "%d", home.health);
        textWidthPixels = MeasureText(text, 20);
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
            textWidthPixels = MeasureText(text, 20);
            DrawText(text, 
                e.pos.x - textWidthPixels / 2,
                e.pos.y - FONT_SIZE / 2,
                FONT_SIZE,
                BLACK);
        }

        EndMode2D();

        // GUI
        int yPos = 4;
        snprintf(text, sizeof(text), "Towers: %d / %d", towerLen, MAX_TOWERS);
        DrawText(text, 4, yPos, 20, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Enemies: %d", aliveCount);
        DrawText(text, 4, yPos, 20, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Current sign: %s", SIGNS[currentType]);
        DrawText(text, 4, yPos, 20, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Current scale: %d", currentScale);
        DrawText(text, 4, yPos, 20, BLACK);

        DrawFPS(screenWidth - 80, 0);

        EndDrawing();
    }

    // De-Initialization
    CloseWindow();

    return 0;
}
