#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <float.h>
#include <limits.h>

#include "raylib.h"
#include "raymath.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#undef RAYGUI_IMPLEMENTATION

#define JS_BASE64_IMPLEMENTATION
#include "base64.h"
#undef JS_BASE64_IMPLEMENTATION

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ALL_BITS_SET ((unsigned int)-1)
#define FLAG_TEST(v, index) ((v & 1 << index) > 0)
#define FLAG_TOGGLE(v, index) (v ^= 1 << index)

typedef struct TowerDefault
{
    const char *text;
    int scale;
    bool (*canTarget)(float health);
    bool canUpgrade;
} TowerDefault;

static bool _target_all(float health) { return true; }
static bool _target_positive(float health) { return health > 0; }
static bool _target_tan(float health)
{
    if (fabsf(health - (int)health) > FLT_EPSILON)
        return true;
    return ((int)health % 90 != 0) || ((int)health % 180 == 0);
}

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
    ET_ROUND,
    ET_SIN,
    ET_COS,
    ET_TAN,
    ET_EOL,
} EquationType;

const TowerDefault TOWER_DEF[] = {
    [ET_NONE] = { "none", 0, NULL, false },
    [ET_ADD] = { "+%d", 1, _target_all, true },
    [ET_SUB] = { "-%d", 1, _target_all, true },
    [ET_MULT] = { "*%d", 2, _target_all, true },
    [ET_DIV] = { "/%d", 2, _target_all, true },
    [ET_SQR] = { "x²", 1, _target_all, false },
    [ET_SQRT] = { "sqrt()", 1, _target_positive, false },
    [ET_LOG_E] = { "ln()", 1, _target_positive, false },
    [ET_LOG_2] = { "log_2()", 1, _target_positive, false },
    [ET_LOG_10] = { "log_10", 1, _target_positive, false },
    [ET_ROUND] = { "round\n%.*f", 1, _target_all, true },
    [ET_SIN] = { "sin", 1, _target_all, false },
    [ET_COS] = { "cos", 1, _target_all, false },
    [ET_TAN] = { "tan", 1, _target_tan, false },
};
static_assert(ARRAY_SIZE(TOWER_DEF) == ET_EOL);

#define HEALTH_DEFAULT 10
// TODO: Split this more sensibly into "LevelParams" struct or something
typedef struct Home // also level parameters
{
    Rectangle rect;
    int health;
    unsigned int allowedTowers; // bit mask of EquationType entries
    unsigned int minTowers;
    int roundingFactor;
    int score;
    int levelIndex;
    bool upgradeAllowed;
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
    Vector2 tile;
    EquationType type;
    int scale;
    float range;
    unsigned int lastShot; // in frames
    unsigned int cooldown; // in frames
    int enemiesShot[TOWER_LIST_SIZE];
    unsigned int shotIndex;
} Tower;

#define SAVED_MSG_LIFETIME 60
#define SAVED_MOVEY_PER_FRAME -0.2f
typedef struct SavedMessage
{
    Vector2 pos;
    int frames;
} SavedMessage;

typedef struct GameState
{
    Home home;
    Rectangle path;

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

    SavedMessage *msg;
    unsigned int msgIndex;
} GameState;

#define MAX_TOWERS 32
#define MAX_ENEMIES 1024
#define QUEUE_SIZE 64
#define SAVED_MSGS_MAX 32
#define MAX_SIMUL_SHOTS MAX_TOWERS
void state_init(GameState *s)
{
    s->home = (Home){
        .rect = {50, 200, TOWER_SIZE, TOWER_SIZE},
        .health = 10,
        .allowedTowers = ALL_BITS_SET, // all by default
    };
    s->path = (Rectangle){100, 200, GetScreenWidth() - 100.f, TOWER_SIZE};

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

    s->msg = calloc(SAVED_MSGS_MAX, sizeof(SavedMessage));
    s->msgIndex = 0;
}

void state_free(GameState *s)
{
    free(s->towers);
    free(s->enemies);
    free(s->queue);
    free(s->shots);
    free(s->msg);
}

void state_reset(GameState *s)
{
    s->towerLen = 0;
    s->home.health = HEALTH_DEFAULT;
    s->home.score = 0;
    s->enemiesLen = 0;
    s->queueHead = s->queueTail = 0;
    s->shotHead = s->shotTail = 0;
    s->msgIndex = 0;
}

void state_addTower(Tower *towers, unsigned int *towerLen, int tileX, int tileY, int type, int scale)
{
    assert(towers);
    assert(towerLen);
    assert(*towerLen < MAX_TOWERS);

    towers[*towerLen] = (Tower){
        .rect = {(float)tileX * TOWER_SIZE, (float)tileY * TOWER_SIZE, TOWER_SIZE, TOWER_SIZE},
        .center = {(tileX + 0.5f) * TOWER_SIZE, (tileY + 0.5f) * TOWER_SIZE},
        .tile = {(float)tileX, (float)tileY},
        .type = type,
        .scale = scale,
        .range = TOWER_RANGE,
        .cooldown = 60,
    };
    *towerLen += 1;
}

// returns true if all entries were added
bool state_addQueueFromString(GameState *s, unsigned int startFrame, const char *queue, unsigned int count, unsigned int spacing)
{
    assert(s);
    assert(spacing > 0);

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

            float value = (float)atof(pos);
            if (value == 0 || !isfinite(value))
                continue;

            s->queue[s->queueHead % QUEUE_SIZE] = (EnemyQueue){
                .spawnFrame = spawnFrame,
                .health = (float)atof(pos),
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

typedef enum TakeHealthResult
{
    TH_DEAD,
    TH_ALIVE,
    TH_SAVED_BY_ROUNDING,
} TakeHealthResult;

// takes health and returns state of enemy
TakeHealthResult takeHealth(Enemy *e, Tower *t, int rounding)
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
        case ET_ROUND: e->health = roundf(e->health * powf(10, (float)t->scale - 1)) / powf(10, (float)t->scale - 1); break;
        case ET_SIN: e->health = sinf(DEG2RAD * e->health); break;
        case ET_COS: e->health = cosf(DEG2RAD * e->health); break;
        case ET_TAN: e->health = tanf(DEG2RAD * e->health); break;
        default:
            printf("ERROR: Type of tower unknown: %d\n", t->type);
            assert(false);
    }
    float healthNotRounded = 0;
    if (rounding > 0)
    {
        healthNotRounded = (float)(int)(e->health * rounding) / rounding;
        e->health = roundf(e->health * rounding) / rounding;
    }

    if (fabs(e->health) < FLT_EPSILON)
    {
        return TH_DEAD;
    }
    if (rounding > 0 && fabs(healthNotRounded) < FLT_EPSILON)
    {
        return TH_SAVED_BY_ROUNDING;
    }
    return TH_ALIVE;
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

void checkPlaceTower(bool *canPlace, int *upgradeIndex, 
    GameState *state, Vector2 pos, EquationType selectedType, Rectangle *guiRects, int guiRectCount)
{
    assert(canPlace);
    assert(state);

    if (!*canPlace)
        return;

    if (state->towerLen >= MAX_TOWERS) { *canPlace = false; return; }
    if (selectedType == ET_NONE) { *canPlace = false; return; }
    if (CheckCollisionPointRec(pos, state->path)) { *canPlace = false; return; }
    if (CheckCollisionPointRec(pos, state->home.rect))  { *canPlace = false; return; }

    for (int idx = 0; idx < guiRectCount; ++idx)
    {
        if (CheckCollisionPointRec(pos, guiRects[idx]))
        {
            *canPlace = false;
            return;
        }
    }

    for (unsigned int i = 0; i < state->towerLen; ++i) {
        if (CheckCollisionPointRec(pos, state->towers[i].rect))
        {
            EquationType typeAtMouse = state->towers[i].type;
            if (typeAtMouse != selectedType)
                *canPlace = false;
            else if (!TOWER_DEF[typeAtMouse].canUpgrade || !state->home.upgradeAllowed)
                *canPlace = false;
            else
                *upgradeIndex = i;
            return;
        }
    }
}

int countAlive(Enemy *array, unsigned int count)
{
    int result = 0;
    for (unsigned int i = 0; i < count; ++i)
    {
        if (!array[i].alive)
            continue;

        ++result;
    }
    return result;
}

Color enemyColor(float health)
{
    if (health >= 1)
        return SKYBLUE;
    if (health <= -1)
        return (Color){ 135, 255, 105, 255 };
    return PINK;
}

#define FONT_SIZE 20
#define MIN_FONT_SIZE 10
#define BUTTON_SIZE 40
#define GUI_SPACING 4
typedef enum EditBox 
{
    EB_NONE = -1,
    EB_NAME,
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
    bool upgradeAllowed;
} LevelDef;

const LevelDef LEVELS[] = {
    {
        .name = "Learning to count",
        .cat = LC_NATURAL,
        .health = "1,2,3,4,5",
        .count = 3,
        .spacing = QUEUE_SPACING_DEFAULT,
        .towersAllowed = (1 << ET_NONE) | (1 << ET_ADD) | (1 << ET_SUB),
        .minSolution = 5, // [-1] * 5
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
        .name = "Glass half full",
        .cat = LC_RATIONAL,
        // 4,8,12,16
        .health = "1.5,3.5,5.5,7.5",
        .count = 4,
        .spacing = QUEUE_SPACING_DEFAULT,
        .towersAllowed = (1 << ET_NONE) | (1 << ET_ADD) | (1 << ET_SUB) | (1 << ET_MULT) | (1 << ET_DIV),
        .minSolution = 8, // [*2], [+1], [/2] * 2, [-1] * 4
        .roundingFactor = 10,
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
        .name = "Built to scale",
        .cat = LC_RATIONAL,
        .health = "1,10,100,1e4,1e5,1e6,1e7,1e8,1e9,1e10",
        .count = 2,
        .spacing = QUEUE_SPACING_DEFAULT,
        .towersAllowed = (1 << ET_NONE) | (1 << ET_ADD) | (1 << ET_SUB) | (1 << ET_MULT) | (1 << ET_DIV) | (1 << ET_SQR) | (1 << ET_SQRT) | (1 << ET_LOG_10),
        .minSolution = 8, // [log_10] * 2, [+1], [sqrt] * 4, [-1]
        .roundingFactor = 10,
    },
    {
        .name = "Broken Countdown",
        .cat = LC_RATIONAL,
        .health = "32,-31,30,-29,28,-27,26,-25,24,-23,22,-21,20,-19,18,-17,16,-15,14,-13,12,-11,10,-9,8,-7,6,-5,4,-3,2,-1",
        .count = 1,
        .spacing = QUEUE_SPACING_DEFAULT,
        .towersAllowed = (1 << ET_NONE) | (1 << ET_ADD) | (1 << ET_SUB) | (1 << ET_MULT) | (1 << ET_DIV) | (1 << ET_SQR) | (1 << ET_SQRT) | (1 << ET_LOG_10),
        .minSolution = 9, // [²], [log_10], [+1], [sqrt]*5, [-1]
        .roundingFactor = 10,
    },
    {
        .name = "My little brother",
        .cat = LC_REAL,
        .health = "1,0.1,0.01",
        .count = 5,
        .spacing = QUEUE_SPACING_DEFAULT,
        .towersAllowed = (1 << ET_NONE) | (1 << ET_ADD) | (1 << ET_MULT) | (1 << ET_DIV) | (1 << ET_SQR) | (1 << ET_SQRT) | (1 << ET_LOG_10),
        .minSolution = 3, // [log_10], [+1] * 2 // [sqr] * 2, [log_10]
        .roundingFactor = 100,
    },
};

void state_loadFromLevelDef(GameState *state, LevelDef l, int index)
{
    assert(state);

    state_addQueueFromString(state, 0, l.health, l.count, l.spacing);
    state->home.allowedTowers = l.towersAllowed;
    state->home.minTowers = l.minSolution;
    state->home.roundingFactor = l.roundingFactor;
    state->home.upgradeAllowed = l.upgradeAllowed;
    state->home.levelIndex = index;
}

#define LEVEL_STR_VERSION 2
static const int VERSION_SIZE = 1 + 1; // const char + version
static const int LEVEL_NAME_SIZE = 1;
#define HEADER_SIZE (VERSION_SIZE + LEVEL_NAME_SIZE)
#define HEADER_ENC_SIZE (base64_strlen(HEADER_SIZE)-1) // minus 1 for null terminator
static const int HEALTH_LEN_SIZE = 1; // health
static const int LEVEL_DEF_SKIP_SIZE = 2 * sizeof(const char*) + sizeof(LevelCat);
static const int TOWER_LEN_SIZE = 1;
static const int TOWER_BYTES_SIZE = sizeof(int) * 4;
#define LEVEL_MIN_SIZE (HEADER_SIZE + HEALTH_LEN_SIZE + sizeof(LevelDef) - LEVEL_DEF_SKIP_SIZE + TOWER_LEN_SIZE)
static const char LEVEL_IDENT_CHAR = 'L';
static const char SOLUTION_IDENT_CHAR = 'S';

bool state_levelToString(char *output, size_t outputLen, LevelDef level, Tower towers[], int towerCnt)
{
    assert(output);
    assert(level.name);
    assert(level.health);
    assert(LEVEL_STR_VERSION < 256);
    assert(towerCnt < 256);

    const int towerLen = TOWER_BYTES_SIZE * towerCnt;
    const size_t nameLen = strlen(level.name);
    const size_t healthLen = strlen(level.health);
    if (nameLen > 255)
        return false;
    if (healthLen > 255)
        return false;
    size_t byteSize = LEVEL_MIN_SIZE + healthLen + towerLen; // name is not encoded
    if (outputLen < base64_strlen(byteSize) + nameLen)
        return false;
    
    unsigned char *bytes = calloc(byteSize, 1);
    assert(bytes != NULL);
    size_t idx = 0;

    if (towerCnt > 0)
        bytes[idx++] = SOLUTION_IDENT_CHAR;
    else
        bytes[idx++] = LEVEL_IDENT_CHAR;
    bytes[idx++] = LEVEL_STR_VERSION;
    bytes[idx++] = (unsigned char)nameLen;
    // NOTE (JS, 25.04.26): we split after these 3 bytes (4 encoded) to insert the name in cleartext later
    // I think this is nice, because you can easily identify level strings by the name

    bytes[idx++] = (unsigned char)healthLen;
    memcpy(bytes + idx, level.health, healthLen);
    idx += healthLen;

    unsigned char *levelStart = (unsigned char*)&level + LEVEL_DEF_SKIP_SIZE;
    const int levelLen = sizeof(level) - LEVEL_DEF_SKIP_SIZE;
    memcpy(bytes + idx, levelStart, levelLen);
    idx += levelLen;

    bytes[idx++] = (unsigned char)towerCnt;
    for (int t = 0; t < towerCnt; ++t)
    {
        int *bytesStart = (int*)(bytes + idx);
        bytesStart[0] = (int)towers[t].tile.x;
        bytesStart[1] = (int)towers[t].tile.y;
        bytesStart[2] = towers[t].type;
        bytesStart[3] = towers[t].scale;
        idx += TOWER_BYTES_SIZE;
    }

    assert(idx == byteSize);

    const size_t encSize = base64_encode(output, outputLen, bytes, byteSize);
    if (encSize == 0)
    {
        free(bytes);
        return false;
    }

    // make room for name
    memmove(output + nameLen + HEADER_ENC_SIZE, output + HEADER_ENC_SIZE, encSize - HEADER_ENC_SIZE);
    // replace all whitespace with underscores 
    // NOTE (JS, 25.04.26): do not change length of name, since we already encoded the length!
    char *nameDup = strdup(level.name);
    static const char *whitespace = " \t\r\n";
    char *whitePos = strpbrk(nameDup, whitespace);
    while (whitePos != NULL)
    {
        *whitePos = '_';
        whitePos = strpbrk(nameDup, whitespace);
    }
    // insert name
    memcpy(output + 4, nameDup, nameLen);
    free(nameDup);

    free(bytes);
    return true;
}

bool state_levelFromString(LevelDef *output, char *name, size_t nameCap, char *health, size_t healthCap,
    Tower towers[], unsigned int *towerCnt, 
    const char *input)
{
    assert(output);
    assert(name);
    assert(health);
    assert(towers);
    assert(towerCnt);
    assert(input);

    size_t inputLen = strlen(input);
    if (inputLen < base64_strlen(LEVEL_MIN_SIZE))
        return false;

    unsigned char bytes[2048] = {0};
    // Format is 4 bytes "header" + name cleartext + rest encoded, so we split it
    // and just decode the header first
    size_t bytesLen = base64_decode(bytes, sizeof(bytes), input, HEADER_ENC_SIZE);
    if (bytesLen == 0)
        return false;

    size_t idx = 0;
    bool isSolution = false;
    if (bytes[idx] == SOLUTION_IDENT_CHAR)
        isSolution = true;
    else if (bytes[idx] != LEVEL_IDENT_CHAR)
        return false;
    idx += 1;
    
    int version = bytes[idx++];
    if (version != LEVEL_STR_VERSION)
        return false;
    
    int nameLen = bytes[idx++];
    if (nameLen > nameCap)
        return false;
    if (nameLen > inputLen - idx)
        return false;
    memcpy(name, input + HEADER_ENC_SIZE, nameLen);
    name[nameLen] = 0;
    // NOTE (JS, 25.04.26): This will override all underscores in the original name, 
    // but that is fine to me...
    for (int ndx = 0; ndx < nameLen; ++ndx)
        if (name[ndx] == '_')
            name[ndx] = ' ';

    // Header finished, decode the rest now
    idx = 0;
    input += HEADER_ENC_SIZE + nameLen;
    inputLen -= HEADER_ENC_SIZE + nameLen;
    bytesLen = base64_decode(bytes, sizeof(bytes), input, inputLen);
    if (bytesLen == 0)
        return false;

    if (idx >= bytesLen)
        return false;
    int healthLen = bytes[idx++];
    if (healthLen > bytesLen - idx)
        return false;
    if (healthLen > healthCap)
        return false;
    memcpy(health, bytes + idx, healthLen);
    health[healthLen] = 0;
    idx += healthLen;

    int structLen = sizeof(*output) - LEVEL_DEF_SKIP_SIZE;
    if (structLen > bytesLen - idx)
        return false;
    unsigned char *structStart = (unsigned char*)output + LEVEL_DEF_SKIP_SIZE;
    memcpy(structStart, bytes + idx, structLen);
    idx += structLen;

    if (idx >= bytesLen)
        return false;
    int towerLen = bytes[idx++];
    if (towerLen * TOWER_BYTES_SIZE > bytesLen - idx)
        return false;
    if (towerLen > MAX_TOWERS)
        return false;
    for (int t = 0; t < towerLen; ++t)
    {
        int *bytesStart = (int*)(bytes + idx);
        state_addTower(towers, towerCnt, bytesStart[0], bytesStart[1], bytesStart[2], bytesStart[3]);
        idx += TOWER_BYTES_SIZE;
    }

    assert(idx == bytesLen);

    return true;
}

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
    
    size_t res = fread(&data->progress, sizeof(data->progress), 1, f);
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
    
    size_t res = fwrite(data, sizeof(*data), 1, f);
    if (res != sizeof(*data))
        return false;

    return true;
}

const int screenWidth = 800;
const int screenHeight = 450;
Scene scene;
Savegame save;
RenderTexture2D screen;
float scale = 1.f;

void menu(void);
void tutorial(void);
void level_select(GameState *state);
void level(GameState *state);
void playground(GameState *state);

void level_logic(GameState *state, unsigned int frame);
void level_draw(GameState *state);

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "A puzzling tower defense game for beautiful math nerds.");
    SetWindowMinSize(screenWidth, screenHeight);

    SetTargetFPS(60);
    SetExitKey(0); // disable close on ESC
    
    // Render texture initialization, used to hold the rendering result so we can easily resize it
    screen = LoadRenderTexture(screenWidth, screenHeight);
    assert(screen.id != 0);
    SetTextureFilter(screen.texture, TEXTURE_FILTER_ANISOTROPIC_16X);  // Texture scale filter to use

    base64_test();

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
                state.home.allowedTowers = ALL_BITS_SET;
                state.home.roundingFactor = 100;
                level_select(&state);
                break;
            case SC_LEVEL:
                level(&state);
                break;
            case SC_PLAYGROUND:
                state_reset(&state);
                state.home.allowedTowers = ALL_BITS_SET;
                state.home.roundingFactor = 100;
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

    UnloadRenderTexture(screen);

    // De-Initialization
    CloseWindow();

    return 0;
}

void UpdateGlobalScaling() 
{
    scale = MIN((float)GetScreenWidth()/screenWidth, (float)GetScreenHeight()/screenHeight);

    SetMouseOffset((int)(-(GetScreenWidth() - screenWidth * scale) / 2.f), (int)(-(GetScreenHeight() - screenHeight * scale) / 2.f));
    SetMouseScale(1/scale, 1/scale);
}

void DrawScreenScaled()
{
    BeginDrawing();

    ClearBackground(BLACK);     // Clear screen background

    // Draw render texture to screen, properly scaled
    DrawTexturePro(screen.texture, (Rectangle){ 0, 0, (float)screenWidth, (float)-screenHeight },
                    (Rectangle){ (GetScreenWidth() - screenWidth * scale) / 2.f, (GetScreenHeight() - screenHeight * scale) / 2.f,
                    screenWidth * scale, screenHeight * scale }, (Vector2){ 0, 0 }, 0, WHITE);

    EndDrawing();
}

void menu(void)
{
    bool sceneChange = false;
    float health[] = {1e7, 0.25, -3};
    Vector2 enemyPos[ARRAY_SIZE(health)] = {
        { screenWidth / 2.f - 100, 190.f },
        { screenWidth / 2.f, 190.f },
        { screenWidth / 2.f + 100, 190.f },
    };

    // Main game loop
    while (!WindowShouldClose() && !sceneChange) // Detect window close button or ESC key
    {
        if (IsWindowResized())
            UpdateGlobalScaling();

        // Draw onto texture unscaled
        BeginTextureMode(screen);

        ClearBackground(LIGHTGRAY);

        int textW = MeasureText("A puzzling tower defense game", 40);
        DrawText("A puzzling tower defense game", (screenWidth - textW) / 2, 40, 40, BLACK);
        textW = MeasureText("for beautiful math nerds.", 40);
        DrawText("for beautiful math nerds.", (screenWidth - textW) / 2, 90, 40, BLACK);
        char text[32] = "";
        
        for (int i = 0; i < ARRAY_SIZE(health); ++i)
        {
            DrawCircleV(enemyPos[i], ENEMY_SIZE * 2, enemyColor(health[i]));
            // %g is confusing. the precision option seems to specify the max total number of
            // significant digits (%.3g of 10.555 prints 10.6, while 0.555 prints 0.555).
            // Sometimes it will round, sometimes it won't (%.3g of 1.555 prints 1.55).
            snprintf(text, sizeof(text), "%.*g", 3, health[i]);
            int fontSize = FONT_SIZE;
            textW = MeasureText(text, fontSize);
            DrawText(text, 
                (int)(enemyPos[i].x - textW / 2),
                (int)(enemyPos[i].y - fontSize / 2),
                fontSize,
                BLACK);
        }

        int yPos = 260;
        if (GuiButton((Rectangle){screenWidth / 2.f - 100, (float)yPos, 200, 24}, "Tutorial"))
        {
            scene = SC_TURORIAL;
            sceneChange = true;
        }
        yPos += 32;
        if (GuiButton((Rectangle){screenWidth / 2.f - 100, (float)yPos, 200, 24}, "Level select"))
        {
            scene = SC_LEVEL_SELECT;
            sceneChange = true;
        }
        yPos += 32;
        if (GuiButton((Rectangle){screenWidth / 2.f - 100, (float)yPos, 200, 24}, "Editor / Playground"))
        {
            scene = SC_PLAYGROUND;
            sceneChange = true;
        }
        yPos += 32;
        if (GuiButton((Rectangle){screenWidth / 2.f - 100, (float)yPos, 96, 24}, "Size 1x"))
        {
            int w = GetScreenWidth();
            int h = GetScreenHeight();
            Vector2 windowPos = GetWindowPosition();
            SetWindowSize(screenWidth, screenHeight);
            SetWindowPosition((int)windowPos.x + (w - screenWidth) / 2, (int)windowPos.y + (h - screenHeight) / 2);
            UpdateGlobalScaling();
        }
        if (GuiButton((Rectangle){screenWidth / 2.f + 4, (float)yPos, 96, 24}, "Size 2x"))
        {
            int w = GetScreenWidth();
            int h = GetScreenHeight();
            Vector2 windowPos = GetWindowPosition();
            SetWindowSize(screenWidth * 2, screenHeight * 2);
            SetWindowPosition((int)windowPos.x + (w - screenWidth*2) / 2, (int)windowPos.y + (h - screenHeight*2) / 2);
            UpdateGlobalScaling();
        }
        yPos += 32;
        if (GuiButton((Rectangle){screenWidth / 2.f - 100, (float)yPos, 200, 24}, "Exit"))
        {
            scene = SC_EXIT;
            sceneChange = true;
        }
        yPos += 32;

        DrawText("Built with raylib", GUI_SPACING, screenHeight - FONT_SIZE - GUI_SPACING, FONT_SIZE, BLACK);
        DrawText("Game by Janek", screenWidth - 156, screenHeight - FONT_SIZE - GUI_SPACING, FONT_SIZE, BLACK);

        GuiUnlock();

        EndTextureMode();

        DrawScreenScaled();
    }
}

void tutorial(void)
{
    bool sceneChange = false;

    // Main game loop
    while (!WindowShouldClose() && !sceneChange) // Detect window close button or ESC key
    {
        if (IsWindowResized())
            UpdateGlobalScaling();

        if (IsKeyPressed(KEY_ESCAPE))
        {
            scene = SC_MENU;
            sceneChange = true;
            break;
        }

        BeginTextureMode(screen);

        ClearBackground(LIGHTGRAY);

        const int spacing = 8;
        int yPos = 16;
        DrawText("- GOAL: Reduce enemy HP to 0 exactly!", 16, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- Health can go negative, only 0 is death.", 16, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- Towers are mathematical functions which are applied on hit.", 16, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- Towers shoot each enemy only once.", 16, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- Towers only execute valid math.", 16, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("(i.e. sqrt() towers cannot target negative health enemies)", 40, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- Towers cannot be sold/deleted, but pressing R will restart the level.", 16, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- Space pauses.", 16, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- Gold stars are awarded for:", 16, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- completing the level", 40, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- not losing health", 40, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing;
        DrawText("- placing the least amount of towers possible", 40, yPos, FONT_SIZE, BLACK);
        yPos += FONT_SIZE + spacing * 3;
        DrawText("THANKS FOR PLAYING", 16, yPos, FONT_SIZE, BLACK);

        if (GuiButton((Rectangle){(float)screenWidth - 124, (float)screenHeight - 28, 120, 24}, "Got it!"))
        {
            scene = SC_MENU;
            sceneChange = true;
        }

        GuiUnlock();

        EndTextureMode();

        DrawScreenScaled();
    }
}

void level_select(GameState *state)
{
    bool sceneChange = false;
    int levelCount = ARRAY_SIZE(LEVELS);
    bool unlockAll = false;

    // Main game loop
    while (!WindowShouldClose() && !sceneChange) // Detect window close button or ESC key
    {
        if (IsWindowResized())
            UpdateGlobalScaling();

        if (IsKeyPressed(KEY_ESCAPE))
        {
            scene = SC_MENU;
            sceneChange = true;
            break;
        }

        BeginTextureMode(screen);

        ClearBackground(LIGHTGRAY);

        DrawText("Level select", 16, 16, FONT_SIZE * 2, BLACK);

        int xPos = 16;
        int yPos = 48;
        int currentCat = -1;
        char text[128] = "";
        for (int i = 0; i < levelCount; ++i)
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
            if (GuiButton((Rectangle){(float)xPos, (float)yPos, 200, 24}, text))
            {
                state_loadFromLevelDef(state, l, i);
                scene = SC_LEVEL;
                sceneChange = true;
            }
            xPos += 200 + GUI_SPACING;
        }
        yPos += 24 + GUI_SPACING * 2;
        DrawText("Complex numbers C", 16, yPos, FONT_SIZE, BLACK);
        DrawText("Just kidding, maybe later...", 16, yPos + FONT_SIZE + GUI_SPACING, FONT_SIZE / 2, BLACK);


        GuiSetState(STATE_NORMAL);
        if (GuiButton((Rectangle){(float)screenWidth - 124, (float)screenHeight - 28, 120, 24}, "Back"))
        {
            scene = SC_MENU;
            sceneChange = true;
        }
        if (GuiButton((Rectangle){(float)screenWidth - 248, (float)screenHeight - 28, 120, 24}, unlockAll ? "You sure?" : "Unlock all"))
        {
            if (!unlockAll)
                unlockAll = true;
            else
                save.progress = INT_MAX;
        }

        GuiUnlock();

        EndTextureMode();

        DrawScreenScaled();
    }
}

void speedControls(int *speedLevel, bool *paused, int xPos)
{
    GuiToggle((Rectangle){(float)xPos, 4, 24, 24}, GuiIconText(ICON_PLAYER_PAUSE, NULL), paused);
    xPos += 24 + GUI_SPACING;
    bool speedBtnActive = (*speedLevel == 1 && !*paused);
    GuiToggle((Rectangle){(float)xPos, 4, 24, 24}, GuiIconText(ICON_PLAYER_PLAY, NULL), &speedBtnActive);
    if (speedBtnActive)
    {
        *speedLevel = 1;
        *paused = false;
    }
    xPos += 24 + GUI_SPACING;
    speedBtnActive = (*speedLevel == 4 && !*paused);
    GuiToggle((Rectangle){(float)xPos, 4, 24, 24}, GuiIconText(ICON_ARROW_RIGHT, NULL), &speedBtnActive);
    if (speedBtnActive)
    {
        *speedLevel = 4;
        *paused = false;
    }
    xPos += 24 + GUI_SPACING;
    speedBtnActive = (*speedLevel == 12 && !*paused);
    GuiToggle((Rectangle){(float)xPos, 4, 24, 24}, GuiIconText(ICON_ARROW_RIGHT_FILL, NULL), &speedBtnActive);
    if (speedBtnActive)
    {
        *speedLevel = 12;
        *paused = false;
    }
}

void level(GameState *state)
{
    assert(state);
    assert(state->queueHead != state->queueTail);

    Camera2D camera = { 0 };
    camera.target = (Vector2){ screenWidth/2.f, screenHeight/2.f };
    camera.offset = (Vector2){ screenWidth/2.f, screenHeight/2.f };
    camera.rotation = 0.f;
    camera.zoom = 1.f;

    unsigned int frame = (unsigned int)-300; // test rollover robustness

    enum LevelGuiArea {
        TOWERS,
        SPEED_CONTROL,
    };
    Rectangle guiAreas[] = {
        [TOWERS] = {0, (float)screenHeight - TOWER_SIZE - 1, (float)screenWidth, TOWER_SIZE + 1},
        [SPEED_CONTROL] = {0, 0, (float)screenWidth, TOWER_SIZE + 1},
    };

    int currentType = ET_NONE;
    bool paused = false;
    bool sceneChange = false;
    int speedLevel = 1;
    int aliveCount = 0;
    bool gameEnded = false;

    EnemyQueue *queueBackup = calloc(QUEUE_SIZE, sizeof(queueBackup[0]));
    memcpy(queueBackup, state->queue, QUEUE_SIZE * sizeof(queueBackup[0]));
    unsigned int queueBackupHead = state->queueHead;

    // Main game loop
    while (!WindowShouldClose() && !sceneChange)
    {
        if (IsWindowResized())
            UpdateGlobalScaling();

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

        int tileX = GetMouseX() / TOWER_SIZE;
        int tileY = GetMouseY() / TOWER_SIZE;
        bool canPlaceTower = !gameEnded;
        int upgradeTowerIndex = -1;

        checkPlaceTower(&canPlaceTower, &upgradeTowerIndex, state, GetMousePosition(), currentType,
                guiAreas, ARRAY_SIZE(guiAreas));

        if (IsMouseButtonPressed(0) && canPlaceTower)
        {
            if (upgradeTowerIndex == -1)
                state_addTower(state->towers, &state->towerLen, tileX, tileY, 
                        currentType, TOWER_DEF[currentType].scale);
            else
                state->towers[upgradeTowerIndex].scale += 1;
        }

        // ------------------ Logic ------------------
        if (!paused)
        {
            for (int i = 0; i < speedLevel; ++i)
            {
                level_logic(state, frame);

                ++frame;
            }
            aliveCount = countAlive(state->enemies, state->enemiesLen);

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
        BeginTextureMode(screen);

        ClearBackground(LIGHTGRAY);

        BeginMode2D(camera);

        DrawRectangleRec(state->path, WHITE);

        // placement preview
        if (currentType != ET_NONE && !gameEnded)
        {
            DrawRectangle(tileX * TOWER_SIZE, tileY * TOWER_SIZE, TOWER_SIZE, TOWER_SIZE, canPlaceTower ? GRAY : MAROON);
            if (canPlaceTower)
            {
                DrawCircleLines((int)((tileX + 0.5f) * TOWER_SIZE), (int)((tileY + 0.5f) * TOWER_SIZE), TOWER_RANGE, GRAY);
            }
        }

        level_draw(state);

        // queue preview
        int ePosX = 60;
        const int ePosY = 4 + ENEMY_SIZE;
        char text[64] = "";
        DrawText("Queue:", 4, ePosY - 4, 10, BLACK);
        for (unsigned int i = state->queueTail; i < state->queueHead; ++i)
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

        speedControls(&speedLevel, &paused, screenWidth - (GUI_SPACING + 24) * 4);
        if (paused)
        {
            int textW = MeasureText("PAUSED", FONT_SIZE * 2);
            DrawText("PAUSED", (screenWidth - textW) / 2, 60, FONT_SIZE * 2, BLACK);
        }

        int xPos = 4;
        int yPos = screenHeight - BUTTON_SIZE - GUI_SPACING;
        for (int tdx = 0; tdx < ET_EOL; ++tdx)
        {
            if (!FLAG_TEST(state->home.allowedTowers, tdx))
                continue;

            snprintf(text, sizeof(text), TOWER_DEF[tdx].text, TOWER_DEF[tdx].scale);
            bool active = currentType == tdx;
            GuiToggle((Rectangle){ (float)xPos, (float)yPos, BUTTON_SIZE, BUTTON_SIZE}, text, &active);
            if (active)
            {
                currentType = tdx;
            }
            xPos += BUTTON_SIZE + GUI_SPACING;
        }

        snprintf(text, sizeof(text), "Par: %d", state->home.minTowers);
        DrawText(text, screenWidth - 150, screenHeight - FONT_SIZE * 2 - GUI_SPACING * 2, FONT_SIZE, BLACK);
        snprintf(text, sizeof(text), "Precision: %.*f", 
                (int)log10f((float)state->home.roundingFactor), 1 / (float)state->home.roundingFactor);
        DrawText(text, screenWidth - 150, screenHeight - FONT_SIZE - GUI_SPACING, FONT_SIZE, BLACK);
        
    #ifdef _DEBUG
        yPos = 4;
        snprintf(text, sizeof(text), "Frame: %u", frame);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Towers: %d / %d", state->towerLen, MAX_TOWERS);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;
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
    #endif

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

            if (GuiButton((Rectangle){screenWidth / 2.f - 120, 212, 116, 24}, "Try again"))
            {
                state_reset(state);
                memcpy(state->queue, queueBackup, QUEUE_SIZE * sizeof(queueBackup[0]));
                state->queueHead = queueBackupHead;
                frame = 0;
                gameEnded = false;
            }
            if (GuiButton((Rectangle){screenWidth / 2.f + 4, 212, 116, 24}, "Go to level select"))
            {
                scene = SC_LEVEL_SELECT;
                sceneChange = true;
            }
        }

        EndTextureMode();

        DrawScreenScaled();
    }
}

void level_logic(GameState *state, unsigned int frame)
{
    for (unsigned int i_enemy = 0; i_enemy < state->enemiesLen; ++i_enemy)
    {
        Enemy *e = state->enemies + i_enemy;
        if (!e->alive)
            continue;

        // tower in range -> shoot
        for (unsigned int i_tower = 0; i_tower < state->towerLen; ++i_tower)
        {
            Tower *t = state->towers + i_tower;
            if (frame - t->lastShot < t->cooldown)
                continue;
            if (!CheckCollisionCircles(e->pos, ENEMY_SIZE, t->center, t->range))
                continue;
            if (!TOWER_DEF[t->type].canTarget(e->health))
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

            int res = takeHealth(e, t, state->home.roundingFactor);

            switch (res)
            {
                case TH_DEAD:
                    e->alive = false;
                    break;
                case TH_SAVED_BY_ROUNDING:
                    state->msg[state->msgIndex++ % SAVED_MSGS_MAX] = (SavedMessage){
                        .pos = { e->pos.x - 30, e->pos.y - ENEMY_SIZE - GUI_SPACING },
                        .frames = SAVED_MSG_LIFETIME,
                    };
                    printf("Saved by rounding\n");
                    break;
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
    for (unsigned int i_shot = state->shotTail; i_shot != state->shotHead; ++i_shot)
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
    for (unsigned int i_queue = state->queueTail; i_queue != state->queueHead; ++i_queue)
    {
        EnemyQueue e = state->queue[i_queue % QUEUE_SIZE];
        // check if frame is in the future (with rollover)
        if (e.spawnFrame - frame < frame - e.spawnFrame)
            break;

        assert(state->enemiesLen < MAX_ENEMIES);
        state->enemies[state->enemiesLen++] = (Enemy){
            .pos = {(float)screenWidth + 50, screenHeight / 2.f},
            .speed = {-0.5f, 0},
            .health = e.health,
            .alive = true,
        };
        ++state->queueTail;
    }
    // advance save msg
    for (int i = 0; i < SAVED_MSGS_MAX; ++i)
    {
        state->msg[i].frames -= 1;
        state->msg[i].pos.y += SAVED_MOVEY_PER_FRAME;
    }
}

void level_draw(GameState *state)
{
    // Towers
    char text[64] = "";
    int textWidthPixels = 0;
    for (unsigned int i = 0; i < state->towerLen; ++i) 
    {
        Tower t = state->towers[i];
        DrawRectangleRec(t.rect, DARKGRAY);
        if (t.type == ET_ROUND)
            snprintf(text, sizeof(text), TOWER_DEF[t.type].text, t.scale-1, 1 / powf(10, (float)t.scale - 1));
        else
            snprintf(text, sizeof(text), TOWER_DEF[t.type].text, t.scale);
        int fontSize = FONT_SIZE;
        textWidthPixels = MeasureText(text, fontSize);
        while (textWidthPixels > TOWER_SIZE && fontSize > MIN_FONT_SIZE)
        {
            fontSize /= 2;
            textWidthPixels = MeasureText(text, fontSize);
        }
        DrawText(text, 
            (int)t.rect.x + (TOWER_SIZE - textWidthPixels) / 2,
            (int)t.rect.y + (TOWER_SIZE - fontSize) / 2,
            fontSize,
            WHITE);
    }

    // Home
    DrawRectangleRec(state->home.rect, RED);
    snprintf(text, sizeof(text), "%d", state->home.health);
    textWidthPixels = MeasureText(text, FONT_SIZE);
    DrawText(text, 
        (int)state->home.rect.x + (TOWER_SIZE - textWidthPixels) / 2,
        (int)state->home.rect.y + (TOWER_SIZE - FONT_SIZE) / 2,
        FONT_SIZE,
        BLACK);

    int roundingDigits = 0;
    if (state->home.roundingFactor > 0)
    {
        roundingDigits = (int)log10(state->home.roundingFactor) + 1;
        if (roundingDigits < 3) // to avoid 1e1 draw on 10
            roundingDigits = 3;
    }

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
        if (roundingDigits > 0)
            snprintf(text, sizeof(text), "%.*g", roundingDigits, e.health);
        else
            snprintf(text, sizeof(text), "%f", e.health);
        int fontSize = FONT_SIZE;
        textWidthPixels = MeasureText(text, fontSize);
        while (textWidthPixels > ENEMY_SIZE && fontSize > MIN_FONT_SIZE)
        {
            fontSize /= 2;
            textWidthPixels = MeasureText(text, fontSize);
        }
        DrawText(text, 
            (int)e.pos.x - textWidthPixels / 2,
            (int)e.pos.y - fontSize / 2,
            fontSize,
            BLACK);
    #ifdef _DEBUG
        snprintf(text, sizeof(text), "%.4f", e.health);
        textWidthPixels = MeasureText(text, 10);
        DrawText(text, 
            (int)e.pos.x - textWidthPixels / 2,
            (int)e.pos.y + fontSize / 2,
            10,
            BLACK);
    #endif
    }

    // Shots
    for (unsigned int i = state->shotTail; i < state->shotHead; ++i)
    {
        Shot s = state->shots[i % MAX_SIMUL_SHOTS];

        Vector2 varTower = {rand() % 4 - 2.f, rand() % 4 - 2.f};
        Vector2 varTarget = {rand() % 8 - 4.f, rand() % 8 - 4.f};

        DrawLineV(
            Vector2Add(state->towers[s.tower].center, varTower), 
            Vector2Add(state->enemies[s.target].pos, varTarget),
            RED);
    }

    // Saved messages
    for (int i = 0; i < SAVED_MSGS_MAX; ++i)
    {
        if (state->msg[i].frames <= 0)
            continue;

        DrawText("Saved by\nRounding", (int)state->msg[i].pos.x, (int)state->msg[i].pos.y, FONT_SIZE / 2, 
            (state->msg[i].frames % 4) < 2 ? RED : BLACK);
    }
}

// Custom button control, returns mouse button when clicked (left = 1, right = 2, middle = 3)
typedef enum ButtonResult {
    BT_NONE,
    BT_HOVER,
    BT_CLICK_LEFT,
    BT_CLICK_RIGHT,
    BT_CLICK_MIDDLE,
} ButtonResult;

// Toggle Button control
ButtonResult GuiToggleEx(Rectangle bounds, const char *text, bool *active)
{
    ButtonResult result = BT_NONE;
    GuiState state = guiState;

    bool temp = false;
    if (active == NULL) active = &temp;

    // Update control
    //--------------------------------------------------------------------
    if (!guiLocked && !guiControlExclusiveMode)
    {
        Vector2 mousePoint = GetMousePosition();

        // Check toggle button state
        if (CheckCollisionPointRec(mousePoint, bounds))
        {
            result = BT_HOVER;
            state = STATE_FOCUSED;

            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) 
                state = STATE_PRESSED;
            else if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
            {
                state = STATE_NORMAL;
                if (guiState != STATE_DISABLED)
                    *active = !(*active);
                result = BT_CLICK_LEFT;
            }
            else if (IsMouseButtonReleased(MOUSE_RIGHT_BUTTON))
                result = BT_CLICK_RIGHT;
            else if (IsMouseButtonReleased(MOUSE_MIDDLE_BUTTON))
                result = BT_CLICK_MIDDLE;
        }
    }
    if (guiState == STATE_DISABLED)
        state = STATE_DISABLED;
    //--------------------------------------------------------------------

    // Draw control
    //--------------------------------------------------------------------
    if (state == STATE_NORMAL)
    {
        GuiDrawRectangle(bounds, GuiGetStyle(TOGGLE, BORDER_WIDTH), GetColor(GuiGetStyle(TOGGLE, ((*active)? BORDER_COLOR_PRESSED : (BORDER + state*3)))), GetColor(GuiGetStyle(TOGGLE, ((*active)? BASE_COLOR_PRESSED : (BASE + state*3)))));
        GuiDrawText(text, GetTextBounds(TOGGLE, bounds), GuiGetStyle(TOGGLE, TEXT_ALIGNMENT), GetColor(GuiGetStyle(TOGGLE, ((*active)? TEXT_COLOR_PRESSED : (TEXT + state*3)))));
    }
    else
    {
        GuiDrawRectangle(bounds, GuiGetStyle(TOGGLE, BORDER_WIDTH), GetColor(GuiGetStyle(TOGGLE, BORDER + state*3)), GetColor(GuiGetStyle(TOGGLE, BASE + state*3)));
        GuiDrawText(text, GetTextBounds(TOGGLE, bounds), GuiGetStyle(TOGGLE, TEXT_ALIGNMENT), GetColor(GuiGetStyle(TOGGLE, TEXT + state*3)));
    }

    if (state == STATE_FOCUSED) GuiTooltip(bounds);
    //--------------------------------------------------------------------

    return result;
}

#define STATUS_SHOW_DURATION_S 5.0f

void playground(GameState *state)
{
    Camera2D camera = { 0 };
    camera.target = (Vector2){ screenWidth/2.f, screenHeight/2.f };
    camera.offset = (Vector2){ screenWidth/2.f, screenHeight/2.f };
    camera.rotation = 0.f;
    camera.zoom = 1.f;

    unsigned int frame = (unsigned int)-600; // test rollover robustness
    
    int currentType = ET_NONE;
    int editBoxActive = EB_NONE;

    Rectangle nameBox = {(float)screenWidth - 124, 4, 120, 24};
    char nameText[256] = "My cool puzzle";
    Rectangle healthBox = {(float)screenWidth - 124, 32, 120, 24};
    char healthText[256] = "1,5,1e2,-1e2,-42,-1,0.1,0.5,1e-2";
    Rectangle countBox = {(float)screenWidth - 124, 60, 120, 24};
    char countText[16] = "3";
    Rectangle spacingBox = {(float)screenWidth - 124, 88, 120, 24};
    char spacingText[16] = "120";
    Rectangle queueButton = {(float)screenWidth - 124, 116, 120, 24};

    Rectangle validateButton = {(float)screenWidth - 124, 272, 120, 24};
    Rectangle encodePuzzleButton = {(float)screenWidth - 124, 300, 120, 24};
    Rectangle encodeSolutionButton = {(float)screenWidth - 124, 328, 120, 24};
    Rectangle decodeButton = {(float)screenWidth - 124, 356, 120, 24};
    const char *statusText = "";
    double statusShowTime = 0;
    bool statusGood = true;
    bool validateRun = false;
    bool levelValidated = false;
    bool hasUpgradedTowers = false;

    enum PlaygroundGuiArea {
        TOWERS,
        PRECISION,
        SPEED_CONTROL,
        EDITS_RIGHT,
    };
    Rectangle guiRects[] = {
        [PRECISION] = {0, (float)screenHeight - BUTTON_SIZE*2 - GUI_SPACING*3, BUTTON_SIZE + GUI_SPACING*2, BUTTON_SIZE + GUI_SPACING*2},
        [TOWERS] = {0, (float)screenHeight - BUTTON_SIZE - GUI_SPACING*2, (float)screenWidth, BUTTON_SIZE + GUI_SPACING*2},
        [SPEED_CONTROL] = {0, 0, (float)screenWidth, TOWER_SIZE + 1},
        [EDITS_RIGHT] = {(float)screenWidth - 150, 0, (float)screenWidth, (float)screenHeight},
    };

    bool paused = false;
    bool sceneChange = false;
    int speedLevel = 1;

    state->home.upgradeAllowed = true;

    // Main game loop
    while (!WindowShouldClose() && !sceneChange)
    {
        if (IsWindowResized())
            UpdateGlobalScaling();

        // ------------------ Input ------------------
        if (IsKeyPressed(KEY_ESCAPE))
        {
            scene = SC_MENU;
            sceneChange = true;
            break;
        }
        if (IsKeyPressed(KEY_R) && !validateRun)
        {
            state_reset(state);
            hasUpgradedTowers = false;
        }
        if (IsKeyPressed(KEY_SPACE) && editBoxActive == EB_NONE)
        {
            paused = !paused;
        }

        if (CheckCollisionPointRec(GetMousePosition(), nameBox) && IsMouseButtonPressed(0))
            editBoxActive = EB_NAME;
        if (CheckCollisionPointRec(GetMousePosition(), countBox) && IsMouseButtonPressed(0))
            editBoxActive = EB_COUNT;
        if (CheckCollisionPointRec(GetMousePosition(), healthBox) && IsMouseButtonPressed(0))
            editBoxActive = EB_HEALTH;
        if (CheckCollisionPointRec(GetMousePosition(), spacingBox) && IsMouseButtonPressed(0))
            editBoxActive = EB_SPACING;

        int tileX = GetMouseX() / TOWER_SIZE;
        int tileY = GetMouseY() / TOWER_SIZE;
        bool canPlaceTower = editBoxActive == EB_NONE;
        int upgradeTowerIndex = -1;
        checkPlaceTower(&canPlaceTower, &upgradeTowerIndex, state, GetMousePosition(), currentType, 
                guiRects, ARRAY_SIZE(guiRects));
        if (IsMouseButtonPressed(0) && canPlaceTower)
        {
            if (upgradeTowerIndex == -1)
                state_addTower(state->towers, &state->towerLen, tileX, tileY, 
                        currentType, TOWER_DEF[currentType].scale);
            else
            {
                state->towers[upgradeTowerIndex].scale += 1;
                hasUpgradedTowers = true;
            }
            levelValidated = false;
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
        if (validateRun)
        {
            int aliveCount = countAlive(state->enemies, state->enemiesLen);
            if (aliveCount == 0)
            {
                validateRun = false;
                statusShowTime = GetTime();
                speedLevel = 1;
                if (state->home.health == HEALTH_DEFAULT)
                {
                    levelValidated = true;
                    state->home.minTowers = state->towerLen;
                    statusText = "Validation successful, export unlocked";
                    statusGood = true;
                }
                else
                {
                    statusText = "Validation failed, successful solve needed";
                    statusGood = false;
                }
            }
        }

        // ------------------ Draw ------------------
        BeginTextureMode(screen);

        ClearBackground(LIGHTGRAY);

        BeginMode2D(camera);

        DrawRectangleRec(state->path, WHITE);

        // placement preview
        if (currentType != ET_NONE && !validateRun)
        {
            DrawRectangle(tileX * TOWER_SIZE, tileY * TOWER_SIZE, TOWER_SIZE, TOWER_SIZE, canPlaceTower ? GRAY : MAROON);
            if (canPlaceTower)
            {
                DrawCircleLines((int)((tileX + 0.5f) * TOWER_SIZE), (int)((tileY + 0.5f) * TOWER_SIZE), TOWER_RANGE, GRAY);
            }
        }

        level_draw(state);

        EndMode2D();

        // GUI
        if (GuiButton((Rectangle){4, 4, 24, 24}, GuiIconText(ICON_EXIT, NULL)))
        {
            scene = SC_MENU;
            sceneChange = true;
            break;
        }
        speedControls(&speedLevel, &paused, (screenWidth - 4 * 24 - 3 * GUI_SPACING) / 2);
        if (validateRun)
        {
            int textW = MeasureText("VALIDATION IN PROGRESS", FONT_SIZE);
            DrawText("VALIDATION IN PROGRESS", (screenWidth - textW) / 2, 40, FONT_SIZE, BLACK);
        }
        if (paused)
        {
            int textW = MeasureText("PAUSED", FONT_SIZE * 2);
            DrawText("PAUSED", (screenWidth - textW) / 2, 64, FONT_SIZE * 2, BLACK);
        }

        if (validateRun) // Disable all GUI while validating level
            GuiLock();

        GuiLabel((Rectangle){nameBox.x - 60, nameBox.y, 60, nameBox.height}, "Name:");
        if (GuiTextBox(nameBox, nameText, sizeof(nameText), editBoxActive == EB_NAME))
        {
            editBoxActive = EB_NONE;
        }
        if (editBoxActive == EB_NAME) { GuiLock(); }
        GuiLabel((Rectangle){healthBox.x - 60, healthBox.y, 60, healthBox.height}, "Health:");
        if (GuiTextBox(healthBox, healthText, sizeof(healthText), editBoxActive == EB_HEALTH))
        {
            levelValidated = false;
            editBoxActive = EB_NONE;
        }
        if (editBoxActive == EB_HEALTH) { GuiLock(); }
        GuiLabel((Rectangle){countBox.x - 60, countBox.y, 60, countBox.height}, "Count:");
        if (GuiTextBox(countBox, countText, sizeof(countText), editBoxActive == EB_COUNT))
        {
            int value = atoi(countText);
            if (value <= 0)
                value = 1;
            snprintf(countText, sizeof(countText), "%d", value);

            levelValidated = false;
            editBoxActive = EB_NONE;
        }
        if (editBoxActive == EB_COUNT) { GuiLock(); }
        GuiLabel((Rectangle){spacingBox.x - 60, spacingBox.y, 60, spacingBox.height}, "Spacing:");
        if (GuiTextBox(spacingBox, spacingText, sizeof(spacingText), editBoxActive == EB_SPACING))
        {
            int value = atoi(spacingText);
            if (value <= 0)
                value = 120;
            snprintf(spacingText, sizeof(spacingText), "%d", value);

            levelValidated = false;
            editBoxActive = EB_NONE;
        }
        if (editBoxActive == EB_SPACING) { GuiLock(); }
        if (GuiButton(queueButton, "Queue Spawn"))
        {
            int count = atoi(countText);
            int spacing = atoi(spacingText);
            if (count > 0 && spacing > 0)
                state_addQueueFromString(state, frame, healthText, count, spacing);
        }

        if (GuiButton(validateButton, "Validate Puzzle"))
        {
            int count = atoi(countText);
            int spacing = atoi(spacingText);
            if (count > 0 && spacing > 0)
            {
                validateRun = true;
                state_addQueueFromString(state, frame, healthText, count, spacing);
                state->home.health = HEALTH_DEFAULT;
                speedLevel = 12;
            }
        }
        if (!levelValidated)
            GuiSetState(STATE_DISABLED);
        bool copyPuzzle = GuiButton(encodePuzzleButton, "Puzzle to Clipboard");
        bool copySolution = GuiButton(encodeSolutionButton, "Solution to Clipboard");
        if (copyPuzzle || copySolution)
        {
            int count = atoi(countText);
            int spacing = atoi(spacingText);
            assert(count > 0);
            assert(spacing > 0);

            char levelStr[2048] = "";
            LevelDef level = {
                .name = nameText,
                .cat = LC_NATURAL,
                .health = healthText,
                .count = count,
                .spacing = spacing,
                .towersAllowed = state->home.allowedTowers,
                .minSolution = state->home.minTowers,
                .roundingFactor = state->home.roundingFactor,
                .upgradeAllowed = state->home.upgradeAllowed,
            };
            bool res = state_levelToString(levelStr, sizeof(levelStr), level, state->towers, 
                copySolution ? state->towerLen : 0);
            if (res)
            {
                SetClipboardText(levelStr);
                printf("Successfully copied level to clipboard!\n");
                if (copySolution)
                    statusText = "Solution copied to clipboard";
                else
                    statusText = "Level copied to clipboard";
                statusGood = true;
                statusShowTime = GetTime();
            }
            else
            {
                printf("ERROR: Failed to generate level string!\n");
                statusText = "Failed to encode level";
                statusGood = false;
                statusShowTime = GetTime();
            }
        }
        GuiSetState(STATE_NORMAL);
        if (GuiButton(decodeButton, "Load from Clipboard"))
        {
            const char *clipboard = GetClipboardText();
            LevelDef level = {0};
            char name[sizeof(nameText)] = "";
            char health[sizeof(healthText)] = "";
            Tower towers[MAX_TOWERS] = {0};
            unsigned int towerCnt = 0;
            bool res = state_levelFromString(&level, name, sizeof(name), health, sizeof(health),
                    towers, &towerCnt, clipboard);
            if (res)
            {
                strcpy(nameText, name);
                strcpy(healthText, health);
                sprintf(countText, "%d", level.count);
                sprintf(spacingText, "%d", level.spacing);
                state->home.allowedTowers = level.towersAllowed;
                state->home.minTowers = level.minSolution;
                state->home.roundingFactor = level.roundingFactor;
                state->home.upgradeAllowed = level.upgradeAllowed;
                state->towerLen = towerCnt;
                memcpy(state->towers, towers, sizeof(towers[0]) * towerCnt);
                printf("Successfully loaded level from clipboard!\n");
                hasUpgradedTowers = false;
                for (unsigned int idx = 0; idx < state->towerLen; ++idx)
                {
                    if (state->towers[idx].scale != TOWER_DEF[state->towers[idx].type].scale)
                    {
                        hasUpgradedTowers = true;
                        break;
                    }
                }
                statusText = "Load successful";
                statusGood = true;
                statusShowTime = GetTime();
            }
            else
            {
                printf("ERROR: Failed to load level from string!\n");
                statusText = "Failed to load";
                statusGood = false;
                statusShowTime = GetTime();
            }
            levelValidated = false;
        }
        if (statusText[0] != 0 && GetTime() - statusShowTime < STATUS_SHOW_DURATION_S)
        {
            int width = MeasureText(statusText, 10);
            DrawText(statusText, screenWidth - width - 8, screenHeight - 64, 10, statusGood ? DARKGREEN : RED);
        }

        int xPos = 4;
        int yPos = screenHeight - BUTTON_SIZE - GUI_SPACING;
        char text[64] = "";
        for (int tdx = 0; tdx < ET_EOL; ++tdx)
        {
            if (tdx == ET_ROUND)
                snprintf(text, sizeof(text), TOWER_DEF[tdx].text, TOWER_DEF[tdx].scale - 1, 1 / powf(10, (float)TOWER_DEF[tdx].scale - 1));
            else
                snprintf(text, sizeof(text), TOWER_DEF[tdx].text, TOWER_DEF[tdx].scale);
            bool active = currentType == tdx;
            if (!FLAG_TEST(state->home.allowedTowers, tdx))
                GuiSetState(STATE_DISABLED);
            ButtonResult res = GuiToggleEx((Rectangle){ (float)xPos, (float)yPos, BUTTON_SIZE, BUTTON_SIZE}, text, &active);
            GuiSetState(STATE_NORMAL);
            if (active)
                currentType = tdx;
            if (res == BT_CLICK_RIGHT)
            {
                FLAG_TOGGLE(state->home.allowedTowers, tdx);
                if (active)
                    currentType = ET_NONE;
                levelValidated = false;
            }

            xPos += BUTTON_SIZE + GUI_SPACING;
        }

        if (hasUpgradedTowers)
            GuiSetState(STATE_DISABLED);
        if(GuiCheckBox((Rectangle){(float)xPos, (float)yPos, 24, 24}, "Upgrade Allowed", &state->home.upgradeAllowed))
            levelValidated = false;
        GuiSetState(STATE_NORMAL);

        xPos = 4;
        yPos -= BUTTON_SIZE + GUI_SPACING;
        if (GuiButton((Rectangle){(float)xPos, (float)yPos, (BUTTON_SIZE - GUI_SPACING) / 2, BUTTON_SIZE}, 
            GuiIconText(ICON_ARROW_LEFT, NULL)))
        {
            if (state->home.roundingFactor > 1)
            {
                state->home.roundingFactor /= 10;
                levelValidated = false;
            }
            else if (state->home.roundingFactor == 1)
            {
                state->home.roundingFactor = 0;
                levelValidated = false;
            }
        }
        if (GuiButton((Rectangle){(float)xPos + (BUTTON_SIZE + GUI_SPACING) / 2, (float)yPos, (BUTTON_SIZE - GUI_SPACING) / 2, BUTTON_SIZE}, 
            GuiIconText(ICON_ARROW_RIGHT, NULL)))
        {
            if (state->home.roundingFactor == 0)
            {
                state->home.roundingFactor = 1;
                levelValidated = false;
            }
            else if (state->home.roundingFactor < 1e5)
            {
                state->home.roundingFactor *= 10;
                levelValidated = false;
            }
        }
        xPos += BUTTON_SIZE + GUI_SPACING * 2;
        if (state->home.minTowers > 0)
            snprintf(text, sizeof(text), "Par: %d", state->home.minTowers);
        else
            strcpy(text, "Par: ??");
        DrawText(text, screenWidth - 84, screenHeight - FONT_SIZE - GUI_SPACING, FONT_SIZE, BLACK);
        if (state->home.roundingFactor == 0)
            snprintf(text, sizeof(text), "Precision: full float");
        else
            snprintf(text, sizeof(text), "Precision: %.*f", (int)log10f((float)state->home.roundingFactor), 1 / (float)state->home.roundingFactor);
        DrawText(text, xPos, yPos + (BUTTON_SIZE - FONT_SIZE) / 2, FONT_SIZE, BLACK);

    #ifdef _DEBUG
        yPos = 4;
        snprintf(text, sizeof(text), "Frame: %u", frame);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;
        snprintf(text, sizeof(text), "Towers: %d / %d", state->towerLen, MAX_TOWERS);
        DrawText(text, 4, yPos, FONT_SIZE, BLACK);
        yPos += 24;
        int aliveCount = countAlive(state->enemies, state->enemiesLen);
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
    #endif

        GuiUnlock();
        EndTextureMode();

        DrawScreenScaled();
    }
}
