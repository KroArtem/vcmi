#include "StdInc.h"
#include "CCallback.h"

#include "lib/CCreatureHandler.h"
#include "client/CGameInfo.h"
#include "lib/CGameState.h"
#include "lib/BattleState.h"
#include "client/CPlayerInterface.h"
#include "client/Client.h"
#include "lib/map.h"
#include "lib/CBuildingHandler.h"
#include "lib/CDefObjInfoHandler.h"
#include "lib/CGeneralTextHandler.h"
#include "lib/CHeroHandler.h"
#include "lib/CObjectHandler.h"
#include "lib/Connection.h"
#include "lib/NetPacks.h"
#include "client/mapHandler.h"
#include "lib/CSpellHandler.h"
#include "lib/CArtHandler.h"
#include "lib/GameConstants.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "lib/UnlockGuard.h"

/*
 * CCallback.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

template <ui16 N> bool isType(CPack *pack)
{
	return pack->getType() == N;
}

bool CCallback::teleportHero(const CGHeroInstance *who, const CGTownInstance *where)
{
	CastleTeleportHero pack(who->id, where->id, 1);
	sendRequest(&pack);
	return true;
}

bool CCallback::moveHero(const CGHeroInstance *h, int3 dst)
{
	MoveHero pack(dst,h->id);
	sendRequest(&pack);
	return true;
}
void CCallback::selectionMade(int selection, int asker)
{
	QueryReply pack(asker,selection);
	pack.player = player;
	cl->serv->sendPackToServer(pack, player);
}
void CCallback::recruitCreatures(const CGObjectInstance *obj, ui32 ID, ui32 amount, si32 level/*=-1*/)
{
	if(player!=obj->tempOwner  &&  obj->ID != 106)
		return;

	RecruitCreatures pack(obj->id,ID,amount,level);
	sendRequest(&pack);
}

bool CCallback::dismissCreature(const CArmedInstance *obj, int stackPos)
{
	if(((player>=0)  &&  obj->tempOwner != player) || (obj->stacksCount()<2  && obj->needsLastStack()))
		return false;

	DisbandCreature pack(stackPos,obj->id);
	sendRequest(&pack);
	return true;
}

bool CCallback::upgradeCreature(const CArmedInstance *obj, int stackPos, int newID)
{
	UpgradeCreature pack(stackPos,obj->id,newID);
	sendRequest(&pack);
	return false;
}

void CCallback::endTurn()
{
	tlog5 << "Player " << (unsigned)player << " ended his turn." << std::endl;
	EndTurn pack;
	sendRequest(&pack); //report that we ended turn
}
int CCallback::swapCreatures(const CArmedInstance *s1, const CArmedInstance *s2, int p1, int p2)
{
	ArrangeStacks pack(1,p1,p2,s1->id,s2->id,0);
	sendRequest(&pack);
	return 0;
}

int CCallback::mergeStacks(const CArmedInstance *s1, const CArmedInstance *s2, int p1, int p2)
{
	ArrangeStacks pack(2,p1,p2,s1->id,s2->id,0);
	sendRequest(&pack);
	return 0;
}
int CCallback::splitStack(const CArmedInstance *s1, const CArmedInstance *s2, int p1, int p2, int val)
{
	ArrangeStacks pack(3,p1,p2,s1->id,s2->id,val);
	sendRequest(&pack);
	return 0;
}

bool CCallback::dismissHero(const CGHeroInstance *hero)
{
	if(player!=hero->tempOwner) return false;

	DismissHero pack(hero->id);
	sendRequest(&pack);
	return true;
}

// int CCallback::getMySerial() const
// {	
// 	boost::shared_lock<boost::shared_mutex> lock(*gs->mx);
// 	return gs->players[player].serial;
// }

bool CCallback::swapArtifacts(const IArtifactSetBase * src, ui16 pos1, const IArtifactSetBase * dest, ui16 pos2)
{
	const CStackInstance * stack1 = dynamic_cast<const CStackInstance*>(src);
	const CStackInstance * stack2 = dynamic_cast<const CStackInstance*>(dest);
	const CGHeroInstance * hero1 = dynamic_cast<const CGHeroInstance*>(src);
	const CGHeroInstance * hero2 = dynamic_cast<const CGHeroInstance*>(dest);

	ExchangeArtifacts ea(0,0,0,0);

	if (hero1 && hero2)
	{
		if(player!=hero1->tempOwner && player!=hero2->tempOwner) //player can exchange artifacts only between his own heroes
			return false;
		else
		{
			ExchangeArtifacts ea(hero1->id, hero2->id, pos1, pos2);
			sendRequest(&ea);
			return true;
		}
	}
	else if (hero1 && stack2) //move artifact from hero to stack
	{
		ea.hid1 = hero1->id;
		ea.s2 = StackLocation(stack2->armyObj, stack2->armyObj->findStack(stack2));
		ea.slot1 = pos1;
		ea.slot2 = pos2;
		sendRequest(&ea);
		return true;
	}
	else if (stack1 && hero2) //move artifacts from stakc to hero
	{
		ea.s1 = StackLocation(stack1->armyObj, stack1->armyObj->findStack(stack1));
		ea.hid2 = hero2->id;
		ea.slot1 = pos1;
		ea.slot2 = pos2;
		sendRequest(&ea);
		return true;
	}
	else if (stack1 && stack2)
	{
		//TODO: merge stacks?
	}
	else
		return false;
}

/**
 * Assembles or disassembles a combination artifact.
 * @param hero Hero holding the artifact(s).
 * @param artifactSlot The worn slot ID of the combination- or constituent artifact.
 * @param assemble True for assembly operation, false for disassembly.
 * @param assembleTo If assemble is true, this represents the artifact ID of the combination
 * artifact to assemble to. Otherwise it's not used.
 */
bool CCallback::assembleArtifacts (const CGHeroInstance * hero, ui16 artifactSlot, bool assemble, ui32 assembleTo)
{
	if (player != hero->tempOwner)
		return false;

	AssembleArtifacts aa(hero->id, artifactSlot, assemble, assembleTo);
	sendRequest(&aa);
	return true;
}

bool CCallback::buildBuilding(const CGTownInstance *town, si32 buildingID)
{
	//CGTownInstance * t = const_cast<CGTownInstance *>(town);

	if(town->tempOwner!=player)
		return false;

	if(!canBuildStructure(town, buildingID))
		return false;
// 	const CBuilding *b = CGI->buildh->buildings[t->subID][buildingID];
// 	for(int i=0;i<b->resources.size();i++)
// 		if(b->resources[i] > gs->players[player].resources[i])
// 			return false; //lack of resources

	BuildStructure pack(town->id,buildingID);
	sendRequest(&pack);
	return true;
}

int CBattleCallback::battleMakeAction(BattleAction* action)
{
	assert(action->actionType == BattleAction::HERO_SPELL);
	MakeCustomAction mca(*action);
	sendRequest(&mca);
	return 0;
}

void CBattleCallback::sendRequest(const CPack* request)
{

	//TODO should be part of CClient (client owns connection, not CB)
	//but it would have to be very tricky cause template/serialization issues
	if(waitTillRealize)
		cl->waitingRequest.set(typeList.getTypeID(request));

	cl->serv->sendPackToServer(*request, player);

	if(waitTillRealize)
	{
		std::unique_ptr<vstd::unlock_shared_guard> unlocker; //optional, if flag set
		if(unlockGsWhenWaiting)
			unlocker = vstd::make_unique<vstd::unlock_shared_guard>(vstd::makeUnlockSharedGuard(getGsMutex()));

		cl->waitingRequest.waitWhileTrue();
	}
}

void CCallback::swapGarrisonHero( const CGTownInstance *town )
{
	if(town->tempOwner != player) return;

	GarrisonHeroSwap pack(town->id);
	sendRequest(&pack);
}

void CCallback::buyArtifact(const CGHeroInstance *hero, int aid)
{
	if(hero->tempOwner != player) return;

	BuyArtifact pack(hero->id,aid);
	sendRequest(&pack);
}

void CCallback::trade(const CGObjectInstance *market, int mode, int id1, int id2, int val1, const CGHeroInstance *hero/* = NULL*/)
{
	TradeOnMarketplace pack;
	pack.market = market;
	pack.hero = hero;
	pack.mode = mode;
	pack.r1 = id1;
	pack.r2 = id2;
	pack.val = val1;
	sendRequest(&pack);
}

void CCallback::setFormation(const CGHeroInstance * hero, bool tight)
{
	const_cast<CGHeroInstance*>(hero)-> formation = tight;
	SetFormation pack(hero->id,tight);
	sendRequest(&pack);
}

void CCallback::setSelection(const CArmedInstance * obj)
{
	SetSelection ss;
	ss.player = player;
	ss.id = obj->id;
	sendRequest(&(CPackForClient&)ss);

	if(obj->ID == GameConstants::HEROI_TYPE)
	{
		if(cl->pathInfo->hero != obj) //calculate new paths only if we selected a different hero
			cl->calculatePaths(static_cast<const CGHeroInstance *>(obj));

		//nasty workaround. TODO: nice workaround
		cl->gs->getPlayer(player)->currentSelection = obj->id;
	}
}

void CCallback::recruitHero(const CGObjectInstance *townOrTavern, const CGHeroInstance *hero)
{
	ui8 i=0;
	for(; i<gs->players[player].availableHeroes.size(); i++)
	{
		if(gs->players[player].availableHeroes[i] == hero)
		{
			HireHero pack(i,townOrTavern->id);
			pack.player = player;
			sendRequest(&pack);
			return;
		}
	}
}

bool CCallback::getPath(int3 src, int3 dest, const CGHeroInstance * hero, CPath &ret)
{
	return gs->getPath(src,dest,hero, ret);
}

void CCallback::save( const std::string &fname )
{
	cl->save(fname);
}


void CCallback::sendMessage(const std::string &mess)
{
	PlayerMessage pm(player, mess);
	sendRequest(&(CPackForClient&)pm);
}

void CCallback::buildBoat( const IShipyard *obj )
{
	BuildBoat bb;
	bb.objid = obj->o->id;
	sendRequest(&bb);
}

CCallback::CCallback( CGameState * GS, int Player, CClient *C ) 
	:CBattleCallback(GS, Player, C)
{
	waitTillRealize = false;
	unlockGsWhenWaiting = false;
}

const CGPathNode * CCallback::getPathInfo( int3 tile )
{
	if (!gs->map->isInTheMap(tile))
		return nullptr;

	validatePaths();
	return &cl->pathInfo->nodes[tile.x][tile.y][tile.z];
}

bool CCallback::getPath2( int3 dest, CGPath &ret )
{
	if (!gs->map->isInTheMap(dest))
		return false;

	validatePaths();

	boost::unique_lock<boost::mutex> pathLock(cl->pathMx);
	return cl->pathInfo->getPath(dest, ret);
}

void CCallback::recalculatePaths()
{
	cl->calculatePaths(cl->IGameCallback::getSelectedHero(player));
}

void CCallback::calculatePaths( const CGHeroInstance *hero, CPathsInfo &out, int3 src /*= int3(-1,-1,-1)*/, int movement /*= -1*/ )
{
	gs->calculatePaths(hero, out, src, movement);
}

void CCallback::dig( const CGObjectInstance *hero )
{
	DigWithHero dwh;
	dwh.id = hero->id;
	sendRequest(&dwh);
}

void CCallback::castSpell(const CGHeroInstance *hero, int spellID, const int3 &pos)
{
	CastAdvSpell cas;
	cas.hid = hero->id;
	cas.sid = spellID;
	cas.pos = pos;
	sendRequest(&cas);
}

void CCallback::unregisterMyInterface()
{
	assert(player >= 0); //works only for player callback
	cl->playerint.erase(player);
	cl->battleints.erase(player);
	//TODO? should callback be disabled as well?
}

void CCallback::validatePaths()
{
	const CGHeroInstance *h = cl->IGameCallback::getSelectedHero(player);
	if(cl->pathInfo->hero != h							//wrong hero
		|| cl->pathInfo->hpos != h->getPosition(false)  //wrong hero positoin
		|| !cl->pathInfo->isValid) //paths invalidated by game event
	{ 
		recalculatePaths();
	}
}

CBattleCallback::CBattleCallback(CGameState *GS, int Player, CClient *C )
{
	gs = GS;
	player = Player;
	cl = C;
}

bool CBattleCallback::battleMakeTacticAction( BattleAction * action )
{
	assert(cl->gs->curB->tacticDistance);
	MakeAction ma;
	ma.ba = *action;
	sendRequest(&ma);
	return true;
}
