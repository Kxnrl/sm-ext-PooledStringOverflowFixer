#include "extension.h"
#include "list"

#include "stringpool.h"
#include "CDetour/detours.h"

// m_bForcePurgeFixedupStrings
// (bool*)(char*)pEntity + 838

//#define DEBUG

//#undef WIN32


PooledStringFix g_Extension;
SMEXT_LINK(&g_Extension);

class CGameStringPool : public CStringPool
#ifdef DEBUG
{
    virtual char const* Name() { return "CGameStringPool"; }

    virtual void LevelShutdownPostEntity()
    {
        FreeAll();
        PurgeDeferredDeleteList();
        //CGameString::IncrementSerialNumber();
    }

public:
    ~CGameStringPool()
    {
        PurgeDeferredDeleteList();
    }

    void PurgeDeferredDeleteList()
    {
        for (int i = 0; i < m_DeferredDeleteList.Count(); ++i)
        {
            free((void*)m_DeferredDeleteList[i]);
        }
        m_DeferredDeleteList.Purge();
    }

    void Dump(void)
    {
        for (int i = m_Strings.FirstInorder(); i != m_Strings.InvalidIndex(); i = m_Strings.NextInorder(i))
        {
            DevMsg("  %d (0x%p) : %s\n", i, m_Strings[i], m_Strings[i]);
        }
        DevMsg("\n");
        DevMsg("Size:  %d items\n", m_Strings.Count());
    }

    void Remove(const char* pszValue)
    {
        int i = m_Strings.Find(pszValue);
        if (i != m_Strings.InvalidIndex())
        {
            m_DeferredDeleteList.AddToTail(m_Strings[i]);
            m_Strings.RemoveAt(i);
        }
    }

private:
    CUtlVector< const char* > m_DeferredDeleteList;
};
#else
{};
#endif


std::list<const char*> g_PooledString;

IGameConfig* g_pGameConf;
CGameStringPool* g_pGameStringPool;

CDetour* g_pSetText;
CDetour* g_pKeyValue;
CDetour* g_pCEventAction;
CDetour* g_pDoEntFireByInstanceHandle;
CDetour* g_pDoEntFire;
CDetour* g_pCleanUpMap;
CDetour* g_pAllocPooledString;
CDetour* g_pRemovePooledString;

bool g_bQueuePool;


#ifdef WIN32
DETOUR_DECL_MEMBER1(AllocPooledString, const char*, const char*, pszValue)
{
    if (g_pGameStringPool == nullptr)
        g_pGameStringPool = reinterpret_cast<CGameStringPool*>(this);

    auto string = DETOUR_MEMBER_CALL(AllocPooledString)(pszValue);

#else
DETOUR_DECL_STATIC2(AllocPooledString, const char*, void*, pool, const char*, pszValue)
{
    if (g_pGameStringPool == nullptr)
        g_pGameStringPool = reinterpret_cast<CGameStringPool*>(pool);

    auto string = DETOUR_STATIC_CALL(AllocPooledString)(pool, pszValue);

#endif
    // in hook or say command
    if (string && (g_bQueuePool || ((string[0] == 's' || string[0] == 'S') && strncasecmp(string, "say ", 4) == 0)))
    {
        g_PooledString.push_back(string);

#ifdef DEBUG
        smutils->LogMessage(myself, __FUNCTION__" -> Added PooledString -> [%s]", string);
    }
    else
    {
        g_PooledString.push_back(string);
        smutils->LogMessage(myself, __FUNCTION__" -> Skip PooledString -> [%s]", string);
#endif
    }

    return string;
}

#ifdef DEBUG
void AddPooledString(const char* pszValue, const char* fn)
#else
void AddPooledString(const char* pszValue)
#endif
{
    if (g_pGameStringPool == nullptr)
        return;

#ifdef WIN32
    auto string = ((reinterpret_cast<AllocPooledStringClass*>(g_pGameStringPool)->*AllocPooledStringClass::AllocPooledString_Actual)(pszValue));
#else
    auto string = DETOUR_STATIC_CALL(AllocPooledString)(g_pGameStringPool, pszValue);
#endif
    
    g_PooledString.push_back(string);

#ifdef DEBUG
    smutils->LogMessage(myself, "%s -> Added PooledString -> [%s] | [%s]", fn, pszValue, string);
#endif
}

#ifdef WIN32
int(__stdcall* RemovePooledString_Actual)(const char* pszValue) = nullptr;
int __stdcall  RemovePooledString        (const char* pszValue)
#else
int(__cdecl* RemovePooledString_Actual)(const char* pszValue) = nullptr;
int __cdecl  RemovePooledString(const char* pszValue)
#endif
{
#ifdef DEBUG
    smutils->LogMessage(myself, "Removed(%s)", pszValue);
#endif
    return RemovePooledString_Actual(pszValue);
}

DETOUR_DECL_MEMBER0(CCSGameRules__CleanUpMap, void)
{
#ifdef DEBUG
    smutils->LogMessage(myself, "We have %d strings in pool", g_pGameStringPool->Count());
#endif

    if (g_pGameStringPool == nullptr)
    {
        DETOUR_MEMBER_CALL(CCSGameRules__CleanUpMap)();
        return;
    }

    if (!g_PooledString.empty())
    {
        g_PooledString.sort();
        g_PooledString.unique();

        if (!g_PooledString.empty())
        {
            smutils->LogMessage(myself, "Before clean strings -> queue %d strings, pooled %d strings", g_PooledString.size(), g_pGameStringPool->Count());

            for (auto& it : g_PooledString)
            {
                auto data = it;
                if (data)
                {
                    RemovePooledString_Actual(data);
#ifdef DEBUG
                    smutils->LogMessage(myself, "Removed PooledString -> %s", data);
#endif
                }
            }

            smutils->LogMessage(myself, "Clear All PooledString -> %d strings remaining.", g_pGameStringPool->Count());
        }

        g_PooledString.clear();
    }

#ifdef DEBUG
    engine->ServerCommand("con_logfile \"__con.log\"\n");
    engine->ServerCommand("dumpgamestringtable\n");
    engine->ServerExecute();
    engine->ServerCommand("con_logfile \"\"\n");
#endif

#ifdef DEBUG
    smutils->LogMessage(myself, "Detour_CleanUpMap");
#endif

    DETOUR_MEMBER_CALL(CCSGameRules__CleanUpMap)();

#ifdef DEBUG
    smutils->LogMessage(myself, "Detour_CleanUpMapPost");
#endif
}

DETOUR_DECL_MEMBER1(CGameText__SetText, void, inputdata_t&, inputdata)
{
    g_bQueuePool = true;
    DETOUR_MEMBER_CALL(CGameText__SetText)(inputdata);
    g_bQueuePool = true;

#ifdef DEBUG
    if (inputdata.value.fieldType == FIELD_STRING)
    {
        CBaseEntity* pEntity = reinterpret_cast<CBaseEntity*>(this);
        smutils->LogMessage(myself, "Entity = %d | Text = %s", gamehelpers->EntityToBCompatRef(pEntity), inputdata.value.iszVal.ToCStr());
    }
#endif
}

//DETOUR_DECL_MEMBER2(CGameText__KeyValue, bool, const char*, szKeyName, const char*, szValue)
DETOUR_DECL_MEMBER2(CBaseEntity__KeyValue, bool, const char*, szKeyName, const char*, szValue)
{
    bool ret;

    if (strcasecmp(szKeyName, "message") == 0)
    {
#ifdef DEBUG
        CBaseEntity* pEntity = reinterpret_cast<CBaseEntity*>(this);
        smutils->LogMessage(myself, "Entity = %d | Message = %s", gamehelpers->EntityToBCompatRef(pEntity), szValue);
#endif

        g_bQueuePool = true;
        ret = DETOUR_MEMBER_CALL(CBaseEntity__KeyValue)(szKeyName, szValue);
        g_bQueuePool = false;
    }
    else
    {
        ret = DETOUR_MEMBER_CALL(CBaseEntity__KeyValue)(szKeyName, szValue);
    }

    return ret;
}

DETOUR_DECL_MEMBER1(CEventAction__CEventAction, void, const char*, pActionData)
{
    // todo
    // NOT IMPL
    // Parse action and value using nexttoken()
    DETOUR_MEMBER_CALL(CEventAction__CEventAction)(pActionData);
}

DETOUR_DECL_STATIC6(DoEntFireByInstanceHandle, void, void*, hTarget, const char*, pszAction, const char*, pszValue, float, delay, void*, hActivator, void*, hCaller)
{
    DETOUR_STATIC_CALL(DoEntFireByInstanceHandle)(hTarget, pszAction, pszValue, delay, hActivator, hCaller);

    if (!pszValue || !pszValue[0])
        return;

    // only value
#ifdef DEBUG
    AddPooledString(pszValue, __FUNCTION__);
    smutils->LogMessage(myself, "EntFireByHandle => [%s]", pszValue);
#else
    AddPooledString(pszValue);
#endif
}

DETOUR_DECL_STATIC6(DoEntFire, void, const char*, pszTarget, const char*, pszAction, const char*, pszValue, float, delay, void*, hActivator, void*, hCaller)
{
    DETOUR_STATIC_CALL(DoEntFire)(pszTarget, pszAction, pszValue, delay, hActivator, hCaller);

    if (!pszValue || !pszValue[0])
        return;

    // only value
#ifdef DEBUG
    AddPooledString(pszValue, __FUNCTION__);
    smutils->LogMessage(myself, "EntFire => [%s]", pszValue);
#else
    AddPooledString(pszValue);
#endif
}

void PooledStringFix::OnCoreMapStart(edict_t* pEdictList, int edictCount, int clientMax)
{
    g_pGameStringPool = nullptr;
}

void PooledStringFix::OnCoreMapEnd()
{
    g_PooledString.clear();
}

bool PooledStringFix::SDK_OnLoad(char* error, size_t maxlength, bool late)
{
    if (!gameconfs->LoadGameConfigFile("PooledStringFix.games", &g_pGameConf, error, maxlength))
        return false;

    CDetourManager::Init(smutils->GetScriptingEngine(), g_pGameConf);

    g_pSetText = DETOUR_CREATE_MEMBER(CGameText__SetText, "CGameText::SetText");
    g_pKeyValue = DETOUR_CREATE_MEMBER(CBaseEntity__KeyValue, "CBaseEntity::KeyValue"); // CEnvHudHint, CMessage, CGameText & more
    g_pCEventAction = DETOUR_CREATE_MEMBER(CEventAction__CEventAction, "CEventAction::CEventAction");
    g_pDoEntFireByInstanceHandle = DETOUR_CREATE_STATIC(DoEntFireByInstanceHandle, "DoEntFireByInstanceHandle");
    g_pDoEntFire = DETOUR_CREATE_STATIC(DoEntFire, "DoEntFire");
    g_pCleanUpMap = DETOUR_CREATE_MEMBER(CCSGameRules__CleanUpMap, "CCSGameRules::CleanUpMap");
    g_pRemovePooledString = DETOUR_CREATE_STATIC(RemovePooledString, "RemovePooledString");

#ifdef WIN32
    g_pAllocPooledString = DETOUR_CREATE_MEMBER(AllocPooledString, "AllocPooledString");
#else
    g_pAllocPooledString = DETOUR_CREATE_STATIC(AllocPooledString, "AllocPooledString");
#endif

    if (g_pSetText)
        g_pSetText->EnableDetour();

    if (g_pKeyValue)
        g_pKeyValue->EnableDetour();

    if (g_pDoEntFireByInstanceHandle)
        g_pDoEntFireByInstanceHandle->EnableDetour();

    if (g_pDoEntFire)
        g_pDoEntFire->EnableDetour();

    if (g_pCleanUpMap)
        g_pCleanUpMap->EnableDetour();

    if (g_pAllocPooledString)
        g_pAllocPooledString->EnableDetour();

    if (g_pRemovePooledString)
        g_pRemovePooledString->EnableDetour();

    /*
    if (g_pCEventAction)
        g_pCEventAction->EnableDetour();
    */

    // init
    g_pShareSys->RegisterLibrary(myself, "PooledStringFix");

    return true;
}

void PooledStringFix::SDK_OnUnload()
{
    if (g_pSetText)
        g_pSetText->Destroy();

    if (g_pKeyValue)
        g_pKeyValue->Destroy();

    if (g_pDoEntFireByInstanceHandle)
        g_pDoEntFireByInstanceHandle->Destroy();

    if (g_pDoEntFire)
        g_pDoEntFire->Destroy();

    if (g_pCleanUpMap)
        g_pCleanUpMap->Destroy();

    if (g_pCEventAction)
        g_pCEventAction->Destroy();

    if (g_pAllocPooledString)
        g_pAllocPooledString->Destroy();

    if (g_pRemovePooledString)
        g_pRemovePooledString->Destroy();
}