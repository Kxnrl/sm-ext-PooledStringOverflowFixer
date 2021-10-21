#ifndef _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_

#include "smsdk_ext.h"

class PooledStringFix : public SDKExtension
{
public:
    // SDKExtension
    virtual bool SDK_OnLoad(char* error, size_t maxlength, bool late);
    virtual void SDK_OnUnload();

    virtual void OnCoreMapStart(edict_t* pEdictList, int edictCount, int clientMax);
    virtual void OnCoreMapEnd();
};

#endif