#include "global.h"
#include "constants/items.h"

#include "battle.h"
#include "event_data.h"
#include "item.h"
#include "money.h"
#include "pokedex.h"
#include "string_util.h"

#include "rogue.h"
#include "rogue_adventurepaths.h"
#include "rogue_controller.h"
#include "rogue_quest.h"

extern const u8 gText_QuestRewardGive[];
extern const u8 gText_QuestRewardGiveMoney[];
extern const u8 gText_QuestLogStatusIncomplete[];

extern EWRAM_DATA struct RogueQuestData gRogueQuestData;

static EWRAM_DATA u8 sRewardQuest = 0;
static EWRAM_DATA u8 sRewardParam = 0;
static EWRAM_DATA u8 sPreviousRouteType = 0;

typedef void (*QuestCallback)(u16 questId, struct RogueQuestState* state);

static const u16 TypeToMonoQuest[NUMBER_OF_MON_TYPES] =
{
    [TYPE_NORMAL] = QUEST_NORMAL_Champion,
    [TYPE_FIGHTING] = QUEST_FIGHTING_Champion,
    [TYPE_FLYING] = QUEST_FLYING_Champion,
    [TYPE_POISON] = QUEST_POISON_Champion,
    [TYPE_GROUND] = QUEST_GROUND_Champion,
    [TYPE_ROCK] = QUEST_ROCK_Champion,
    [TYPE_BUG] = QUEST_BUG_Champion,
    [TYPE_GHOST] = QUEST_GHOST_Champion,
    [TYPE_STEEL] = QUEST_STEEL_Champion,
    [TYPE_MYSTERY] = QUEST_NONE,
    [TYPE_FIRE] = QUEST_FIRE_Champion,
    [TYPE_WATER] = QUEST_WATER_Champion,
    [TYPE_GRASS] = QUEST_GRASS_Champion,
    [TYPE_ELECTRIC] = QUEST_ELECTRIC_Champion,
    [TYPE_PSYCHIC] = QUEST_PSYCHIC_Champion,
    [TYPE_ICE] = QUEST_ICE_Champion,
    [TYPE_DRAGON] = QUEST_DRAGON_Champion,
    [TYPE_DARK] = QUEST_DARK_Champion,
#ifdef ROGUE_EXPANSION
    [TYPE_FAIRY] = QUEST_FAIRY_Champion,
#endif
};

static void UnlockFollowingQuests(u16 questId);
static void UpdateMonoQuests(void);

void ResetQuestState(u16 saveVersion)
{
    u16 i;

    if(saveVersion == 0)
    {
        // Reset the state for any new quests
        for(i = 0; i < QUEST_CAPACITY; ++i)
        {
            memset(&gRogueQuestData.questStates[i], 0, sizeof(struct RogueQuestState));
        }

        // These quests must always be unlocked
        for(i = QUEST_FirstAdventure; i <= QUEST_Champion; ++i)
        {
            TryUnlockQuest(i);
        }
    }
}

bool8 AnyNewQuests(void)
{
    u16 i;
    struct RogueQuestState* state;

    for(i = 0; i < QUEST_CAPACITY; ++i)
    {
        state = &gRogueQuestData.questStates[i];
        if(state->isUnlocked && state->hasNewMarker)
        {
            return TRUE;
        }
    }

    return FALSE;
}

bool8 AnyQuestRewardsPending(void)
{
    u16 i;
    struct RogueQuestState* state;

    for(i = 0; i < QUEST_CAPACITY; ++i)
    {
        state = &gRogueQuestData.questStates[i];
        if(state->isUnlocked && state->hasPendingRewards)
        {
            return TRUE;
        }
    }

    return FALSE;
}

u16 GetCompletedQuestCount(void)
{
    u16 i;
    struct RogueQuestState* state;
    u16 count = 0;

    for(i = 0; i < QUEST_CAPACITY; ++i)
    {
        state = &gRogueQuestData.questStates[i];
        if(state->isUnlocked && state->isCompleted)
            ++count;
    }

    return count;
}

u16 GetUnlockedQuestCount(void)
{
    u16 i;
    struct RogueQuestState* state;
    u16 count = 0;

    for(i = 0; i < QUEST_CAPACITY; ++i)
    {
        state = &gRogueQuestData.questStates[i];
        if(state->isUnlocked)
            ++count;
    }

    return count;
}

bool8 GetQuestState(u16 questId, struct RogueQuestState* outState)
{
    if(questId < QUEST_CAPACITY)
    {
        memcpy(outState, &gRogueQuestData.questStates[questId], sizeof(struct RogueQuestState));
        return outState->isUnlocked;
    }

    return FALSE;
}

void SetQuestState(u16 questId, struct RogueQuestState* state)
{
    if(questId < QUEST_CAPACITY)
    {
        memcpy(&gRogueQuestData.questStates[questId], state, sizeof(struct RogueQuestState));
    }
}

bool8 IsQuestRepeatable(u16 questId)
{
    return (gRogueQuests[questId].flags & QUEST_FLAGS_REPEATABLE) != 0;
}

bool8 IsQuestCollected(u16 questId)
{
    struct RogueQuestState state;
    if (GetQuestState(questId, &state))
    {
        return state.isCompleted && !state.hasPendingRewards;
    }

    return FALSE;
}

bool8 IsQuestGloballyTracked(u16 questId)
{
    return (gRogueQuests[questId].flags & QUEST_FLAGS_GLOBALALLY_TRACKED) != 0;
}

bool8 IsQuestActive(u16 questId)
{
    struct RogueQuestState state;
    if (GetQuestState(questId, &state))
    {
        return state.isValid;
    }

    return FALSE;
}

bool8 DoesQuestHaveUnlocks(u16 questId)
{
    return gRogueQuests[questId].unlockedQuests[0] != QUEST_NONE;
}


static struct RogueQuestReward const* GetCurrentRewardTarget()
{
    return &gRogueQuests[sRewardQuest].rewards[sRewardParam];
}

static bool8 QueueTargetRewardQuest()
{
    u16 i;
    struct RogueQuestState* state;
    for(i = 0; i < QUEST_CAPACITY; ++i)
    {
        state = &gRogueQuestData.questStates[i];

        if(state->hasPendingRewards)
        {
            if(sRewardQuest != i)
            {
                sRewardQuest = i;
                sRewardParam = 0;
                UnlockFollowingQuests(sRewardQuest);
                // TODO - Check to see if we have enough space for all of these items
            }
            else
            {
                ++sRewardParam;
            }
            return TRUE;
        }
    }

    sRewardQuest = QUEST_NONE;
    sRewardParam = 0;
    return FALSE;
}

static bool8 QueueNextReward()
{
    while(QueueTargetRewardQuest())
    {
        struct RogueQuestReward const* reward = GetCurrentRewardTarget();

        if(sRewardParam >= QUEST_MAX_REWARD_COUNT || reward->type == QUEST_REWARD_NONE)
        {
            // We've cleared out this quest's rewards
            gRogueQuestData.questStates[sRewardQuest].hasPendingRewards = FALSE;

            sRewardQuest = QUEST_NONE;
            sRewardParam = 0;
        }
        else
        {
            return TRUE;
        }
    }

    return FALSE;
}

static bool8 GiveAndGetNextAnnouncedReward()
{
    while(QueueNextReward())
    {
        struct RogueQuestReward const* reward = GetCurrentRewardTarget();
        bool8 shouldAnnounce = reward->giveText != NULL;

        // Actually give the reward here
        switch(reward->type)
        {
            case QUEST_REWARD_SET_FLAG:
                FlagSet(reward->params[0]);
                break;

            case QUEST_REWARD_CLEAR_FLAG:
                FlagClear(reward->params[0]);
                break;

            case QUEST_REWARD_GIVE_ITEM:
                if(!AddBagItem(reward->params[0], reward->params[1]))
                {
                    AddPCItem(reward->params[0], reward->params[1]);
                }
                shouldAnnounce = TRUE;
                break;

            case QUEST_REWARD_GIVE_MONEY:
                AddMoney(&gSaveBlock1Ptr->money, reward->params[0]);
                shouldAnnounce = TRUE;
                break;

            //case QUEST_REWARD_CUSTOM_TEXT:
            //    break;
        }
        
        if(shouldAnnounce)
        {
            return TRUE;
        }
    }

    return FALSE;
}


bool8 GiveNextRewardAndFormat(u8* str, u8* type)
{
    if(GiveAndGetNextAnnouncedReward())
    {
        struct RogueQuestReward const* reward = GetCurrentRewardTarget();
        *type = reward->type;

        if(reward->giveText)
        {
            StringCopy(str, reward->giveText);
        }
        else
        {
            switch(reward->type)
            {
                case QUEST_REWARD_GIVE_ITEM:
                    CopyItemNameHandlePlural(reward->params[0], gStringVar1, reward->params[1]);
                    StringExpandPlaceholders(str, gText_QuestRewardGive);
                    break;

                case QUEST_REWARD_GIVE_MONEY:
                    ConvertUIntToDecimalStringN(gStringVar1, reward->params[0], STR_CONV_MODE_LEFT_ALIGN, 7);
                    StringExpandPlaceholders(str, gText_QuestRewardGiveMoney);
                    break;
                
                default:
                    // Just return an obviously broken message
                    StringCopy(str, gText_QuestLogStatusIncomplete);
                    break;
            }
        }
        return TRUE;
    }

    return FALSE;
}

bool8 TryUnlockQuest(u16 questId)
{
    struct RogueQuestState* state = &gRogueQuestData.questStates[questId];

    if(!state->isUnlocked)
    {
        state->isUnlocked = TRUE;
        state->isValid = FALSE;
        state->isCompleted = FALSE;
        state->hasNewMarker = TRUE;
        return TRUE;
    }

    return FALSE;
}

bool8 TryMarkQuestAsComplete(u16 questId)
{
    struct RogueQuestState* state = &gRogueQuestData.questStates[questId];

    if(state->isValid)
    {
        state->isValid = FALSE;

        if(!state->isCompleted)
        {
            // First time finishing
            state->isCompleted = TRUE;
            state->hasPendingRewards = TRUE;
        }
        else if(IsQuestRepeatable(questId))
        {
            // Has already completed this once before so has rewards pending
            state->hasPendingRewards = TRUE;
        }

        return TRUE;
    }

    return FALSE;
}

bool8 TryDeactivateQuest(u16 questId)
{
    struct RogueQuestState* state = &gRogueQuestData.questStates[questId];

    if(state->isValid)
    {
        state->isValid = FALSE;
        return TRUE;
    }

    return FALSE;
}

static void UnlockFollowingQuests(u16 questId)
{
    u8 i;

    for(i = 0; i < QUEST_MAX_FOLLOWING_QUESTS; ++i)
    {
        if(gRogueQuests[questId].unlockedQuests[i] == QUEST_NONE)
            break;

        TryUnlockQuest(gRogueQuests[questId].unlockedQuests[i]);
    }
}

static void ForEachUnlockedQuest(QuestCallback callback)
{
    u16 i;
    struct RogueQuestState* state;

    for(i = QUEST_NONE + 1; i < QUEST_CAPACITY; ++i)
    {
        state = &gRogueQuestData.questStates[i];
        if(state->isUnlocked)
        {
            callback(i, state);
        }
    }
}

static void ForEachActiveQuest(QuestCallback callback)
{
    u16 i;
    struct RogueQuestState* state;

    for(i = QUEST_NONE + 1; i < QUEST_CAPACITY; ++i)
    {
        state = &gRogueQuestData.questStates[i];
        if(state->isValid && !state->isCompleted)
        {
            callback(i, state);
        }
    }
}

static void ActivateQuestIfNeeded(u16 questId, struct RogueQuestState* state)
{
    if(state->isUnlocked)
    {
        if(IsQuestRepeatable(questId))
        {
            // Don't reactivate quests if we have rewards pending
            if(!state->hasPendingRewards)
                state->isValid = TRUE;
        }
        else if(!state->isCompleted)
        {
            state->isValid = TRUE;
        }
    }
}

static void DeactivateQuestIfNeeded(u16 questId, struct RogueQuestState* state)
{
    if(state->isValid && !IsQuestGloballyTracked(questId))
    {
        state->isValid = FALSE;
    }
}

// External callbacks
//

static void UpdateChaosChampion(bool8 enteringPotentialEncounter)
{
    struct RogueQuestState state;

    if(IsQuestActive(QUEST_ChaosChampion) && GetQuestState(QUEST_ChaosChampion, &state))
    {
        bool8 isRandomanDisabled = FlagGet(FLAG_ROGUE_RANDOM_TRADE_DISABLED);

        if(enteringPotentialEncounter)
        {
            state.data.half = isRandomanDisabled ? 0 : 1;
            SetQuestState(QUEST_ChaosChampion, &state);
        }
        else if(state.data.half)
        {
            if(!isRandomanDisabled)
            {
                // Randoman was still active i.e. we didn't use him
                TryDeactivateQuest(QUEST_ChaosChampion);
            }
            else
            {
                state.data.half = 0;
                SetQuestState(QUEST_ChaosChampion, &state);
            }
        }
    }
}

void QuestNotify_BeginAdventure(void)
{
    sPreviousRouteType = 0;

    // Cannot activate quests on Gauntlet mode
    if(!FlagGet(FLAG_ROGUE_GAUNTLET_MODE))
    {
        ForEachUnlockedQuest(ActivateQuestIfNeeded);
    }

    // Handle skip difficulty
    if(gRogueRun.currentDifficulty > 0)
    {
        u16 i;

        TryDeactivateQuest(QUEST_GymChallenge);
        TryDeactivateQuest(QUEST_GymMaster);
        TryDeactivateQuest(QUEST_NoFainting2);
        TryDeactivateQuest(QUEST_NoFainting3);

        for(i = TYPE_NORMAL; i < NUMBER_OF_MON_TYPES; ++i)
            TryDeactivateQuest(TypeToMonoQuest[i]);
    }

    if(gRogueRun.currentDifficulty > 4)
    {
        TryDeactivateQuest(QUEST_NoFainting1);
    }

    if(gRogueRun.currentDifficulty > 8)
    {
        // Can't technically happen atm
        TryDeactivateQuest(QUEST_EliteMaster);
    }

    UpdateChaosChampion(TRUE);
    UpdateMonoQuests();
}

static void OnEndBattle(void)
{
    struct RogueQuestState state;

    if(IsQuestActive(QUEST_NoFainting1) && GetQuestState(QUEST_NoFainting1, &state))
    {
        if(Rogue_IsPartnerMonInTeam() == FALSE)
        {
            state.isValid = FALSE;
            SetQuestState(QUEST_NoFainting1, &state);
        }
    }

    UpdateMonoQuests();
}

void QuestNotify_EndAdventure(void)
{
    TryMarkQuestAsComplete(QUEST_FirstAdventure);

    ForEachUnlockedQuest(DeactivateQuestIfNeeded);
}

void QuestNotify_OnWildBattleEnd(void)
{
    if(gBattleOutcome == B_OUTCOME_CAUGHT)
    {
        if(IsQuestActive(QUEST_Collector1))
        {
            u16 caughtCount = GetNationalPokedexCount(FLAG_GET_CAUGHT);
            if(caughtCount >= 15)
                TryMarkQuestAsComplete(QUEST_Collector1);
        }

        if(IsQuestActive(QUEST_DenExplorer) && gRogueAdvPath.currentRoomType == ADVPATH_ROOM_WILD_DEN)
            TryMarkQuestAsComplete(QUEST_DenExplorer);
    }

    OnEndBattle();
}

bool8 IsSpeciesType(u16 species, u8 type);

static void UpdateMonoQuests(void)
{
    u16 type;
    u16 questId;
    u8 i;

    for(type = TYPE_NORMAL; type < NUMBER_OF_MON_TYPES; ++type)
    {
        questId = TypeToMonoQuest[type];

        if(IsQuestActive(questId))
        {
            for(i = 0; i < gPlayerPartyCount; ++i)
            {
                u16 species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES);
                if(!IsSpeciesType(species, type))
                {
                    TryDeactivateQuest(questId);
                    break;
                }
            }
        }
    }
}

static void CompleteMonoQuests(void)
{
    u16 type;
    u16 questId;
    u8 i;

    for(type = TYPE_NORMAL; type < NUMBER_OF_MON_TYPES; ++type)
    {
        questId = TypeToMonoQuest[type];

        if(IsQuestActive(questId))
            TryMarkQuestAsComplete(questId);
    }
}

bool8 IsSpeciesLegendary(u16 species);

void QuestNotify_OnTrainerBattleEnd(bool8 isBossTrainer)
{
    u8 i;

    if(isBossTrainer)
    {
        u16 relativeDifficulty = gRogueRun.currentDifficulty - VarGet(VAR_ROGUE_SKIP_TO_DIFFICULTY);

        switch(gRogueRun.currentDifficulty)
        {
            case 1:
                TryMarkQuestAsComplete(QUEST_Gym1);
                break;
            case 2:
                TryMarkQuestAsComplete(QUEST_Gym2);
                break;
            case 3:
                TryMarkQuestAsComplete(QUEST_Gym3);
                break;
            case 4:
                TryMarkQuestAsComplete(QUEST_Gym4);
                break;
            case 5:
                TryMarkQuestAsComplete(QUEST_Gym5);
                break;
            case 6:
                TryMarkQuestAsComplete(QUEST_Gym6);
                break;
            case 7:
                TryMarkQuestAsComplete(QUEST_Gym7);
                break;
            case 8: // Just beat last Gym
                TryMarkQuestAsComplete(QUEST_Gym8);
                TryMarkQuestAsComplete(QUEST_NoFainting2);
                break;

            case 12: // Just beat last E4
                if(IsQuestActive(QUEST_Collector2))
                {
                    for(i = 0; i < PARTY_SIZE; ++i)
                    {
                        u16 species = GetMonData(&gPlayerParty[i], MON_DATA_SPECIES);
                        if(IsSpeciesLegendary(species))
                        {
                            TryMarkQuestAsComplete(QUEST_Collector2);
                            break;
                        }
                    }
                }
                break;

            case 14: // Just beat final champ
                TryMarkQuestAsComplete(QUEST_Champion);
                TryMarkQuestAsComplete(QUEST_NoFainting3);
                TryMarkQuestAsComplete(QUEST_ChaosChampion);
                CompleteMonoQuests();
                break;
        }

        if(gRogueRun.currentDifficulty >= 4)
            TryMarkQuestAsComplete(QUEST_GymChallenge);

        if(gRogueRun.currentDifficulty >= 8)
            TryMarkQuestAsComplete(QUEST_GymMaster);

        if(gRogueRun.currentDifficulty >= 12)
            TryMarkQuestAsComplete(QUEST_EliteMaster);

        if(relativeDifficulty == 4)
        {
            if(IsQuestActive(QUEST_NoFainting1))
                TryMarkQuestAsComplete(QUEST_NoFainting1);
        }
    }
    
    OnEndBattle();
}

void QuestNotify_OnMonFainted()
{
    TryDeactivateQuest(QUEST_NoFainting2);
    TryDeactivateQuest(QUEST_NoFainting3);
}

void QuestNotify_OnWarp(struct WarpData* warp)
{
    if(Rogue_IsRunActive())
    {
        struct RogueQuestState state;

        // Warped into
        switch(gRogueAdvPath.currentRoomType)
        {
            case ADVPATH_ROOM_ROUTE:
                if(gRogueAdvPath.currentRoomParams.perType.route.difficulty == 2)
                {
                    if(IsQuestActive(QUEST_Bike1) && GetQuestState(QUEST_Bike1, &state))
                    {
                        state.data.byte[0] = gSaveBlock2Ptr->playTimeHours;
                        state.data.byte[1] = gSaveBlock2Ptr->playTimeMinutes;
                        SetQuestState(QUEST_Bike1, &state);
                    }

                    if(gRogueRun.currentDifficulty >= 8)
                    {
                        if(IsQuestActive(QUEST_Bike2) && GetQuestState(QUEST_Bike2, &state))
                        {
                            state.data.byte[0] = gSaveBlock2Ptr->playTimeHours;
                            state.data.byte[1] = gSaveBlock2Ptr->playTimeMinutes;
                            SetQuestState(QUEST_Bike2, &state);
                        }
                    }
                }
                break;

            case ADVPATH_ROOM_RESTSTOP:
                UpdateChaosChampion(TRUE);
                break;
        }

        // Warped out of
        switch(sPreviousRouteType)
        {
            case ADVPATH_ROOM_ROUTE:
                if(gRogueAdvPath.currentRoomParams.perType.route.difficulty == 2)
                {
                    if(IsQuestActive(QUEST_Bike1) && GetQuestState(QUEST_Bike1, &state))
                    {
                        u16 startTime = ((u16)state.data.byte[0]) * 60 + ((u16)state.data.byte[1]);
                        u16 exitTime = ((u16)gSaveBlock2Ptr->playTimeHours) * 60 + ((u16)gSaveBlock2Ptr->playTimeMinutes);

                        if((exitTime - startTime) < 120)
                            TryMarkQuestAsComplete(QUEST_Bike1);
                    }

                    if(gRogueRun.currentDifficulty >= 8)
                    {
                        if(IsQuestActive(QUEST_Bike2) && GetQuestState(QUEST_Bike2, &state))
                        {
                            u16 startTime = ((u16)state.data.byte[0]) * 60 + ((u16)state.data.byte[1]);
                            u16 exitTime = ((u16)gSaveBlock2Ptr->playTimeHours) * 60 + ((u16)gSaveBlock2Ptr->playTimeMinutes);

                            if((exitTime - startTime) < 60)
                                TryMarkQuestAsComplete(QUEST_Bike2);
                        }
                    }
                }
                break;
        }

        // Could've traded by event so update mono quests
        UpdateMonoQuests();

        if(gRogueAdvPath.currentRoomType != ADVPATH_ROOM_RESTSTOP)
        {
            UpdateChaosChampion(FALSE);
        }

        sPreviousRouteType = gRogueAdvPath.currentRoomType;
    }
}

void QuestNotify_OnAddMoney(u32 amount)
{

}

void QuestNotify_OnRemoveMoney(u32 amount)
{
    if(Rogue_IsRunActive())
    {
        struct RogueQuestState state;

        if(gRogueAdvPath.currentRoomType == ADVPATH_ROOM_RESTSTOP)
        {
            if(IsQuestActive(QUEST_ShoppingSpree) && GetQuestState(QUEST_ShoppingSpree, &state))
            {
                state.data.half += amount;
                SetQuestState(QUEST_ShoppingSpree, &state);

                if(state.data.half >= 20000)
                    TryMarkQuestAsComplete(QUEST_ShoppingSpree);
            }
        }
    }
}

void QuestNotify_OnAddBagItem(u16 itemId, u16 count)
{
    if(Rogue_IsRunActive())
    {
        if(IsQuestActive(QUEST_BerryCollector))
        {
            if(itemId >= FIRST_BERRY_INDEX && itemId <= LAST_BERRY_INDEX)
            {
                u16 i;
                u16 uniqueBerryCount = 0;

                for(i = FIRST_BERRY_INDEX; i <= LAST_BERRY_INDEX; ++i)
                {
                    if(CheckBagHasItem(i,1))
                        ++uniqueBerryCount;
                }

                if(uniqueBerryCount >= 10)
                    TryMarkQuestAsComplete(QUEST_BerryCollector);
            }

        }
    }
}

void QuestNotify_OnRemoveBagItem(u16 itemId, u16 count)
{

}