#pragma once

class Player;
class PlayerbotMgr;
class ChatHandler;
class PerformanceMonitorOperation;

#include <memory>

class PlayerbotAIBase
{
public:
    PlayerbotAIBase();

public:
    bool IsActive() const;
    virtual void UpdateAI(uint32 elapsed);
    
    uint32 GetAIInternalUpdateDelay() const { return aiInternalUpdateDelay; }

    // How long the bot waits between AI passes, i.e. how fast it reacts. Virtual so
    // PlayerbotAI can slow it down while fighting a real player (PopulateSoftenEnemyPvp).
    virtual uint32 GetReactDelay();

protected:
    virtual void UpdateAIInternal(uint32 elapsed, bool minimal = false);
    bool CanUpdateAIInternal() const { return aiInternalUpdateDelay < 100U; }
    void SetAIInternalUpdateDelay(const uint32 delay);
    void ResetAIInternalUpdateDelay() { aiInternalUpdateDelay = 0U; }
    void IncreaseAIInternalUpdateDelay(uint32 delay);
    void YieldAIInternalThread(bool minimal = false);
    
protected:
	uint32 aiInternalUpdateDelay;

    std::unique_ptr<PerformanceMonitorOperation> totalPmo;
};
