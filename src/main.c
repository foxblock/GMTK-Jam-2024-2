#include "raylib.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

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
typedef struct Tower
{
    Vector2 pos;
    EquationType type;
    int scale;
} Tower;

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

    const int MAX_TOWERS = 20;
    Tower *towers = calloc(MAX_TOWERS, sizeof(Tower));
    int towerLen = 0;
    int currentType = ET_SUB;
    int currentScale = 1;

    // Main game loop
    while (!WindowShouldClose()) // Detect window close button or ESC key
    {
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
        if (IsMouseButtonPressed(1) && towerLen < MAX_TOWERS)
        {
            towers[towerLen++] = (Tower){
                .pos = (Vector2){tileX * TOWER_SIZE, tileY * TOWER_SIZE},
                .type = currentType,
                .scale = currentScale,
            };
        }

        // Draw
        BeginDrawing();

        ClearBackground(RAYWHITE);

        BeginMode2D(camera);

        // placement preview
        DrawRectangle(tileX * TOWER_SIZE, tileY * TOWER_SIZE, TOWER_SIZE, TOWER_SIZE, LIGHTGRAY);

        char text[64] = "";
        int textWidthPixels = 0;
        for (int i = 0; i < towerLen; ++i) {
            assert(currentType < ET_EOL);

            Tower t = towers[i];

            DrawRectangleV(t.pos, (Vector2){TOWER_SIZE, TOWER_SIZE}, GRAY);

            snprintf(text, sizeof(text), SIGNS[t.type], t.scale);
            textWidthPixels = MeasureText(text, 20);
            DrawText(text, 
                t.pos.x + (TOWER_SIZE - textWidthPixels) / 2,
                t.pos.y + (TOWER_SIZE - FONT_SIZE) / 2,
                FONT_SIZE,
                BLACK);
        }

        EndMode2D();

        int yPos = 4;
        snprintf(text, sizeof(text), "Towers: %d", towerLen);
        DrawText(text, 4, yPos, 20, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Current sign: %s", SIGNS[currentType]);
        DrawText(text, 4, yPos, 20, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Current scale: %d", currentScale);
        DrawText(text, 4, yPos, 20, BLACK);

        EndDrawing();
    }

    // De-Initialization
    CloseWindow();

    return 0;
}
