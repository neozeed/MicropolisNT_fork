/* power.c - Power distribution implementation for MicropolisNT
 * Based on original Micropolis code from MicropolisLegacy project
 */

#include "simulation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* Power stack size for power distribution algorithm */
#define PWRSTKSIZE 1000

/* Power stack for distribution algorithm */
static int PowerStackNum = 0;
static short PowerStackX[PWRSTKSIZE];
static short PowerStackY[PWRSTKSIZE];

/* Power statistics */
static QUAD MaxPower = 0;
static QUAD NumPower = 0;

/* Number of each type of power plant */
static int CoalPop = 0;
static int NuclearPop = 0;

/* Function prototypes */
static void PushPowerStack(void);
static void PullPowerStack(void);
static int MoveMapSim(short MDir);
static int TestForCond(short TFDir);

/* Move in a direction on the map during power scan */
static int MoveMapSim(short MDir) {
    int xSave, ySave;

    xSave = SMapX;
    ySave = SMapY;

    switch (MDir) {
    case 0: /* North */
        if (SMapY > 0) {
            SMapY--;
            return 1;
        }
        if (SMapY < 0) {
            SMapY = 0;
        }
        return 0;

    case 1: /* East */
        if (SMapX < (WORLD_X - 1)) {
            SMapX++;
            return 1;
        }
        if (SMapX > (WORLD_X - 1)) {
            SMapX = WORLD_X - 1;
        }
        return 0;

    case 2: /* South */
        if (SMapY < (WORLD_Y - 1)) {
            SMapY++;
            return 1;
        }
        if (SMapY > (WORLD_Y - 1)) {
            SMapY = WORLD_Y - 1;
        }
        return 0;

    case 3: /* West */
        if (SMapX > 0) {
            SMapX--;
            return 1;
        }
        if (SMapX < 0) {
            SMapX = 0;
        }
        return 0;

    case 4: /* No move - stay in place */
        return 1;
    }

    return 0;
}

/* Test if tile in a certain direction can be electrified */
static int TestForCond(short TFDir) {
    int xsave, ysave;
    short tile;

    xsave = SMapX;
    ysave = SMapY;

    if (MoveMapSim(TFDir)) {
        tile = Map[SMapY][SMapX] & LOMASK;

        /* Check if tile can conduct power and is not already powered.
           NOTE: ZONEBIT-flagged tiles can also conduct power even if CONDBIT is not set.
           This is critical for residential zones to get power. */
        if (((Map[SMapY][SMapX] & CONDBIT) || (Map[SMapY][SMapX] & ZONEBIT)) && (tile != NUCLEAR) &&
            (tile != POWERPLANT) && !(Map[SMapY][SMapX] & POWERBIT)) {
            SMapX = xsave;
            SMapY = ysave;
            return 1;
        }
    }

    SMapX = xsave;
    SMapY = ysave;
    return 0;
}

/* Add position to power stack */
static void PushPowerStack(void) {
    if (PowerStackNum < (PWRSTKSIZE - 2)) {
        PowerStackNum++;
        PowerStackX[PowerStackNum] = SMapX;
        PowerStackY[PowerStackNum] = SMapY;
    }
}

/* Take position from power stack */
static void PullPowerStack(void) {
    if (PowerStackNum > 0) {
        SMapX = PowerStackX[PowerStackNum];
        SMapY = PowerStackY[PowerStackNum];
        PowerStackNum--;
    }
}

/* Count power plants */
void CountPowerPlants(void) {
    int x, y;
    short tile;

    CoalPop = 0;
    NuclearPop = 0;

    for (y = 0; y < WORLD_Y; y++) {
        for (x = 0; x < WORLD_X; x++) {
            tile = Map[y][x] & LOMASK;

            if ((Map[y][x] & ZONEBIT) != 0) {
                if (tile == POWERPLANT) {
                    CoalPop++;
                } else if (tile == NUCLEAR) {
                    NuclearPop++;
                }
            }
        }
    }
}

/* Add a power plant position to the distribution queue */
void QueuePowerPlant(int x, int y) {
    if (PowerStackNum < (PWRSTKSIZE - 2)) {
        PowerStackNum++;
        PowerStackX[PowerStackNum] = x;
        PowerStackY[PowerStackNum] = y;
    }
}

/* Find all power plants and add them to the queue */
void FindPowerPlants(void) {
    int x, y;
    short tile;

    PowerStackNum = 0;

    for (y = 0; y < WORLD_Y; y++) {
        for (x = 0; x < WORLD_X; x++) {
            tile = Map[y][x] & LOMASK;

            if ((Map[y][x] & ZONEBIT) != 0) {
                if (tile == POWERPLANT || tile == NUCLEAR) {
                    QueuePowerPlant(x, y);
                }
            }
        }
    }
}

/* Do a full power distribution scan - ORIGINAL ALGORITHM
   This uses the original Micropolis power transmission method that traces along
   power lines and conductive terrain rather than using a simple radius */
void DoPowerScan(void) {
    int x, y;
    short ADir, ConNum, Dir;

    /* Count power plants */
    CountPowerPlants();

    /* Calculate total power capacity */
    MaxPower = (CoalPop * 700L) + (NuclearPop * 2000L);
    NumPower = 0;

    /* Clear the power map first */
    for (y = 0; y < WORLD_Y; y++) {
        for (x = 0; x < WORLD_X; x++) {
            Map[y][x] &= ~POWERBIT; /* Turn off the power bit */
            PowerMap[y][x] = 0;     /* Set PowerMap to unpowered */
        }
    }

    /* If we have no power plants, no point in doing anything else */
    if (CoalPop == 0 && NuclearPop == 0) {
        /* Update power counts */
        PwrdZCnt = 0;
        UnpwrdZCnt = 0;

        /* Count unpowered zones */
        for (y = 0; y < WORLD_Y; y++) {
            for (x = 0; x < WORLD_X; x++) {
                if (Map[y][x] & ZONEBIT) {
                    UnpwrdZCnt++;
                }
            }
        }
        return;
    }

    /* Initialize the power stack with all power plants */
    FindPowerPlants();

    /* Process the power stack until empty */
    while (PowerStackNum > 0) {
        /* Get the next position from the stack */
        PullPowerStack();

        /* Start in the current position (direction 4) */
        ADir = 4;

        do {
            /* Increment the power counter - if over capacity, stop */
            if (++NumPower > MaxPower) {
                /* We've reached the power capacity limit */
                /* Update power zone counts */
                PwrdZCnt = 0;
                UnpwrdZCnt = 0;

                for (y = 0; y < WORLD_Y; y++) {
                    for (x = 0; x < WORLD_X; x++) {
                        if (Map[y][x] & ZONEBIT) {
                            if (Map[y][x] & POWERBIT) {
                                PwrdZCnt++;
                            } else {
                                UnpwrdZCnt++;
                            }
                        }
                    }
                }
                return;
            }

            /* Move to the current direction */
            MoveMapSim(ADir);

            /* Power the current position */
            Map[SMapY][SMapX] |= POWERBIT;
            PowerMap[SMapY][SMapX] = 1;

            /* Look in all four directions for conducting tiles */
            ConNum = 0;
            Dir = 0;

            while ((Dir < 4) && (ConNum < 2)) {
                /* If there is a conductive tile in this direction */
                if (TestForCond(Dir)) {
                    ConNum++;   /* Count it */
                    ADir = Dir; /* Remember direction */
                }
                Dir++;
            }

            /* If we found more than one conductive direction, branch */
            if (ConNum > 1) {
                PushPowerStack();
            }
        } while (ConNum); /* Continue as long as we have conductive paths */
    }

    /* Update power zone counts */
    PwrdZCnt = 0;
    UnpwrdZCnt = 0;

    for (y = 0; y < WORLD_Y; y++) {
        for (x = 0; x < WORLD_X; x++) {
            if (Map[y][x] & ZONEBIT) {
                if (Map[y][x] & POWERBIT) {
                    PwrdZCnt++;
                } else {
                    UnpwrdZCnt++;
                }
            }
        }
    }
}